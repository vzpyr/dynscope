#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include <wlr/render/pass.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_linux_drm_syncobj_v1.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/xwayland/xwayland.h>

#include "dynscope.h"
#include "host.h"
#include "server.h"
#include "xwm.h"
#include "xcursor.h"

#define MAX_DRAWN_SURFACES 64

struct xwindow {
	struct xwm *xwm;
	struct wlr_xwayland_surface *xs;
	uint64_t sequence;
	struct wl_listener destroy;
	struct wl_listener map_request;
	struct wl_listener associate;
	struct wl_listener dissociate;
	struct wl_listener request_activate;
	struct wl_listener request_configure;
	struct wl_listener set_title;
	struct wl_list link;
};

struct xwm {
	struct server *server;
	struct wl_listener new_surface;
	struct wl_list windows;
	struct wlr_xwayland_surface *focus;
	uint64_t next_sequence;
	bool closing;
};

static void keyboard_focus(struct xwm *xwm, struct wlr_xwayland_surface *xs) {
	if (xs == NULL || xs->surface == NULL)
		return;
	struct wlr_keyboard *keyboard = &xwm->server->keyboard;
	wlr_xwayland_surface_activate(xs, true);
	wlr_seat_keyboard_notify_enter(xwm->server->seat, xs->surface, keyboard->keycodes, keyboard->num_keycodes, &keyboard->modifiers);
	server_constrain_focused(xwm->server);
}

static void claim_focus(struct xwm *xwm, struct wlr_xwayland_surface *xs) {
	if (xs == NULL || xs->override_redirect)
		return;
	xwm->focus = xs;
	const char *title = xs->title != NULL && xs->title[0] != '\0' ? xs->title : "dynscope";
	host_set_title(xwm->server->ds, title);
	keyboard_focus(xwm, xs);
	dynscope_log_debug("dynscope: focus window %p \"%s\" %ux%u\n", (void *)xs, title, xs->width, xs->height);
}

static struct wlr_xwayland_surface *pick_focus(struct xwm *xwm) {
	struct xwindow *win;
	struct wlr_xwayland_surface *best = NULL;
	uint64_t best_seq = 0;
	wl_list_for_each(win, &xwm->windows, link) {
		struct wlr_xwayland_surface *xs = win->xs;
		if (xs->override_redirect || xs->surface == NULL || !xs->surface->mapped)
			continue;
		if (best == NULL || win->sequence >= best_seq) {
			best = xs;
			best_seq = win->sequence;
		}
	}
	if (best != NULL)
		return best;

	wl_list_for_each_reverse(win, &xwm->windows, link) {
		struct wlr_xwayland_surface *xs = win->xs;
		if (xs->override_redirect || xs->surface == NULL)
			continue;
		return xs;
	}
	return NULL;
}

static struct wlr_xwayland_surface *xwm_focus_window(struct xwm *xwm) {
	if (xwm->focus != NULL && !xwm->focus->override_redirect && xwm->focus->surface != NULL && xwm->focus->surface->mapped)
		return xwm->focus;
	return pick_focus(xwm);
}

static void xwindow_raise(struct xwm *xwm, struct xwindow *win) {
	wl_list_remove(&win->link);
	wl_list_insert(xwm->windows.prev, &win->link);
}

static struct xwindow *xwindow_find(struct xwm *xwm, struct wlr_xwayland_surface *xs) {
	struct xwindow *win;
	wl_list_for_each(win, &xwm->windows, link) {
		if (win->xs == xs)
			return win;
	}
	return NULL;
}

static void handle_destroy(struct wl_listener *listener, void *data) {
	struct xwindow *win = wl_container_of(listener, win, destroy);
	struct xwm *xwm = win->xwm;
	struct wlr_xwayland_surface *xs = data;
	(void)xs;

	dynscope_log_debug("dynscope: window %p destroyed\n", (void *)win->xs);

	wl_list_remove(&win->destroy.link);
	wl_list_remove(&win->map_request.link);
	wl_list_remove(&win->associate.link);
	wl_list_remove(&win->dissociate.link);
	wl_list_remove(&win->request_activate.link);
	wl_list_remove(&win->request_configure.link);
	wl_list_remove(&win->set_title.link);
	wl_list_remove(&win->link);
	free(win);

	if (xwm->focus == xs) {
		xwm->focus = pick_focus(xwm);
		if (xwm->focus != NULL)
			claim_focus(xwm, xwm->focus);
		else
			server_constrain_focused(xwm->server);
	}

	if (xwm->closing) {
		bool any_mapped = false;
		wl_list_for_each(win, &xwm->windows, link) {
			if (!win->xs->override_redirect && win->xs->surface != NULL && win->xs->surface->mapped) {
				any_mapped = true;
				break;
			}
		}
		if (!any_mapped)
			dynscope_close(xwm->server->ds);
	}
}

