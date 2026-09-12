#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <drm_fourcc.h>
#include <fcntl.h>
#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <xkbcommon/xkbcommon.h>

#include <wlr/backend/headless.h>
#include <wlr/render/allocator.h>
#include <wlr/render/drm_format_set.h>
#include <wlr/render/gles2.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_pointer_constraints_v1.h>
#include <wlr/types/wlr_primary_selection_v1.h>
#include <wlr/types/wlr_relative_pointer_v1.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_viewporter.h>
#include <wlr/interfaces/wlr_keyboard.h>
#include <wlr/util/log.h>
#include <wlr/util/region.h>
#include <wlr/xwayland/xwayland.h>

#include "dynscope.h"
#include "host.h"
#include "server.h"
#include "xwm.h"
#include "xcursor.h"
#include "clipboard.h"

struct wlr_egl_context {
	EGLDisplay display;
	EGLContext context;
	EGLSurface draw_surface;
	EGLSurface read_surface;
};

bool wlr_egl_make_current(struct wlr_egl *egl, struct wlr_egl_context *save_context);
bool wlr_egl_restore_context(struct wlr_egl_context *context);

static const struct wlr_keyboard_impl keyboard_impl = {
	.name = "dynscope",
};

static void server_constrain(struct server *s, struct wlr_pointer_constraint_v1 *constraint);
static void server_warp_to_constraint_hint(struct server *s);

static int frame_alloc(struct server *s, int width, int height, struct frame *out) {
	uint64_t modifiers[1] = {DRM_FORMAT_MOD_LINEAR};
	struct wlr_drm_format format = {
		.format = DRM_FORMAT_XRGB8888,
		.len = 1,
		.modifiers = modifiers,
	};

	struct wlr_buffer *buffer = wlr_allocator_create_buffer(s->allocator, width, height, &format);
	if (buffer == NULL) {
		return -1;
	}

	struct wlr_dmabuf_attributes attrs;
	if (!wlr_buffer_get_dmabuf(buffer, &attrs)) {
		wlr_buffer_drop(buffer);
		return -1;
	}

	out->buffer = buffer;
	out->attrs = attrs;
	out->generation = 0;
	out->in_flight = false;
	return 0;
}

static void frame_destroy(struct server *s, struct frame *frame) {
	(void)s;
	wlr_buffer_drop(frame->buffer);
	frame->buffer = NULL;
}

static void frame_pool_resize(struct server *s, int width, int height) {
	for (int i = 0; i < s->pool.nframes; i++)
		frame_destroy(s, &s->pool.frames[i]);
	s->pool.nframes = 0;
	s->pool.width = width;
	s->pool.height = height;
}

static struct frame *frame_pick(struct server *s, int width, int height) {
	for (int i = 0; i < s->pool.nframes; i++) {
		if (!s->pool.frames[i].in_flight)
			return &s->pool.frames[i];
	}

	if (s->pool.nframes < FRAME_POOL_MAX) {
		struct frame *frame = &s->pool.frames[s->pool.nframes];
		if (frame_alloc(s, width, height, frame) == 0) {
			s->pool.nframes++;
			return frame;
		}
	}

	struct frame *oldest = &s->pool.frames[0];
	for (int i = 1; i < s->pool.nframes; i++) {
		if (s->pool.frames[i].generation < oldest->generation)
			oldest = &s->pool.frames[i];
	}
	return oldest;
}

static void server_gpu_sync(struct server *s) {
	struct wlr_egl_context prev;
	if (!wlr_egl_make_current(s->egl, &prev))
		return;
	glFinish();
	wlr_egl_restore_context(&prev);
}