static void handle_map_request(struct wl_listener *listener, void *data) {
	struct xwindow *win = wl_container_of(listener, win, map_request);
	struct wlr_xwayland_surface *xs = win->xs;
	(void)data;
	struct xwm *xwm = win->xwm;

	if (xs->override_redirect)
		return;

	int sw = xwm->server->output != NULL ? xwm->server->output->width : DEFAULT_WIDTH;
	int sh = xwm->server->output != NULL ? xwm->server->output->height : DEFAULT_HEIGHT;

	uint16_t w = xs->width > 0 ? xs->width : (uint16_t)sw;
	uint16_t h = xs->height > 0 ? xs->height : (uint16_t)sh;
	int16_t x = xs->x;
	int16_t y = xs->y;

	if (xs->fullscreen || (xs->width == 0 && xs->height == 0) || (w >= (uint16_t)sw && h >= (uint16_t)sh)) {
		x = 0;
		y = 0;
		w = (uint16_t)sw;
		h = (uint16_t)sh;
	} else if (x == 0 && y == 0) {
		x = (int16_t)((sw - (int)w) / 2);
		y = (int16_t)((sh - (int)h) / 2);
	}

	wlr_xwayland_surface_configure(xs, x, y, w, h);
	dynscope_log_debug("dynscope: window %p map request \"%s\" configure %ux%u at %d,%d\n", (void *)xs, xs->title != NULL ? xs->title : "", w, h, x, y);

	win->sequence = ++xwm->next_sequence;
	xwindow_raise(xwm, win);
	if (xwm->windows.next != xwm->windows.prev)
		wlr_xwayland_surface_restack(xs, NULL, XCB_STACK_MODE_ABOVE);
	claim_focus(xwm, xs);

	if (w >= (uint16_t)sw && h >= (uint16_t)sh)
		host_set_initial_size(xwm->server->ds, sw, sh);
	else
		host_set_initial_size(xwm->server->ds, (int)w, (int)h);
}

static void handle_associate(struct wl_listener *listener, void *data) {
	struct xwindow *win = wl_container_of(listener, win, associate);
	struct wlr_xwayland_surface *xs = win->xs;
	struct xwm *xwm = win->xwm;

	(void)data;
	dynscope_log_debug("dynscope: window %p associate\n", (void *)xs);
	if (xwm->focus == NULL || xwm->focus == xs)
		claim_focus(xwm, xs);
}

static void handle_dissociate(struct wl_listener *listener, void *data) {
	struct xwindow *win = wl_container_of(listener, win, dissociate);
	struct xwm *xwm = win->xwm;
	(void)data;

	dynscope_log_debug("dynscope: window %p dissociate\n", (void *)win->xs);
	if (xwm->focus == win->xs) {
		xwm->focus = pick_focus(xwm);
		if (xwm->focus != NULL)
			claim_focus(xwm, xwm->focus);
		else
			server_constrain_focused(xwm->server);
	}
}

static void handle_request_activate(struct wl_listener *listener, void *data) {
	struct xwindow *win = wl_container_of(listener, win, request_activate);
	struct wlr_xwayland_surface *xs = win->xs;
	(void)data;
	dynscope_log_debug("dynscope: window %p request_activate\n", (void *)xs);
	win->sequence = ++win->xwm->next_sequence;
	xwindow_raise(win->xwm, win);
	if (win->xwm->windows.next != win->xwm->windows.prev)
		wlr_xwayland_surface_restack(xs, NULL, XCB_STACK_MODE_ABOVE);
	claim_focus(win->xwm, xs);
}

static void handle_request_configure(struct wl_listener *listener, void *data) {
	struct xwindow *win = wl_container_of(listener, win, request_configure);
	struct wlr_xwayland_surface_configure_event *event = data;
	struct xwm *xwm = win->xwm;

	wlr_xwayland_surface_configure(win->xs, event->x, event->y, event->width, event->height);
	dynscope_log_debug("dynscope: window %p configure request %ux%u at %d,%d\n", (void *)win->xs, event->width, event->height, event->x, event->y);

	if (win->xs == xwm->focus && win->xs->fullscreen && event->width > 0 && event->height > 0)
		server_update_output_mode(xwm->server, (int)event->width, (int)event->height);
}