static void server_update_fit(struct server *s, int width, int height) {
	s->win_w = width;
	s->win_h = height;

	int game_w = 0;
	int game_h = 0;
	xwm_game_size(s, &game_w, &game_h);

	dynscope_log_debug("dynscope: fit window=%dx%d game=%dx%d\n", width, height, game_w, game_h);

	if (s->xwm == NULL || game_w <= 0 || game_h <= 0) {
		s->fit.scale = 1.0;
		s->fit.x = 0.0;
		s->fit.y = 0.0;
		s->fit.w = width;
		s->fit.h = height;
		return;
	}

	double scale = (double)width / (double)game_w;
	double scale_y = (double)height / (double)game_h;
	if (scale_y < scale)
		scale = scale_y;
	int w = (int)((double)game_w * scale + 0.5);
	int h = (int)((double)game_h * scale + 0.5);
	if (w > width)
		w = width;
	if (h > height)
		h = height;

	s->fit.scale = scale;
	s->fit.x = (double)(width - w) / 2.0;
	s->fit.y = (double)(height - h) / 2.0;
	s->fit.w = w;
	s->fit.h = h;

}

void server_present(struct dynscope *ds, int width, int height, struct frame_info *out) {
	struct server *s = ds->server;
	memset(out, 0, sizeof(*out));
	out->fd = -1;
	if (width <= 0 || height <= 0 || s == NULL)
		return;

	server_update_fit(s, width, height);

	if (s->pool.width != width || s->pool.height != height)
		frame_pool_resize(s, width, height);

	struct frame *frame = frame_pick(s, width, height);
	if (frame->buffer == NULL || frame->attrs.n_planes < 1)
		return;

	struct wlr_render_pass *pass = wlr_renderer_begin_buffer_pass(s->renderer, frame->buffer, NULL);
	if (pass == NULL)
		return;

	struct wlr_render_rect_options background = {
		.box = {0, 0, width, height},
		.color = {0.0f, 0.0f, 0.0f, 1.0f},
	};
	wlr_render_pass_add_rect(pass, &background);

	xwm_draw(s, pass, width, height);

	if (!wlr_render_pass_submit(pass))
		return;

	server_gpu_sync(s);

	frame->in_flight = true;
	frame->generation = ++s->pool.next_generation;

	out->generation = frame->generation;
	out->fd = fcntl(frame->attrs.fd[0], F_DUPFD_CLOEXEC, 0);
	out->format = frame->attrs.format;
	out->width = width;
	out->height = height;
	out->offset = frame->attrs.offset[0];
	out->stride = frame->attrs.stride[0];
	out->modifier = frame->attrs.modifier;
}

void server_frame_released(struct dynscope *ds, int generation) {
	struct server *s = ds->server;
	if (s == NULL)
		return;
	for (int i = 0; i < s->pool.nframes; i++) {
		if (s->pool.frames[i].generation == generation) {
		s->pool.frames[i].in_flight = false;
			return;
		}
	}
}

void server_pointer_enter(struct dynscope *ds, double host_x, double host_y) {
	struct server *s = ds->server;
	if (s == NULL)
		return;

	struct wlr_surface *surface = NULL;
	double x, y;
	xwm_pick_surface(s, host_x, host_y, &surface, &x, &y);
	dynscope_log_debug("dynscope: server pointer enter host=(%.0f,%.0f) surface=%p res=%u game=(%.1f,%.1f)\n", host_x, host_y, (void *)surface, surface != NULL && surface->resource != NULL ? wl_resource_get_id(surface->resource) : 0, x, y);
	if (surface == NULL)
		return;
	s->pointer_surface = surface;
	s->pointer_x = x;
	s->pointer_y = y;
	wlr_seat_pointer_notify_enter(s->seat, surface, x, y);
	xcursor_refresh(s);
	host_apply_cursor(ds);
}

void server_pointer_motion(struct dynscope *ds, uint32_t time_msec, double host_x, double host_y) {
	struct server *s = ds->server;
	if (s == NULL)
		return;

	struct wlr_surface *surface = NULL;
	double x, y;
	xwm_pick_surface(s, host_x, host_y, &surface, &x, &y);
	s->pointer_x = x;
	s->pointer_y = y;
	if (surface == s->pointer_surface) {
		if (surface != NULL) {
			wlr_seat_pointer_notify_motion(s->seat, time_msec, x, y);
			wlr_seat_pointer_notify_frame(s->seat);
		}
		return;
	}

	if (surface == NULL) {
		s->pointer_surface = NULL;
		s->pointer_x = 0;
		s->pointer_y = 0;
		wlr_seat_pointer_notify_clear_focus(s->seat);
		host_request_frame(ds);
		return;
	}

	s->pointer_surface = surface;
	wlr_seat_pointer_notify_enter(s->seat, surface, x, y);
}

void server_pointer_leave(struct dynscope *ds) {
	struct server *s = ds->server;
	if (s == NULL)
		return;
	s->pointer_surface = NULL;
	wlr_seat_pointer_notify_clear_focus(s->seat);
	host_request_frame(ds);
}

void server_pointer_rel_motion(struct dynscope *ds, uint32_t time_msec, uint64_t time_usec, double dx, double dy) {
	struct server *s = ds->server;
	if (s == NULL || s->pointer_surface == NULL)
		return;

	wlr_relative_pointer_manager_v1_send_relative_motion(s->relative_pointer, s->seat, time_usec, dx, dy, dx, dy);

	struct wlr_pointer_constraint_v1 *constraint = s->active_constraint;
	if (constraint != NULL) {
		if (constraint->type == WLR_POINTER_CONSTRAINT_V1_LOCKED) {
			wlr_seat_pointer_notify_frame(s->seat);
			return;
		}
		if (!pixman_region32_empty(&s->confine)) {
			double sx = s->pointer_x;
			double sy = s->pointer_y;
			double nx, ny;
			if (!wlr_region_confine(&s->confine, sx, sy, sx + dx, sy + dy, &nx, &ny)) {
				wlr_seat_pointer_notify_frame(s->seat);
				return;
			}
			dx = nx - sx;
			dy = ny - sy;
			if (dx == 0.0 && dy == 0.0) {
				wlr_seat_pointer_notify_frame(s->seat);
				return;
			}
		}
	}

	s->pointer_x += dx;
	s->pointer_y += dy;
	if (s->pointer_surface->current.width > 0) {
		double max_x = (double)(s->pointer_surface->current.width - 1);
		double max_y = (double)(s->pointer_surface->current.height - 1);
		if (s->pointer_x < 0.0)
			s->pointer_x = 0.0;
		if (s->pointer_y < 0.0)
			s->pointer_y = 0.0;
		if (s->pointer_x > max_x)
			s->pointer_x = max_x;
		if (s->pointer_y > max_y)
			s->pointer_y = max_y;
	}

	wlr_seat_pointer_notify_motion(s->seat, time_msec, s->pointer_x, s->pointer_y);
	wlr_seat_pointer_notify_frame(s->seat);
}

void server_pointer_button(struct dynscope *ds, uint32_t time_msec, uint32_t button, uint32_t state) {
	struct server *s = ds->server;
	if (s == NULL)
		return;
	dynscope_log_debug("dynscope: pointer button %u state %u on surface %p at (%.1f, %.1f)\n",
		button, state, (void *)s->pointer_surface, s->pointer_x, s->pointer_y);
	if (s->pointer_surface == NULL)
		return;
	if (state == WL_POINTER_BUTTON_STATE_PRESSED)
		xwm_surface_activate(s, s->pointer_surface);
	wlr_seat_pointer_notify_button(s->seat, time_msec, button, (enum wl_pointer_button_state)state);
	wlr_seat_pointer_notify_frame(s->seat);
}

void server_pointer_axis(struct dynscope *ds, uint32_t time_msec, uint32_t orientation, double value, int32_t value_discrete, uint32_t source) {
	struct server *s = ds->server;
	if (s == NULL || s->pointer_surface == NULL)
		return;
	wlr_seat_pointer_notify_axis(s->seat, time_msec, (enum wl_pointer_axis)orientation, value, value_discrete, (enum wl_pointer_axis_source)source, WL_POINTER_AXIS_RELATIVE_DIRECTION_IDENTICAL);
	wlr_seat_pointer_notify_frame(s->seat);
}