static void handle_set_title(struct wl_listener *listener, void *data) {
	struct xwindow *win = wl_container_of(listener, win, set_title);
	(void)data;
	if (win->xwm->focus == win->xs) {
		const char *title = win->xs->title != NULL && win->xs->title[0] != '\0' ? win->xs->title : "dynscope";
		host_set_title(win->xwm->server->ds, title);
	}
}

static void handle_new_surface(struct wl_listener *listener, void *data) {
	struct xwm *xwm = wl_container_of(listener, xwm, new_surface);
	struct wlr_xwayland_surface *xs = data;

	dynscope_log_debug("dynscope: new xwayland surface %p title=\"%s\" class=\"%s\" override_redirect=%d\n",
		(void *)xs, xs->title != NULL ? xs->title : "", xs->class != NULL ? xs->class : "", xs->override_redirect);

	struct xwindow *win = calloc(1, sizeof(*win));
	if (win == NULL)
		return;
	win->xwm = xwm;
	win->xs = xs;
	win->sequence = ++xwm->next_sequence;
	win->destroy.notify = handle_destroy;
	wl_signal_add(&xs->events.destroy, &win->destroy);
	win->map_request.notify = handle_map_request;
	wl_signal_add(&xs->events.map_request, &win->map_request);
	win->associate.notify = handle_associate;
	wl_signal_add(&xs->events.associate, &win->associate);
	win->dissociate.notify = handle_dissociate;
	wl_signal_add(&xs->events.dissociate, &win->dissociate);
	win->request_activate.notify = handle_request_activate;
	wl_signal_add(&xs->events.request_activate, &win->request_activate);
	win->request_configure.notify = handle_request_configure;
	wl_signal_add(&xs->events.request_configure, &win->request_configure);
	win->set_title.notify = handle_set_title;
	wl_signal_add(&xs->events.set_title, &win->set_title);
	wl_list_insert(xwm->windows.prev, &win->link);
}

static void draw_surface_tree(struct wlr_render_pass *pass, struct wlr_surface *surface, double base_x, double base_y, double scale, struct wlr_surface **drawn, int *ndrawn) {
	if (surface == NULL)
		return;

	struct wlr_subsurface *sub;
	wl_list_for_each(sub, &surface->current.subsurfaces_below, current.link) {
		double sx = base_x + (double)sub->current.x * scale;
		double sy = base_y + (double)sub->current.y * scale;
		draw_surface_tree(pass, sub->surface, sx, sy, scale, drawn, ndrawn);
	}

	struct wlr_texture *texture = wlr_surface_get_texture(surface);
	if (texture != NULL) {
		struct wlr_fbox src;
		wlr_surface_get_buffer_source_box(surface, &src);
		double w = surface->current.width > 0 ? (double)surface->current.width : src.width;
		double h = surface->current.height > 0 ? (double)surface->current.height : src.height;
		int dw = (int)(w * scale + 0.5);
		int dh = (int)(h * scale + 0.5);
		if (dw > 0 && dh > 0) {
			struct wlr_linux_drm_syncobj_surface_v1_state *sync_state = wlr_linux_drm_syncobj_v1_get_surface_state(surface);
			struct wlr_render_texture_options options = {
				.texture = texture,
				.src_box = src,
				.dst_box = {
					(int)(base_x + 0.5),
					(int)(base_y + 0.5),
					dw,
					dh,
				},
				.wait_timeline = sync_state != NULL ? sync_state->acquire_timeline : NULL,
				.wait_point = sync_state != NULL ? sync_state->acquire_point : 0,
			};
			wlr_render_pass_add_texture(pass, &options);
		}
	}
	if (*ndrawn < MAX_DRAWN_SURFACES) {
		bool exists = false;
		for (int i = 0; i < *ndrawn; i++) {
			if (drawn[i] == surface) {
				exists = true;
				break;
			}
		}
		if (!exists)
			drawn[(*ndrawn)++] = surface;
	}

	wl_list_for_each(sub, &surface->current.subsurfaces_above, current.link) {
		double sx = base_x + (double)sub->current.x * scale;
		double sy = base_y + (double)sub->current.y * scale;
		draw_surface_tree(pass, sub->surface, sx, sy, scale, drawn, ndrawn);
	}
}

struct wlr_surface *xwm_focus_surface(struct server *server) {
	struct xwm *xwm = server->xwm;
	if (xwm == NULL)
		return NULL;
	struct wlr_xwayland_surface *focus = xwm_focus_window(xwm);
	if (focus != NULL && focus->surface != NULL)
		return focus->surface;
	return NULL;
}