static void server_warp_to_constraint_hint(struct server *s) {
	struct wlr_pointer_constraint_v1 *constraint = s->active_constraint;
	if (constraint == NULL || !constraint->current.cursor_hint.enabled)
		return;
	double sx = constraint->current.cursor_hint.x;
	double sy = constraint->current.cursor_hint.y;
	if (s->pointer_x == sx && s->pointer_y == sy)
		return;
	s->pointer_x = sx;
	s->pointer_y = sy;
	wlr_seat_pointer_warp(s->seat, sx, sy);
	dynscope_log_debug("dynscope: warp to cursor hint (%.1f,%.1f)\n", sx, sy);
}

static void server_update_cursor_constraint(struct server *s) {
	struct wlr_pointer_constraint_v1 *constraint = s->active_constraint;
	if (constraint == NULL)
		return;

	if (s->constraint_requires_warp && constraint->surface != NULL) {
		s->constraint_requires_warp = false;

		server_warp_to_constraint_hint(s);

		if (!pixman_region32_contains_point(&constraint->region, floor(s->pointer_x), floor(s->pointer_y), NULL)) {
			int nboxes;
			pixman_box32_t *boxes = pixman_region32_rectangles(&constraint->region, &nboxes);
			if (nboxes > 0) {
				double best_dist = DBL_MAX;
				int best = 0;
				for (int i = 0; i < nboxes; i++) {
					double cx = s->pointer_x;
					double cy = s->pointer_y;
					if (cx < boxes[i].x1)
						cx = boxes[i].x1;
					if (cx > boxes[i].x2)
						cx = boxes[i].x2;
					if (cy < boxes[i].y1)
						cy = boxes[i].y1;
					if (cy > boxes[i].y2)
						cy = boxes[i].y2;
					double dx = cx - s->pointer_x;
					double dy = cy - s->pointer_y;
					double dist = dx * dx + dy * dy;
					if (dist < best_dist) {
						best_dist = dist;
						best = i;
					}
				}
				s->pointer_x = s->pointer_x < boxes[best].x1 ? boxes[best].x1 : (s->pointer_x > boxes[best].x2 ? boxes[best].x2 : s->pointer_x);
				s->pointer_y = s->pointer_y < boxes[best].y1 ? boxes[best].y1 : (s->pointer_y > boxes[best].y2 ? boxes[best].y2 : s->pointer_y);
				wlr_seat_pointer_warp(s->seat, s->pointer_x, s->pointer_y);
			}
		}
	}

	if (constraint->type == WLR_POINTER_CONSTRAINT_V1_CONFINED)
		pixman_region32_copy(&s->confine, &constraint->region);
	else
		pixman_region32_clear(&s->confine);
}

static void server_constrain(struct server *s, struct wlr_pointer_constraint_v1 *constraint) {
	if (s->active_constraint == constraint)
		return;

	if (s->active_constraint != NULL) {
		if (constraint == NULL)
			server_warp_to_constraint_hint(s);
		wlr_pointer_constraint_v1_send_deactivated(s->active_constraint);
		s->active_constraint = NULL;
	}
	pixman_region32_clear(&s->confine);

	if (constraint == NULL) {
		server_update_lock(s);
		return;
	}

	s->active_constraint = constraint;
	s->constraint_requires_warp = true;
	server_update_cursor_constraint(s);
	wlr_pointer_constraint_v1_send_activated(constraint);
	server_update_lock(s);
}

void server_constrain_focused(struct server *s) {
	if (s == NULL || s->constraints == NULL)
		return;
	struct wlr_surface *surface = xwm_focus_surface(s);
	struct wlr_pointer_constraint_v1 *constraint = NULL;
	if (surface != NULL)
		constraint = wlr_pointer_constraints_v1_constraint_for_surface(s->constraints, surface, s->seat);
	server_constrain(s, constraint);
}

void server_keyboard_focus(struct dynscope *ds, bool focused) {
	struct server *s = ds->server;
	if (s == NULL)
		return;
	if (focused)
		server_constrain_focused(s);
	else
		server_constrain(s, NULL);
}

void server_update_lock(struct server *s) {
	if (s == NULL)
		return;
	bool relative = s->cursor_image_empty && s->active_constraint != NULL;
	if (relative == s->host_locked)
		return;
	s->host_locked = relative;
	dynscope_log_debug("dynscope: relative mouse mode %s (cursor_empty=%d constraint=%d)\n", relative ? "on" : "off", s->cursor_image_empty, s->active_constraint != NULL);
	host_set_locked(s->ds, relative);
}

static void handle_constraint_set_region(struct wl_listener *listener, void *data) {
	struct game_constraint *gc = wl_container_of(listener, gc, set_region);
	(void)data;
	struct server *s = gc->server;
	if (s->active_constraint == gc->constraint) {
		s->constraint_requires_warp = true;
		server_update_cursor_constraint(s);
	}
}

static void handle_constraint_destroy(struct wl_listener *listener, void *data) {
	struct game_constraint *gc = wl_container_of(listener, gc, destroy);
	struct server *s = gc->server;
	struct wlr_pointer_constraint_v1 *constraint = gc->constraint;
	(void)data;

	wl_list_remove(&gc->set_region.link);
	wl_list_remove(&gc->destroy.link);
	wl_list_remove(&gc->link);
	free(gc);

	if (s->active_constraint == constraint) {
		server_warp_to_constraint_hint(s);
		s->active_constraint = NULL;
		pixman_region32_clear(&s->confine);
		server_update_lock(s);
	}
}

static void handle_new_constraint(struct wl_listener *listener, void *data) {
	struct server *s = wl_container_of(listener, s, new_constraint);
	struct wlr_pointer_constraint_v1 *constraint = data;

	struct game_constraint *gc = calloc(1, sizeof(*gc));
	if (gc == NULL)
		return;
	gc->server = s;
	gc->constraint = constraint;
	gc->set_region.notify = handle_constraint_set_region;
	wl_signal_add(&constraint->events.set_region, &gc->set_region);
	gc->destroy.notify = handle_constraint_destroy;
	wl_signal_add(&constraint->events.destroy, &gc->destroy);
	wl_list_insert(s->game_constraints.prev, &gc->link);

	if (xwm_focus_surface(s) == constraint->surface)
		server_constrain(s, constraint);
}