void xwm_game_size(struct server *server, int *width, int *height) {
	*width = 0;
	*height = 0;
	struct xwm *xwm = server->xwm;
	if (xwm == NULL)
		return;

	struct wlr_xwayland_surface *focus = xwm_focus_window(xwm);
	if (focus == NULL || focus->surface == NULL || !focus->surface->mapped)
		focus = pick_focus(xwm);
	if (focus == NULL)
		return;

	int w = 0;
	int h = 0;
	if (focus->surface != NULL && wlr_surface_has_buffer(focus->surface)) {
		struct wlr_fbox src;
		wlr_surface_get_buffer_source_box(focus->surface, &src);
		w = (int)src.width;
		h = (int)src.height;
	}
	if (w <= 0 || h <= 0) {
		w = (int)focus->width;
		h = (int)focus->height;
	}
	if (w <= 0 || h <= 0) {
		w = server->ds != NULL && server->ds->host_width > 0 ? server->ds->host_width : DEFAULT_WIDTH;
		h = server->ds != NULL && server->ds->host_height > 0 ? server->ds->host_height : DEFAULT_HEIGHT;
	}
	*width = w;
	*height = h;
}

void xwm_surface_activate(struct server *server, struct wlr_surface *surface) {
	struct xwm *xwm = server->xwm;
	if (xwm == NULL || surface == NULL)
		return;

	struct wlr_surface *root_surface = wlr_surface_get_root_surface(surface);
	struct wlr_xwayland_surface *xs = wlr_xwayland_surface_try_from_wlr_surface(root_surface != NULL ? root_surface : surface);
	if (xs == NULL || xs->override_redirect)
		return;

	struct xwindow *win = xwindow_find(xwm, xs);
	if (win != NULL) {
		win->sequence = ++xwm->next_sequence;
		xwindow_raise(xwm, win);
		if (xwm->windows.next != xwm->windows.prev)
			wlr_xwayland_surface_restack(xs, NULL, XCB_STACK_MODE_ABOVE);
	}
	claim_focus(xwm, xs);
}

int xwm_close_windows(struct server *server) {
	struct xwm *xwm = server->xwm;
	if (xwm == NULL)
		return 0;

	xwm->closing = true;
	int count = 0;
	struct xwindow *win;
	wl_list_for_each(win, &xwm->windows, link) {
		struct wlr_xwayland_surface *xs = win->xs;
		if (xs->override_redirect || xs->surface == NULL || !xs->surface->mapped)
			continue;
		wlr_xwayland_surface_close(xs);
		count++;
	}
	return count;
}

void xwm_pick_surface(struct server *server, double host_x, double host_y, struct wlr_surface **surface, double *out_x, double *out_y) {
	struct xwm *xwm = server->xwm;
	*surface = NULL;
	*out_x = 0;
	*out_y = 0;
	if (xwm == NULL)
		return;

	struct fit *fit = &server->fit;
	if (fit->scale <= 0.0 || fit->w <= 0 || fit->h <= 0)
		return;

	struct wlr_xwayland_surface *focus = xwm_focus_window(xwm);
	if (focus == NULL || focus->surface == NULL || !focus->surface->mapped)
		focus = pick_focus(xwm);
	if (focus == NULL || focus->surface == NULL || !focus->surface->mapped)
		return;

	struct xwindow *win;
	wl_list_for_each_reverse(win, &xwm->windows, link) {
		struct wlr_xwayland_surface *xs = win->xs;
		if (!xs->override_redirect || xs->surface == NULL || !xs->surface->mapped)
			continue;
		double rx = (double)(xs->x - focus->x);
		double ry = (double)(xs->y - focus->y);
		double dst_x = fit->x + rx * fit->scale;
		double dst_y = fit->y + ry * fit->scale;
		double pw = xs->width > 0 ? (double)xs->width : (double)xs->surface->current.width;
		double ph = xs->height > 0 ? (double)xs->height : (double)xs->surface->current.height;
		double dst_w = pw * fit->scale;
		double dst_h = ph * fit->scale;
		if (host_x >= dst_x && host_y >= dst_y && host_x < dst_x + dst_w && host_y < dst_y + dst_h) {
			double local_x = (host_x - dst_x) / fit->scale;
			double local_y = (host_y - dst_y) / fit->scale;
			struct wlr_surface *sub = wlr_surface_surface_at(xs->surface, local_x, local_y, out_x, out_y);
			*surface = sub != NULL ? sub : xs->surface;
			if (sub == NULL) {
				*out_x = local_x;
				*out_y = local_y;
			}
			return;
		}
	}

	double gx = (host_x - fit->x) / fit->scale;
	double gy = (host_y - fit->y) / fit->scale;

	int gw = 0;
	int gh = 0;
	xwm_game_size(server, &gw, &gh);
	if (gw > 0 && gh > 0) {
		if (gx < 0.0)
			gx = 0.0;
		if (gy < 0.0)
			gy = 0.0;
		if (gx >= (double)gw)
			gx = (double)gw - 0.01;
		if (gy >= (double)gh)
			gy = (double)gh - 0.01;
	}

	struct wlr_surface *sub = wlr_surface_surface_at(focus->surface, gx, gy, out_x, out_y);
	*surface = sub != NULL ? sub : focus->surface;
	if (sub == NULL) {
		*out_x = gx;
		*out_y = gy;
	}
}