void server_keyboard_keymap(struct dynscope *ds, const char *keymap_string) {
	struct server *s = ds->server;
	if (s == NULL || s->xkb_context == NULL)
		return;

	struct xkb_keymap *keymap = xkb_keymap_new_from_string(s->xkb_context, keymap_string, XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
	if (keymap == NULL)
		return;

	wlr_keyboard_set_keymap(&s->keyboard, keymap);
	xkb_keymap_unref(keymap);
}

void server_keyboard_key(struct dynscope *ds, uint32_t key, bool pressed) {
	struct server *s = ds->server;
	if (s == NULL)
		return;
	dynscope_log_debug("dynscope: server key %u %s\n", key, pressed ? "down" : "up");

	struct wlr_keyboard_key_event event = {
		.keycode = key,
		.update_state = true,
		.state = pressed ? WL_KEYBOARD_KEY_STATE_PRESSED : WL_KEYBOARD_KEY_STATE_RELEASED,
	};
	wlr_keyboard_notify_key(&s->keyboard, &event);
	wlr_seat_set_keyboard(s->seat, &s->keyboard);
	wlr_seat_keyboard_notify_key(s->seat, event.time_msec, key, event.state);
}

void server_keyboard_modifiers(struct dynscope *ds, uint32_t depressed, uint32_t latched, uint32_t locked, uint32_t group) {
	struct server *s = ds->server;
	if (s == NULL)
		return;
	wlr_keyboard_notify_modifiers(&s->keyboard, depressed, latched, locked, group);
}

const char *server_display_name(struct dynscope *ds) {
	struct server *s = ds->server;
	return s != NULL && s->xwayland != NULL ? s->xwayland->display_name : NULL;
}

static void handle_xwayland_ready(struct wl_listener *listener, void *data) {
	(void)listener;
	(void)data;
	struct server *s = wl_container_of(listener, s, xwayland_ready);
	clipboard_init(s);
	xcursor_init(s);
	xcursor_refresh(s);
}

static void handle_xwayland_destroy(struct wl_listener *listener, void *data) {
	(void)listener;
	(void)data;
	struct server *s = wl_container_of(listener, s, xwayland_destroy);
	fprintf(stderr, "dynscope: Xwayland exited\n");
	dynscope_close(s->ds);
}

struct server_surface {
	struct server *server;
	struct wlr_surface *wlr;
	struct wl_listener commit;
	struct wl_listener destroy;
};

static void handle_surface_commit(struct wl_listener *listener, void *data) {
	struct server_surface *ss = wl_container_of(listener, ss, commit);
	(void)data;
	host_request_frame(ss->server->ds);
}

static void handle_surface_destroy(struct wl_listener *listener, void *data) {
	struct server_surface *ss = wl_container_of(listener, ss, destroy);
	(void)data;
	wl_list_remove(&ss->commit.link);
	wl_list_remove(&ss->destroy.link);
	free(ss);
}

static void handle_new_surface(struct wl_listener *listener, void *data) {
	struct server *s = wl_container_of(listener, s, new_surface);
	struct wlr_surface *surface = data;
	struct server_surface *ss = calloc(1, sizeof(*ss));
	if (ss == NULL)
		return;
	ss->server = s;
	ss->wlr = surface;
	ss->commit.notify = handle_surface_commit;
	wl_signal_add(&surface->events.commit, &ss->commit);
	ss->destroy.notify = handle_surface_destroy;
	wl_signal_add(&surface->events.destroy, &ss->destroy);
}

struct mode_entry {
	int width;
	int height;
};

static const struct mode_entry common_modes[] = {
	{5120, 1440},
	{3840, 2160},
	{3840, 1600},
	{3440, 1440},
	{2560, 1440},
	{2560, 1080},
	{1920, 1080},
	{1720, 1440},
	{1280, 720},
};

static void handle_output_bind(struct wl_listener *listener, void *data) {
	struct server *s = wl_container_of(listener, s, output_bind);
	struct wlr_output_event_bind *event = data;
	int refresh = s->ds != NULL && s->ds->host_refresh > 0 ? s->ds->host_refresh : 60000;
	for (size_t i = 0; i < sizeof(common_modes) / sizeof(common_modes[0]); i++) {
		if (common_modes[i].width <= s->output->width && common_modes[i].height <= s->output->height) {
			if (common_modes[i].width != s->output->width || common_modes[i].height != s->output->height)
				wl_output_send_mode(event->resource, 0, common_modes[i].width, common_modes[i].height, refresh);
		}
	}
	uint32_t version = wl_resource_get_version(event->resource);
	if (version >= WL_OUTPUT_DONE_SINCE_VERSION)
		wl_output_send_done(event->resource);
}

int server_init(struct dynscope *ds) {
	struct server *s = calloc(1, sizeof(*s));
	if (s == NULL) {
		fprintf(stderr, "dynscope: out of memory\n");
		return -1;
	}
	s->ds = ds;
	ds->server = s;

	enum wlr_log_importance log_importance = dynscope_debug_enabled() ? WLR_DEBUG : WLR_ERROR;
	wlr_log_init(log_importance, NULL);

	s->display = wl_display_create();
	if (s->display == NULL) {
		fprintf(stderr, "dynscope: failed to create Wayland display\n");
		goto fail;
	}
	ds->loop = wl_display_get_event_loop(s->display);

	s->xkb_context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
	if (s->xkb_context == NULL) {
		fprintf(stderr, "dynscope: failed to create xkb context\n");
		goto fail;
	}
	struct xkb_keymap *keymap = xkb_keymap_new_from_names(s->xkb_context, NULL, XKB_KEYMAP_COMPILE_NO_FLAGS);
	if (keymap == NULL) {
		fprintf(stderr, "dynscope: failed to create default keymap\n");
		goto fail;
	}
	wlr_keyboard_init(&s->keyboard, &keyboard_impl, "dynscope");
	if (!wlr_keyboard_set_keymap(&s->keyboard, keymap)) {
		fprintf(stderr, "dynscope: failed to set default keymap\n");
		xkb_keymap_unref(keymap);
		goto fail;
	}
	xkb_keymap_unref(keymap);

	s->backend = wlr_headless_backend_create(ds->loop);
	if (s->backend == NULL) {
		fprintf(stderr, "dynscope: failed to create headless backend\n");
		goto fail;
	}

	s->renderer = wlr_renderer_autocreate(s->backend);
	if (s->renderer == NULL) {
		fprintf(stderr, "dynscope: failed to create renderer\n");
		goto fail;
	}
	if (!wlr_renderer_is_gles2(s->renderer)) {
		fprintf(stderr, "dynscope: GPU renderer (GLES2) is required, falling back to software is not supported\n");
		goto fail;
	}
	s->egl = wlr_gles2_renderer_get_egl(s->renderer);

	if (!wlr_backend_start(s->backend)) {
		fprintf(stderr, "dynscope: failed to start backend\n");
		goto fail;
	}

	s->allocator = wlr_allocator_autocreate(s->backend, s->renderer);
	if (s->allocator == NULL) {
		fprintf(stderr, "dynscope: failed to create allocator\n");
		goto fail;
	}

	if (!wlr_renderer_init_wl_display(s->renderer, s->display)) {
		fprintf(stderr, "dynscope: failed to initialize buffer protocols\n");
		goto fail;
	}

	s->compositor = wlr_compositor_create(s->display, 6, s->renderer);
	if (s->compositor == NULL) {
		fprintf(stderr, "dynscope: failed to create compositor\n");
		goto fail;
	}
	s->new_surface.notify = handle_new_surface;
	wl_signal_add(&s->compositor->events.new_surface, &s->new_surface);
	s->subcompositor = wlr_subcompositor_create(s->display);
	if (wlr_viewporter_create(s->display) == NULL) {
		fprintf(stderr, "dynscope: failed to create viewporter\n");
		goto fail;
	}

	s->seat = wlr_seat_create(s->display, "seat0");
	if (s->seat == NULL) {
		fprintf(stderr, "dynscope: failed to create seat\n");
		goto fail;
	}
	wlr_seat_set_capabilities(s->seat, WL_SEAT_CAPABILITY_POINTER | WL_SEAT_CAPABILITY_KEYBOARD);
	wlr_seat_set_keyboard(s->seat, &s->keyboard);

	s->data_device = wlr_data_device_manager_create(s->display);
	if (wlr_primary_selection_v1_device_manager_create(s->display) == NULL) {
		fprintf(stderr, "dynscope: failed to create primary selection manager\n");
		goto fail;
	}

	s->constraints = wlr_pointer_constraints_v1_create(s->display);
	if (s->constraints == NULL) {
		fprintf(stderr, "dynscope: failed to create pointer constraints manager\n");
		goto fail;
	}
	s->new_constraint.notify = handle_new_constraint;
	wl_signal_add(&s->constraints->events.new_constraint, &s->new_constraint);
	wl_list_init(&s->game_constraints);
	pixman_region32_init(&s->confine);

	s->relative_pointer = wlr_relative_pointer_manager_v1_create(s->display);
	if (s->relative_pointer == NULL) {
		fprintf(stderr, "dynscope: failed to create relative pointer manager\n");
		goto fail;
	}

	int initial_width = ds->host_width > 0 ? ds->host_width : DEFAULT_WIDTH;
	int initial_height = ds->host_height > 0 ? ds->host_height : DEFAULT_HEIGHT;
	int initial_refresh = ds->host_refresh > 0 ? ds->host_refresh : 60000;

	s->output = wlr_headless_add_output(s->backend, initial_width, initial_height);
	if (s->output == NULL) {
		fprintf(stderr, "dynscope: failed to create virtual output\n");
		goto fail;
	}
	wlr_output_set_name(s->output, "dynscope");
	struct wlr_output_state output_state;
	wlr_output_state_init(&output_state);
	wlr_output_state_set_enabled(&output_state, true);
	wlr_output_state_set_custom_mode(&output_state, initial_width, initial_height, initial_refresh);
	if (!wlr_output_commit_state(s->output, &output_state)) {
		wlr_output_state_finish(&output_state);
		fprintf(stderr, "dynscope: failed to commit virtual output\n");
		goto fail;
	}
	wlr_output_state_finish(&output_state);

	s->output_bind.notify = handle_output_bind;
	wl_signal_add(&s->output->events.bind, &s->output_bind);

	s->layout = wlr_output_layout_create(s->display);
	if (s->layout == NULL) {
		fprintf(stderr, "dynscope: failed to create output layout\n");
		goto fail;
	}
	wlr_output_layout_add(s->layout, s->output, 0, 0);

	s->xwayland = wlr_xwayland_create(s->display, s->compositor, false);
	if (s->xwayland == NULL) {
		fprintf(stderr, "dynscope: failed to start Xwayland\n");
		goto fail;
	}
	s->xwayland_destroy.notify = handle_xwayland_destroy;
	wl_signal_add(&s->xwayland->events.destroy, &s->xwayland_destroy);
	s->xwayland_ready.notify = handle_xwayland_ready;
	wl_signal_add(&s->xwayland->events.ready, &s->xwayland_ready);
	wlr_xwayland_set_seat(s->xwayland, s->seat);

	if (xwm_init(s) < 0)
		goto fail;

	return 0;

fail:
	server_finish(ds);
	return -1;
}

void server_finish(struct dynscope *ds) {
	struct server *s = ds->server;
	if (s == NULL)
		return;
	ds->server = NULL;

	xwm_finish(s);
	xcursor_finish(s);
	clipboard_finish(s);
	wl_list_remove(&s->new_surface.link);
	wl_list_remove(&s->new_constraint.link);
	wl_list_remove(&s->output_bind.link);
	if (s->xwayland != NULL) {
		wl_list_remove(&s->xwayland_destroy.link);
		wl_list_remove(&s->xwayland_ready.link);
		wlr_xwayland_destroy(s->xwayland);
	}
	pixman_region32_fini(&s->confine);
	wlr_keyboard_finish(&s->keyboard);
	if (s->xkb_context != NULL)
		xkb_context_unref(s->xkb_context);
	if (s->backend != NULL)
		wlr_backend_destroy(s->backend);
	if (s->display != NULL) {
		wl_display_destroy_clients(s->display);
		wl_display_destroy(s->display);
	}
	free(s);
}

void server_update_output_mode(struct server *s, int width, int height) {
	if (s == NULL || s->output == NULL || width <= 0 || height <= 0)
		return;
	if (s->output->width == width && s->output->height == height)
		return;
	int refresh = s->ds != NULL && s->ds->host_refresh > 0 ? s->ds->host_refresh : 60000;
	struct wlr_output_state state;
	wlr_output_state_init(&state);
	wlr_output_state_set_custom_mode(&state, width, height, refresh);
	if (wlr_output_commit_state(s->output, &state))
		dynscope_log_debug("dynscope: virtual output mode updated to %dx%d\n", width, height);
	wlr_output_state_finish(&state);
}

int server_request_close(struct dynscope *ds) {
	struct server *s = ds->server;
	if (s == NULL)
		return 0;
	return xwm_close_windows(s);
}