int xwm_init(struct server *server) {
	struct xwm *xwm = calloc(1, sizeof(*xwm));
	if (xwm == NULL) {
		fprintf(stderr, "dynscope: out of memory\n");
		return -1;
	}
	xwm->server = server;
	wl_list_init(&xwm->windows);
	xwm->new_surface.notify = handle_new_surface;
	wl_signal_add(&server->xwayland->events.new_surface, &xwm->new_surface);
	server->xwm = xwm;
	return 0;
}

void xwm_finish(struct server *server) {
	struct xwm *xwm = server->xwm;
	if (xwm == NULL)
		return;
	server->xwm = NULL;
	wl_list_remove(&xwm->new_surface.link);
	struct xwindow *win;
	struct xwindow *tmp;
	wl_list_for_each_safe(win, tmp, &xwm->windows, link) {
		wl_list_remove(&win->destroy.link);
		wl_list_remove(&win->map_request.link);
		wl_list_remove(&win->associate.link);
		wl_list_remove(&win->dissociate.link);
		wl_list_remove(&win->request_activate.link);
		wl_list_remove(&win->request_configure.link);
		wl_list_remove(&win->set_title.link);
		free(win);
	}
	free(xwm);
}

void xwm_draw(struct server *server, struct wlr_render_pass *pass, int width, int height) {
	(void)width;
	(void)height;
	struct xwm *xwm = server->xwm;
	if (xwm == NULL)
		return;

	struct fit *fit = &server->fit;
	if (fit->scale <= 0.0 || fit->w <= 0 || fit->h <= 0)
		return;

	struct wlr_xwayland_surface *focus = xwm_focus_window(xwm);
	if (focus == NULL || focus->surface == NULL || !focus->surface->mapped)
		focus = pick_focus(xwm);
	if (focus == NULL || focus->surface == NULL || !focus->surface->mapped)
		return;

	static struct wlr_xwayland_surface *last_logged_focus = NULL;
	static int last_fit_w = 0;
	static int last_fit_h = 0;
	if (focus != last_logged_focus || fit->w != last_fit_w || fit->h != last_fit_h) {
		last_logged_focus = focus;
		last_fit_w = fit->w;
		last_fit_h = fit->h;
		dynscope_log_debug("dynscope: render active %p \"%s\" %ux%u fit at (%.1f, %.1f) %dx%d scale %.3f\n",
			(void *)focus, focus->title != NULL ? focus->title : "", focus->width, focus->height, fit->x, fit->y, fit->w, fit->h, fit->scale);
	}

	struct wlr_surface *drawn[MAX_DRAWN_SURFACES];
	int ndrawn = 0;

	draw_surface_tree(pass, focus->surface, fit->x, fit->y, fit->scale, drawn, &ndrawn);

	struct xwindow *win;
	wl_list_for_each(win, &xwm->windows, link) {
		struct wlr_xwayland_surface *xs = win->xs;
		if (!xs->override_redirect || xs->surface == NULL || !xs->surface->mapped)
			continue;
		double rx = (double)(xs->x - focus->x);
		double ry = (double)(xs->y - focus->y);
		double dst_x = fit->x + rx * fit->scale;
		double dst_y = fit->y + ry * fit->scale;
		draw_surface_tree(pass, xs->surface, dst_x, dst_y, fit->scale, drawn, &ndrawn);
	}

	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	for (int i = 0; i < ndrawn; i++)
		wlr_surface_send_frame_done(drawn[i], &now);
}
