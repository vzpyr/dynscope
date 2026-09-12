#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <wayland-client.h>

#include "dynscope.h"
#include "host.h"
#include "server.h"
#include "xdg-shell-client-protocol.h"
#include "linux-dmabuf-v1-client-protocol.h"
#include "cursor-shape-v1-client-protocol.h"
#include "pointer-constraints-unstable-v1-client-protocol.h"
#include "relative-pointer-unstable-v1-client-protocol.h"
#include "primary-selection-unstable-v1-client-protocol.h"
#include "fractional-scale-v1-client-protocol.h"
#include "viewporter-client-protocol.h"
#include "xdg-decoration-unstable-v1-client-protocol.h"

#define DEFAULT_WIDTH 1280
#define DEFAULT_HEIGHT 720
#define MAX_OUTSTANDING 4

struct outstanding {
	int generation;
	struct wl_buffer *buffer;
};

struct host_cursor {
	uint32_t *pixels;
	int width;
	int height;
	int hotspot_x;
	int hotspot_y;
	enum { CURSOR_DEFAULT, CURSOR_PIXELS, CURSOR_HIDDEN } mode;
};

struct pending_axis {
	double value;
	int32_t discrete;
	bool have;
	uint32_t source;
	uint32_t time;
	uint32_t axis;
};

struct host_read {
	struct host *host;
	bool primary;
	int fd;
	struct wl_data_offer *offer;
	struct zwp_primary_selection_offer_v1 *primary_offer;
	char *buf;
	size_t len;
	size_t cap;
	bool dead;
	struct wl_event_source *src;
};

struct host {
	struct wl_display *display;
	struct wl_registry *registry;
	struct wl_compositor *compositor;
	struct wl_shm *shm;
	struct wl_seat *seat;
	struct wl_pointer *pointer;
	struct wp_cursor_shape_manager_v1 *cursor_shape_manager;
	struct wp_cursor_shape_device_v1 *cursor_shape_device;
	struct zwp_pointer_constraints_v1 *pointer_constraints;
	struct zwp_relative_pointer_manager_v1 *relative_pointer_manager;
	struct zwp_locked_pointer_v1 *locked_pointer;
	struct zwp_relative_pointer_v1 *relative_pointer;
	bool pointer_locked;
	bool keyboard_entered;
	struct zwp_linux_dmabuf_v1 *dmabuf;
	struct xdg_wm_base *wm_base;
	struct wl_surface *surface;
	struct xdg_surface *xdg_surface;
	struct xdg_toplevel *toplevel;
	struct wl_surface *cursor_surface;
	struct wl_shm_pool *cursor_pool;
	struct wl_buffer *cursor_buffer;

	struct wp_fractional_scale_manager_v1 *fractional_scale_manager;
	struct wp_fractional_scale_v1 *fractional_scale;
	struct wp_viewporter *viewporter;
	struct wp_viewport *viewport;
	struct wp_viewport *cursor_viewport;
	struct zxdg_decoration_manager_v1 *decoration_manager;
	struct zxdg_toplevel_decoration_v1 *toplevel_decoration;
	uint32_t scale_120;
	bool user_resized;
	bool frame_pending;
	struct wl_event_source *idle_source;

	struct wl_keyboard *keyboard;
	struct wl_data_device_manager *data_device_manager;
	struct wl_data_device *data_device;
	struct wl_data_source *clipboard_source;
	struct zwp_primary_selection_device_manager_v1 *primary_manager;
	struct zwp_primary_selection_device_v1 *primary_device;
	struct zwp_primary_selection_source_v1 *primary_source;
	char *clip_data;
	size_t clip_len;
	char *primary_data;
	size_t primary_len;
	struct host_read read_clip;
	struct host_read read_primary;
	struct host_offer *pending_clip_offer;
	struct host_offer *pending_primary_offer;
	uint32_t keyboard_enter_serial;
	uint32_t last_event_serial;
	struct wl_array outstanding;
	struct host_cursor cursor;
	struct pending_axis pending_axis;
	uint32_t pointer_enter_serial;

	struct wl_event_source *fd_src;
	struct wl_event_source *flush_src;
	struct wl_event_source *close_timer;
	struct wl_callback *frame;

	int width;
	int height;
	int pending_width;
	int pending_height;
	bool configured;

	struct dynscope *ds;
};

static const struct wl_registry_listener registry_listener;
static const struct xdg_wm_base_listener wm_base_listener;
static const struct xdg_toplevel_listener toplevel_listener;
static const struct xdg_surface_listener xdg_surface_listener;
static const struct wl_callback_listener frame_listener;
static const struct wl_seat_listener seat_listener;
static const struct wl_keyboard_listener keyboard_listener;
static const struct wl_pointer_listener pointer_listener;
static const struct wl_buffer_listener buffer_listener;
static const struct zwp_locked_pointer_v1_listener locked_pointer_listener;
static const struct zwp_relative_pointer_v1_listener relative_pointer_listener;
static const struct wl_data_device_listener data_device_listener;
static const struct wl_data_offer_listener data_offer_listener;
static const struct wl_data_source_listener data_source_listener;
static const struct zwp_primary_selection_device_v1_listener primary_device_listener;
static const struct zwp_primary_selection_offer_v1_listener primary_offer_listener;
static const struct zwp_primary_selection_source_v1_listener primary_source_listener;
static const struct wp_fractional_scale_v1_listener fractional_scale_listener;
static const struct zxdg_toplevel_decoration_v1_listener decoration_listener;

static void host_apply_cursor_impl(struct host *host);

static void registry_handle_global(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version) {
	struct host *host = data;

	if (strcmp(interface, wl_compositor_interface.name) == 0)
		host->compositor = wl_registry_bind(registry, name, &wl_compositor_interface, version < (uint32_t)wl_compositor_interface.version ? version : (uint32_t)wl_compositor_interface.version);
	else if (strcmp(interface, wl_shm_interface.name) == 0)
		host->shm = wl_registry_bind(registry, name, &wl_shm_interface, version < 1u ? version : 1u);
	else if (strcmp(interface, wp_cursor_shape_manager_v1_interface.name) == 0)
		host->cursor_shape_manager = wl_registry_bind(registry, name, &wp_cursor_shape_manager_v1_interface, version < 1u ? version : 1u);
	else if (strcmp(interface, wl_seat_interface.name) == 0)
		host->seat = wl_registry_bind(registry, name, &wl_seat_interface, version < 8u ? version : 8u);
	else if (strcmp(interface, xdg_wm_base_interface.name) == 0)
		host->wm_base = wl_registry_bind(registry, name, &xdg_wm_base_interface, version < (uint32_t)xdg_wm_base_interface.version ? version : (uint32_t)xdg_wm_base_interface.version);
	else if (strcmp(interface, zwp_linux_dmabuf_v1_interface.name) == 0)
		host->dmabuf = wl_registry_bind(registry, name, &zwp_linux_dmabuf_v1_interface, version < 3u ? version : 3u);
	else if (strcmp(interface, zwp_pointer_constraints_v1_interface.name) == 0)
		host->pointer_constraints = wl_registry_bind(registry, name, &zwp_pointer_constraints_v1_interface, version < 1u ? version : 1u);
	else if (strcmp(interface, zwp_relative_pointer_manager_v1_interface.name) == 0)
		host->relative_pointer_manager = wl_registry_bind(registry, name, &zwp_relative_pointer_manager_v1_interface, version < 1u ? version : 1u);
	else if (strcmp(interface, wl_data_device_manager_interface.name) == 0)
		host->data_device_manager = wl_registry_bind(registry, name, &wl_data_device_manager_interface, version < (uint32_t)wl_data_device_manager_interface.version ? version : (uint32_t)wl_data_device_manager_interface.version);
	else if (strcmp(interface, zwp_primary_selection_device_manager_v1_interface.name) == 0)
		host->primary_manager = wl_registry_bind(registry, name, &zwp_primary_selection_device_manager_v1_interface, version < 1u ? version : 1u);
	else if (strcmp(interface, wp_fractional_scale_manager_v1_interface.name) == 0)
		host->fractional_scale_manager = wl_registry_bind(registry, name, &wp_fractional_scale_manager_v1_interface, 1);
	else if (strcmp(interface, wp_viewporter_interface.name) == 0)
		host->viewporter = wl_registry_bind(registry, name, &wp_viewporter_interface, 1);
	else if (strcmp(interface, zxdg_decoration_manager_v1_interface.name) == 0)
		host->decoration_manager = wl_registry_bind(registry, name, &zxdg_decoration_manager_v1_interface, 1);
}

static void registry_handle_global_remove(void *data, struct wl_registry *registry, uint32_t name) {
	(void)data;
	(void)registry;
	(void)name;
}

static const struct wl_registry_listener registry_listener = {
	.global = registry_handle_global,
	.global_remove = registry_handle_global_remove,
};

static void fractional_scale_handle_preferred_scale(void *data, struct wp_fractional_scale_v1 *wp_fractional_scale_v1, uint32_t scale) {
	(void)wp_fractional_scale_v1;
	struct host *host = data;
	host->scale_120 = scale;
	dynscope_log_debug("dynscope: host preferred fractional scale %u/120 (%.2f)\n", scale, (double)scale / 120.0);
	host_request_frame(host->ds);
}

static const struct wp_fractional_scale_v1_listener fractional_scale_listener = {
	.preferred_scale = fractional_scale_handle_preferred_scale,
};

static void decoration_handle_configure(void *data, struct zxdg_toplevel_decoration_v1 *decoration, uint32_t mode) {
	(void)data;
	(void)decoration;
	(void)mode;
}

static const struct zxdg_toplevel_decoration_v1_listener decoration_listener = {
	.configure = decoration_handle_configure,
};

static void wm_base_handle_ping(void *data, struct xdg_wm_base *xdg_wm_base, uint32_t serial) {
	(void)data;
	xdg_wm_base_pong(xdg_wm_base, serial);
}

static const struct xdg_wm_base_listener wm_base_listener = {
	.ping = wm_base_handle_ping,
};

static void toplevel_handle_configure(void *data, struct xdg_toplevel *toplevel, int32_t width, int32_t height, struct wl_array *states) {
	struct host *host = data;
	(void)toplevel;
	(void)states;
	if (width > 0 && height > 0)
		host->user_resized = true;
	host->pending_width = width;
	host->pending_height = height;
}

static int handle_close_timeout(void *data) {
	struct host *host = data;
	host->close_timer = NULL;
	dynscope_close(host->ds);
	return 0;
}

static void toplevel_handle_close(void *data, struct xdg_toplevel *toplevel) {
	struct host *host = data;
	(void)toplevel;
	if (host->close_timer != NULL)
		return;

	int closed = server_request_close(host->ds);
	if (closed <= 0) {
		dynscope_close(host->ds);
		return;
	}

	host->close_timer = wl_event_loop_add_timer(host->ds->loop, handle_close_timeout, host);
	if (host->close_timer != NULL)
		wl_event_source_timer_update(host->close_timer, 1500);
}


static void toplevel_handle_configure_bounds(void *data, struct xdg_toplevel *toplevel, int32_t width, int32_t height) {
	(void)data;
	(void)toplevel;
	(void)width;
	(void)height;
}

static void toplevel_handle_wm_capabilities(void *data, struct xdg_toplevel *toplevel, struct wl_array *capabilities) {
	(void)data;
	(void)toplevel;
	(void)capabilities;
}

static const struct xdg_toplevel_listener toplevel_listener = {
	.configure = toplevel_handle_configure,
	.close = toplevel_handle_close,
	.configure_bounds = toplevel_handle_configure_bounds,
	.wm_capabilities = toplevel_handle_wm_capabilities,
};

static void buffer_handle_release(void *data, struct wl_buffer *buffer) {
	struct host *host = data;

	struct outstanding *outstanding;
	wl_array_for_each(outstanding, &host->outstanding) {
		if (outstanding->buffer == buffer) {
			server_frame_released(host->ds, outstanding->generation);
			break;
		}
	}
	wl_buffer_destroy(buffer);

	struct outstanding kept[4];
	size_t nkept = 0;
	wl_array_for_each(outstanding, &host->outstanding) {
		if (outstanding->buffer != buffer && nkept < MAX_OUTSTANDING)
			kept[nkept++] = *outstanding;
	}
	wl_array_release(&host->outstanding);
	wl_array_init(&host->outstanding);
	for (size_t i = 0; i < nkept; i++) {
		struct outstanding *entry = wl_array_add(&host->outstanding, sizeof(entry[i]));
		*entry = kept[i];
	}
}

static const struct wl_buffer_listener buffer_listener = {
	.release = buffer_handle_release,
};

static void host_present(struct host *host) {
	if (host->dmabuf == NULL || host->width <= 0 || host->height <= 0)
		return;

	if (host->idle_source != NULL) {
		wl_event_source_remove(host->idle_source);
		host->idle_source = NULL;
	}

	if (host->frame != NULL) {
		host->frame_pending = true;
		return;
	}

	double scale = host_get_scale(host->ds);
	int buf_w = host->width;
	int buf_h = host->height;
	if (host->viewport != NULL && scale > 0.0) {
		buf_w = (int)((double)host->width * scale + 0.5);
		buf_h = (int)((double)host->height * scale + 0.5);
	}

	struct frame_info info;
	server_present(host->ds, buf_w, buf_h, &info);
	if (info.fd < 0)
		return;

	struct zwp_linux_buffer_params_v1 *params = zwp_linux_dmabuf_v1_create_params(host->dmabuf);
	zwp_linux_buffer_params_v1_add(params, info.fd, 0, info.offset, info.stride, (uint32_t)(info.modifier >> 32), (uint32_t)info.modifier);
	close(info.fd);
	struct wl_buffer *buffer = zwp_linux_buffer_params_v1_create_immed(params, info.width, info.height, info.format, 0);
	zwp_linux_buffer_params_v1_destroy(params);
	wl_buffer_add_listener(buffer, &buffer_listener, host);

	struct outstanding *outstanding = wl_array_add(&host->outstanding, sizeof(*outstanding));
	if (outstanding != NULL) {
		outstanding->generation = info.generation;
		outstanding->buffer = buffer;
	} else {
		wl_buffer_destroy(buffer);
		return;
	}

	host->frame = wl_surface_frame(host->surface);
	wl_callback_add_listener(host->frame, &frame_listener, host);

	wl_surface_attach(host->surface, buffer, 0, 0);
	wl_surface_damage(host->surface, 0, 0, INT32_MAX, INT32_MAX);
	if (host->viewport != NULL)
		wp_viewport_set_destination(host->viewport, host->width, host->height);
	wl_surface_commit(host->surface);
}

static void frame_handle_done(void *data, struct wl_callback *callback, uint32_t msecs) {
	struct host *host = data;
	(void)msecs;

	wl_callback_destroy(callback);
	host->frame = NULL;

	if (!host->ds->running)
		return;

	if (host->frame_pending) {
		host->frame_pending = false;
		host_present(host);
	}
}

static const struct wl_callback_listener frame_listener = {
	.done = frame_handle_done,
};

static void xdg_surface_handle_configure(void *data, struct xdg_surface *xdg_surface, uint32_t serial) {
	struct host *host = data;
	xdg_surface_ack_configure(xdg_surface, serial);

	dynscope_log_debug("dynscope: host configure pending=%dx%d\n", host->pending_width, host->pending_height);

	if (!host->configured) {
		host->configured = true;
		host->width = host->pending_width > 0 ? host->pending_width : DEFAULT_WIDTH;
		host->height = host->pending_height > 0 ? host->pending_height : DEFAULT_HEIGHT;
	} else {
		if (host->pending_width > 0)
			host->width = host->pending_width;
		if (host->pending_height > 0)
			host->height = host->pending_height;
	}

	host_request_frame(host->ds);
}

static const struct xdg_surface_listener xdg_surface_listener = {
	.configure = xdg_surface_handle_configure,
};

static void seat_handle_capabilities(void *data, struct wl_seat *seat, uint32_t capabilities) {
	struct host *host = data;

	if ((capabilities & WL_SEAT_CAPABILITY_KEYBOARD) != 0 && host->keyboard == NULL) {
		host->keyboard = wl_seat_get_keyboard(seat);
		wl_keyboard_add_listener(host->keyboard, &keyboard_listener, host);
	} else if ((capabilities & WL_SEAT_CAPABILITY_KEYBOARD) == 0 && host->keyboard != NULL) {
		wl_keyboard_release(host->keyboard);
		host->keyboard = NULL;
	}

	if ((capabilities & WL_SEAT_CAPABILITY_POINTER) != 0 && host->pointer == NULL) {
		host->pointer = wl_seat_get_pointer(seat);
		wl_pointer_add_listener(host->pointer, &pointer_listener, host);
		if (host->cursor_shape_manager != NULL)
			host->cursor_shape_device = wp_cursor_shape_manager_v1_get_pointer(host->cursor_shape_manager, host->pointer);
	} else if ((capabilities & WL_SEAT_CAPABILITY_POINTER) == 0 && host->pointer != NULL) {
		wl_pointer_release(host->pointer);
		host->pointer = NULL;
	}
}

static void seat_handle_name(void *data, struct wl_seat *seat, const char *name) {
	(void)data;
	(void)seat;
	(void)name;
}

static const struct wl_seat_listener seat_listener = {
	.capabilities = seat_handle_capabilities,
	.name = seat_handle_name,
};

static void keyboard_handle_keymap(void *data, struct wl_keyboard *keyboard, uint32_t format, int32_t fd, uint32_t size) {
	struct host *host = data;
	(void)keyboard;

	if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1 || size == 0) {
		close(fd);
		return;
	}

	void *map = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);
	if (map == MAP_FAILED)
		return;

	char *keymap = malloc((size_t)size + 1);
	if (keymap == NULL) {
		munmap(map, size);
		return;
	}
	memcpy(keymap, map, size);
	keymap[size] = '\0';
	munmap(map, size);

	server_keyboard_keymap(host->ds, keymap);
	free(keymap);
}

static void keyboard_handle_enter(void *data, struct wl_keyboard *keyboard, uint32_t serial, struct wl_surface *surface, struct wl_array *keys) {
	struct host *host = data;
	(void)keyboard;
	(void)surface;
	(void)keys;
	host->keyboard_entered = true;
	host->keyboard_enter_serial = serial;
	host->last_event_serial = serial;
	server_keyboard_focus(host->ds, true);
	host_apply_cursor_impl(host);
}

static void keyboard_handle_leave(void *data, struct wl_keyboard *keyboard, uint32_t serial, struct wl_surface *surface) {
	struct host *host = data;
	(void)keyboard;
	(void)serial;
	(void)surface;
	host->keyboard_entered = false;
	server_keyboard_focus(host->ds, false);
	host_apply_cursor_impl(host);
}

static void keyboard_handle_key(void *data, struct wl_keyboard *keyboard, uint32_t serial, uint32_t time, uint32_t key, uint32_t state) {
	struct host *host = data;
	(void)keyboard;
	(void)time;
	host->last_event_serial = serial;
	server_keyboard_key(host->ds, key, state == WL_KEYBOARD_KEY_STATE_PRESSED);
}

static void keyboard_handle_modifiers(void *data, struct wl_keyboard *keyboard, uint32_t serial, uint32_t mods_depressed, uint32_t mods_latched, uint32_t mods_locked, uint32_t group) {
	struct host *host = data;
	(void)keyboard;
	(void)serial;
	server_keyboard_modifiers(host->ds, mods_depressed, mods_latched, mods_locked, group);
}

static void keyboard_handle_repeat_info(void *data, struct wl_keyboard *keyboard, int32_t rate, int32_t delay) {
	(void)data;
	(void)keyboard;
	(void)rate;
	(void)delay;
}

static const struct wl_keyboard_listener keyboard_listener = {
	.keymap = keyboard_handle_keymap,
	.enter = keyboard_handle_enter,
	.leave = keyboard_handle_leave,
	.key = keyboard_handle_key,
	.modifiers = keyboard_handle_modifiers,
	.repeat_info = keyboard_handle_repeat_info,
};

static void pointer_handle_enter(void *data, struct wl_pointer *pointer, uint32_t serial, struct wl_surface *surface, wl_fixed_t sx, wl_fixed_t sy) {
	struct host *host = data;
	(void)pointer;
	(void)surface;
	host->pointer_enter_serial = serial;
	host->last_event_serial = serial;
	double x = wl_fixed_to_double(sx);
	double y = wl_fixed_to_double(sy);
	if (host->viewport != NULL) {
		double scale = host_get_scale(host->ds);
		x *= scale;
		y *= scale;
	}
	server_pointer_enter(host->ds, x, y);
}

static void pointer_handle_leave(void *data, struct wl_pointer *pointer, uint32_t serial, struct wl_surface *surface) {
	struct host *host = data;
	(void)pointer;
	(void)serial;
	(void)surface;
	dynscope_log_debug("dynscope: host pointer leave\n");
	server_pointer_leave(host->ds);
}

static void pointer_handle_motion(void *data, struct wl_pointer *pointer, uint32_t time, wl_fixed_t sx, wl_fixed_t sy) {
	struct host *host = data;
	(void)pointer;
	double x = wl_fixed_to_double(sx);
	double y = wl_fixed_to_double(sy);
	if (host->viewport != NULL) {
		double scale = host_get_scale(host->ds);
		x *= scale;
		y *= scale;
	}
	server_pointer_motion(host->ds, time, x, y);
}

static void pointer_handle_button(void *data, struct wl_pointer *pointer, uint32_t serial, uint32_t time, uint32_t button, uint32_t state) {
	struct host *host = data;
	(void)pointer;
	(void)time;
	host->last_event_serial = serial;
	dynscope_log_debug("dynscope: host button %u state=%u\n", button, state);
	server_pointer_button(host->ds, time, button, state);
}

static void pointer_handle_axis(void *data, struct wl_pointer *pointer, uint32_t time, uint32_t axis, wl_fixed_t value) {
	struct host *host = data;
	(void)pointer;
	if (axis > 1)
		return;
	host->pending_axis.value = wl_fixed_to_double(value);
	host->pending_axis.have = true;
	host->pending_axis.time = time;
	host->pending_axis.axis = axis;
}

static void pointer_handle_frame(void *data, struct wl_pointer *pointer) {
	struct host *host = data;
	(void)pointer;
	if (host->pending_axis.have) {
		server_pointer_axis(host->ds, host->pending_axis.time, host->pending_axis.axis, host->pending_axis.value, host->pending_axis.discrete, host->pending_axis.source);
		host->pending_axis.have = false;
		host->pending_axis.discrete = 0;
	}
}

static void pointer_handle_axis_source(void *data, struct wl_pointer *pointer, uint32_t axis_source) {
	struct host *host = data;
	(void)pointer;
	host->pending_axis.source = axis_source;
}

static void pointer_handle_axis_stop(void *data, struct wl_pointer *pointer, uint32_t time, uint32_t axis) {
	(void)data;
	(void)pointer;
	(void)time;
	(void)axis;
}

static void pointer_handle_axis_discrete(void *data, struct wl_pointer *pointer, uint32_t axis, int32_t discrete) {
	struct host *host = data;
	(void)pointer;
	(void)axis;
	host->pending_axis.discrete += discrete;
}

static void pointer_handle_axis_value120(void *data, struct wl_pointer *pointer, uint32_t axis, int32_t value120) {
	struct host *host = data;
	(void)pointer;
	(void)axis;
	host->pending_axis.discrete += value120 / 120;
}

static const struct wl_pointer_listener pointer_listener = {
	.enter = pointer_handle_enter,
	.leave = pointer_handle_leave,
	.motion = pointer_handle_motion,
	.button = pointer_handle_button,
	.axis = pointer_handle_axis,
	.frame = pointer_handle_frame,
	.axis_source = pointer_handle_axis_source,
	.axis_stop = pointer_handle_axis_stop,
	.axis_discrete = pointer_handle_axis_discrete,
	.axis_value120 = pointer_handle_axis_value120,
};

static void locked_pointer_handle_locked(void *data, struct zwp_locked_pointer_v1 *locked_pointer) {
	struct host *host = data;
	(void)locked_pointer;
	host->pointer_locked = true;
	dynscope_log_debug("dynscope: host pointer locked\n");
	host_apply_cursor_impl(host);
}

static void locked_pointer_handle_unlocked(void *data, struct zwp_locked_pointer_v1 *locked_pointer) {
	struct host *host = data;
	(void)locked_pointer;
	host->pointer_locked = false;
	dynscope_log_debug("dynscope: host pointer unlocked\n");
	host_apply_cursor_impl(host);
}

static const struct zwp_locked_pointer_v1_listener locked_pointer_listener = {
	.locked = locked_pointer_handle_locked,
	.unlocked = locked_pointer_handle_unlocked,
};

static void relative_pointer_handle_relative_motion(void *data, struct zwp_relative_pointer_v1 *relative_pointer, uint32_t time_hi, uint32_t time_lo, wl_fixed_t dx, wl_fixed_t dy, wl_fixed_t dx_unaccel, wl_fixed_t dy_unaccel) {
	struct host *host = data;
	(void)relative_pointer;
	(void)dx;
	(void)dy;
	if (!host->pointer_locked || !host->keyboard_entered)
		return;
	uint64_t time_usec = ((uint64_t)time_hi << 32) | time_lo;
	server_pointer_rel_motion(host->ds, (uint32_t)(time_usec / 1000), time_usec, wl_fixed_to_double(dx_unaccel), wl_fixed_to_double(dy_unaccel));
}

static const struct zwp_relative_pointer_v1_listener relative_pointer_listener = {
	.relative_motion = relative_pointer_handle_relative_motion,
};

static void host_cursor_clear_buffer(struct host_cursor *cursor) {
	free(cursor->pixels);
	cursor->pixels = NULL;
	cursor->width = 0;
	cursor->height = 0;
}

static void host_apply_cursor_impl(struct host *host) {
	struct host_cursor *cursor = &host->cursor;
	if (host->pointer == NULL || host->surface == NULL) {
		dynscope_log_debug("dynscope: apply cursor skipped pointer=%p surface=%p\n", (void *)host->pointer, (void *)host->surface);
		return;
	}

	dynscope_log_debug("dynscope: apply cursor mode=%d serial=%u locked=%d kb=%d\n", cursor->mode, host->pointer_enter_serial, host->pointer_locked, host->keyboard_entered);

	wl_display_dispatch_pending(host->display);
	wl_display_flush(host->display);

	if (host->pointer_locked) {
		if (!host->keyboard_entered) {
			if (host->cursor_shape_device != NULL)
				wp_cursor_shape_device_v1_set_shape(host->cursor_shape_device, host->pointer_enter_serial, WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT);
			else
				wl_pointer_set_cursor(host->pointer, host->pointer_enter_serial, NULL, 0, 0);
		} else {
			wl_pointer_set_cursor(host->pointer, host->pointer_enter_serial, NULL, 0, 0);
		}
		return;
	}

	if (cursor->mode == CURSOR_HIDDEN) {
		wl_pointer_set_cursor(host->pointer, host->pointer_enter_serial, NULL, 0, 0);
		return;
	}
	if (cursor->mode == CURSOR_DEFAULT) {
		if (host->cursor_shape_device != NULL)
			wp_cursor_shape_device_v1_set_shape(host->cursor_shape_device, host->pointer_enter_serial, WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT);
		else
			wl_pointer_set_cursor(host->pointer, host->pointer_enter_serial, NULL, 0, 0);
		return;
	}
	if (cursor->pixels == NULL || host->shm == NULL)
		return;

	size_t size = (size_t)cursor->width * (size_t)cursor->height * 4;
	int fd = memfd_create("dynscope-cursor", MFD_CLOEXEC);
	if (fd < 0)
		return;
	if (ftruncate(fd, (off_t)size) < 0) {
		close(fd);
		return;
	}
	void *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (data == MAP_FAILED) {
		close(fd);
		return;
	}
	memcpy(data, cursor->pixels, size);
	munmap(data, size);

	if (host->cursor_buffer != NULL) {
		wl_buffer_destroy(host->cursor_buffer);
		host->cursor_buffer = NULL;
	}
	if (host->cursor_pool != NULL) {
		wl_shm_pool_destroy(host->cursor_pool);
		host->cursor_pool = NULL;
	}

	struct wl_shm_pool *pool = wl_shm_create_pool(host->shm, fd, (int32_t)size);
	close(fd);
	if (pool == NULL)
		return;
	struct wl_buffer *buffer = wl_shm_pool_create_buffer(pool, 0, cursor->width, cursor->height, cursor->width * 4, WL_SHM_FORMAT_ARGB8888);
	host->cursor_pool = pool;
	if (buffer == NULL)
		return;

	if (host->cursor_surface == NULL) {
		host->cursor_surface = wl_compositor_create_surface(host->compositor);
		if (host->cursor_surface == NULL) {
			wl_buffer_destroy(buffer);
			return;
		}
	}
	double scale = host_get_scale(host->ds);
	if (host->cursor_viewport == NULL && host->viewporter != NULL)
		host->cursor_viewport = wp_viewporter_get_viewport(host->viewporter, host->cursor_surface);

	wl_surface_attach(host->cursor_surface, buffer, 0, 0);
	wl_surface_damage(host->cursor_surface, 0, 0, INT32_MAX, INT32_MAX);
	int logical_w = cursor->width;
	int logical_h = cursor->height;
	int logical_hx = cursor->hotspot_x;
	int logical_hy = cursor->hotspot_y;
	if (scale > 0.0 && scale != 1.0) {
		logical_w = (int)((double)cursor->width / scale + 0.5);
		logical_h = (int)((double)cursor->height / scale + 0.5);
		logical_hx = (int)((double)cursor->hotspot_x / scale + 0.5);
		logical_hy = (int)((double)cursor->hotspot_y / scale + 0.5);
	}
	if (host->cursor_viewport != NULL)
		wp_viewport_set_destination(host->cursor_viewport, logical_w, logical_h);
	else
		wl_surface_set_buffer_scale(host->cursor_surface, (int32_t)(scale + 0.5));
	wl_surface_commit(host->cursor_surface);
	host->cursor_buffer = buffer;

	wl_pointer_set_cursor(host->pointer, host->pointer_enter_serial, host->cursor_surface, logical_hx, logical_hy);
}

struct host_offer {
	struct host *host;
	struct wl_data_offer *offer;
	struct zwp_primary_selection_offer_v1 *primary_offer;
	bool primary;
	bool have_utf8;
	bool have_text;
};

static void data_offer_handle_offer(void *data, struct wl_data_offer *offer, const char *mime_type) {
	struct host_offer *ho = data;
	(void)offer;
	if (strcmp(mime_type, "text/plain;charset=utf-8") == 0)
		ho->have_utf8 = true;
	else if (strcmp(mime_type, "text/plain") == 0)
		ho->have_text = true;
}

static void data_offer_handle_source_actions(void *data, struct wl_data_offer *offer, uint32_t source_actions) {
	(void)data;
	(void)offer;
	(void)source_actions;
}

static void data_offer_handle_action(void *data, struct wl_data_offer *offer, uint32_t dnd_action) {
	(void)data;
	(void)offer;
	(void)dnd_action;
}

static const struct wl_data_offer_listener data_offer_listener = {
	.offer = data_offer_handle_offer,
	.source_actions = data_offer_handle_source_actions,
	.action = data_offer_handle_action,
};

static void primary_offer_handle_offer(void *data, struct zwp_primary_selection_offer_v1 *offer, const char *mime_type) {
	struct host_offer *ho = data;
	(void)offer;
	if (strcmp(mime_type, "text/plain;charset=utf-8") == 0)
		ho->have_utf8 = true;
	else if (strcmp(mime_type, "text/plain") == 0)
		ho->have_text = true;
}

static const struct zwp_primary_selection_offer_v1_listener primary_offer_listener = {
	.offer = primary_offer_handle_offer,
};

static void host_data_set(char **data, size_t *len, const char *src, size_t src_len) {
	free(*data);
	*data = NULL;
	*len = 0;
	if (src_len == 0)
		return;
	char *copy = malloc(src_len + 1);
	if (copy == NULL)
		return;
	memcpy(copy, src, src_len);
	copy[src_len] = '\0';
	*data = copy;
	*len = src_len;
}

static void host_read_finish(struct host_read *read) {
	if (read->dead)
		return;
	read->dead = true;
	if (read->src != NULL) {
		wl_event_source_remove(read->src);
		read->src = NULL;
	}
	close(read->fd);
	read->fd = -1;
	if (read->offer != NULL)
		wl_data_offer_destroy(read->offer);
	if (read->primary_offer != NULL)
		zwp_primary_selection_offer_v1_destroy(read->primary_offer);
	read->offer = NULL;
	read->primary_offer = NULL;
	struct host *host = read->host;
	char *data = read->buf;
	size_t len = read->len;
	read->buf = NULL;
	read->len = 0;
	read->cap = 0;
	bool primary = read->primary;
	if (data != NULL && len > 0)
		server_host_selection(host->ds, primary, data, len);
	free(data);
}

static int host_read_event(int fd, uint32_t mask, void *data) {
	struct host_read *r = data;
	(void)fd;
	if (r->dead)
		return 0;
	if ((mask & WL_EVENT_READABLE) == 0) {
		if ((mask & (WL_EVENT_HANGUP | WL_EVENT_ERROR)) != 0)
			host_read_finish(r);
		return 0;
	}
	while (true) {
		if (r->len + 4096 > r->cap) {
			size_t new_cap = r->cap == 0 ? 4096 : r->cap * 2;
			char *new_buf = realloc(r->buf, new_cap);
			if (new_buf == NULL)
				break;
			r->buf = new_buf;
			r->cap = new_cap;
		}
		ssize_t n = read(r->fd, r->buf + r->len, 4096);
		if (n > 0) {
			r->len += (size_t)n;
			continue;
		}
		if (n < 0 && (errno == EAGAIN || errno == EINTR))
			return 1;
		host_read_finish(r);
		return 0;
	}
	return 1;
}

static void host_selection_read(struct host *host, struct host_offer *ho) {
	const char *mime = ho->have_utf8 ? "text/plain;charset=utf-8" : (ho->have_text ? "text/plain" : NULL);
	if (mime == NULL) {
		if (ho->primary && ho->primary_offer != NULL)
			zwp_primary_selection_offer_v1_destroy(ho->primary_offer);
		else if (!ho->primary && ho->offer != NULL)
			wl_data_offer_destroy(ho->offer);
		return;
	}
	int fds[2];
	if (pipe(fds) < 0) {
		if (ho->primary && ho->primary_offer != NULL)
			zwp_primary_selection_offer_v1_destroy(ho->primary_offer);
		else if (!ho->primary && ho->offer != NULL)
			wl_data_offer_destroy(ho->offer);
		return;
	}
	struct host_read *read = ho->primary ? &host->read_primary : &host->read_clip;
	if (read->src != NULL || read->buf != NULL) {
		close(fds[0]);
		close(fds[1]);
		if (ho->primary && ho->primary_offer != NULL)
			zwp_primary_selection_offer_v1_destroy(ho->primary_offer);
		else if (!ho->primary && ho->offer != NULL)
			wl_data_offer_destroy(ho->offer);
		return;
	}
	if (ho->primary)
		zwp_primary_selection_offer_v1_receive(ho->primary_offer, mime, fds[1]);
	else
		wl_data_offer_receive(ho->offer, mime, fds[1]);
	wl_display_flush(host->display);
	close(fds[1]);
	read->host = host;
	read->primary = ho->primary;
	read->fd = fds[0];
	read->offer = ho->offer;
	read->primary_offer = ho->primary_offer;
	read->buf = NULL;
	read->len = 0;
	read->cap = 0;
	read->dead = false;
	int flags = fcntl(fds[0], F_GETFL);
	if (flags >= 0)
		fcntl(fds[0], F_SETFL, flags | O_NONBLOCK);
	read->src = wl_event_loop_add_fd(host->ds->loop, fds[0], WL_EVENT_READABLE, host_read_event, read);
	if (read->src == NULL)
		close(fds[0]);
}

static void data_device_handle_data_offer(void *data, struct wl_data_device *device, struct wl_data_offer *offer) {
	struct host *host = data;
	(void)device;
	if (host->pending_clip_offer != NULL) {
		wl_data_offer_destroy(host->pending_clip_offer->offer);
		free(host->pending_clip_offer);
		host->pending_clip_offer = NULL;
	}
	struct host_offer *ho = calloc(1, sizeof(*ho));
	if (ho == NULL) {
		wl_data_offer_destroy(offer);
		return;
	}
	ho->host = host;
	ho->offer = offer;
	wl_data_offer_add_listener(offer, &data_offer_listener, ho);
	host->pending_clip_offer = ho;
}

static void data_device_handle_enter(void *data, struct wl_data_device *device, uint32_t serial, struct wl_surface *surface, wl_fixed_t x, wl_fixed_t y, struct wl_data_offer *offer) {
	(void)data;
	(void)device;
	(void)serial;
	(void)surface;
	(void)x;
	(void)y;
	if (offer != NULL)
		wl_data_offer_destroy(offer);
}

static void data_device_handle_leave(void *data, struct wl_data_device *device) {
	(void)data;
	(void)device;
}

static void data_device_handle_motion(void *data, struct wl_data_device *device, uint32_t time, wl_fixed_t x, wl_fixed_t y) {
	(void)data;
	(void)device;
	(void)time;
	(void)x;
	(void)y;
}

static void data_device_handle_drop(void *data, struct wl_data_device *device) {
	(void)data;
	(void)device;
}

static void data_device_handle_selection(void *data, struct wl_data_device *device, struct wl_data_offer *offer) {
	struct host *host = data;
	(void)device;
	struct host_offer *ho = host->pending_clip_offer;
	host->pending_clip_offer = NULL;
	if (offer == NULL || ho == NULL || ho->offer != offer) {
		if (offer != NULL)
			wl_data_offer_destroy(offer);
		free(ho);
		return;
	}
	host_selection_read(host, ho);
	free(ho);
}

static const struct wl_data_device_listener data_device_listener = {
	.data_offer = data_device_handle_data_offer,
	.enter = data_device_handle_enter,
	.leave = data_device_handle_leave,
	.motion = data_device_handle_motion,
	.drop = data_device_handle_drop,
	.selection = data_device_handle_selection,
};

static void primary_device_handle_data_offer(void *data, struct zwp_primary_selection_device_v1 *device, struct zwp_primary_selection_offer_v1 *offer) {
	struct host *host = data;
	(void)device;
	if (host->pending_primary_offer != NULL) {
		zwp_primary_selection_offer_v1_destroy(host->pending_primary_offer->primary_offer);
		free(host->pending_primary_offer);
		host->pending_primary_offer = NULL;
	}
	struct host_offer *ho = calloc(1, sizeof(*ho));
	if (ho == NULL) {
		zwp_primary_selection_offer_v1_destroy(offer);
		return;
	}
	ho->host = host;
	ho->primary = true;
	ho->primary_offer = offer;
	zwp_primary_selection_offer_v1_add_listener(offer, &primary_offer_listener, ho);
	host->pending_primary_offer = ho;
}

static void primary_device_handle_selection(void *data, struct zwp_primary_selection_device_v1 *device, struct zwp_primary_selection_offer_v1 *offer) {
	struct host *host = data;
	(void)device;
	struct host_offer *ho = host->pending_primary_offer;
	host->pending_primary_offer = NULL;
	if (offer == NULL || ho == NULL || ho->primary_offer != offer) {
		if (offer != NULL)
			zwp_primary_selection_offer_v1_destroy(offer);
		free(ho);
		return;
	}
	host_selection_read(host, ho);
	free(ho);
}

static const struct zwp_primary_selection_device_v1_listener primary_device_listener = {
	.data_offer = primary_device_handle_data_offer,
	.selection = primary_device_handle_selection,
};

static void data_source_handle_target(void *data, struct wl_data_source *source, const char *mime_type) {
	(void)data;
	(void)source;
	(void)mime_type;
}

static void data_source_handle_send(void *data, struct wl_data_source *source, const char *mime_type, int32_t fd) {
	struct host *host = data;
	(void)source;
	(void)mime_type;
	if (host->clip_data == NULL || write(fd, host->clip_data, host->clip_len) != (ssize_t)host->clip_len)
		fprintf(stderr, "dynscope: failed to write clipboard data\n");
	close(fd);
}

static void data_source_handle_cancelled(void *data, struct wl_data_source *source) {
	struct host *host = data;
	(void)source;
	if (host->clipboard_source == source)
		host->clipboard_source = NULL;
	wl_data_source_destroy(source);
}

static void data_source_handle_dnd_drop_performed(void *data, struct wl_data_source *source) {
	(void)data;
	(void)source;
}

static void data_source_handle_dnd_finished(void *data, struct wl_data_source *source) {
	(void)data;
	(void)source;
}

static void data_source_handle_action(void *data, struct wl_data_source *source, uint32_t dnd_action) {
	(void)data;
	(void)source;
	(void)dnd_action;
}

static const struct wl_data_source_listener data_source_listener = {
	.target = data_source_handle_target,
	.send = data_source_handle_send,
	.cancelled = data_source_handle_cancelled,
	.dnd_drop_performed = data_source_handle_dnd_drop_performed,
	.dnd_finished = data_source_handle_dnd_finished,
	.action = data_source_handle_action,
};

static void primary_source_handle_send(void *data, struct zwp_primary_selection_source_v1 *source, const char *mime_type, int32_t fd) {
	struct host *host = data;
	(void)source;
	(void)mime_type;
	if (host->primary_data == NULL || write(fd, host->primary_data, host->primary_len) != (ssize_t)host->primary_len)
		fprintf(stderr, "dynscope: failed to write primary selection data\n");
	close(fd);
}

static void primary_source_handle_cancelled(void *data, struct zwp_primary_selection_source_v1 *source) {
	struct host *host = data;
	(void)source;
	if (host->primary_source == source)
		host->primary_source = NULL;
	zwp_primary_selection_source_v1_destroy(source);
}

static const struct zwp_primary_selection_source_v1_listener primary_source_listener = {
	.send = primary_source_handle_send,
	.cancelled = primary_source_handle_cancelled,
};

void host_set_selection(struct dynscope *ds, bool primary, const char *data, size_t len) {
	struct host *host = ds->host;
	if (host == NULL)
		return;
	uint32_t serial = host->last_event_serial;
	if (serial == 0)
		serial = host->keyboard_enter_serial;
	if (serial == 0)
		serial = host->pointer_enter_serial;
	if (primary) {
		host_data_set(&host->primary_data, &host->primary_len, data, len);
		if (host->primary_device == NULL || host->primary_manager == NULL)
			return;
		if (host->primary_source != NULL) {
			zwp_primary_selection_source_v1_destroy(host->primary_source);
			host->primary_source = NULL;
		}
		struct zwp_primary_selection_source_v1 *source = zwp_primary_selection_device_manager_v1_create_source(host->primary_manager);
		if (source == NULL)
			return;
		zwp_primary_selection_source_v1_add_listener(source, &primary_source_listener, host);
		zwp_primary_selection_source_v1_offer(source, "text/plain;charset=utf-8");
		zwp_primary_selection_source_v1_offer(source, "text/plain");
		host->primary_source = source;
		zwp_primary_selection_device_v1_set_selection(host->primary_device, source, serial);
	} else {
		host_data_set(&host->clip_data, &host->clip_len, data, len);
		if (host->data_device == NULL || host->data_device_manager == NULL)
			return;
		if (host->clipboard_source != NULL) {
			wl_data_source_destroy(host->clipboard_source);
			host->clipboard_source = NULL;
		}
		struct wl_data_source *source = wl_data_device_manager_create_data_source(host->data_device_manager);
		if (source == NULL)
			return;
		wl_data_source_add_listener(source, &data_source_listener, host);
		wl_data_source_offer(source, "text/plain;charset=utf-8");
		wl_data_source_offer(source, "text/plain");
		host->clipboard_source = source;
		wl_data_device_set_selection(host->data_device, source, serial);
	}
	wl_display_flush(host->display);
}

static void host_frame_idle(void *data) {
	struct host *host = data;
	host->idle_source = NULL;
	if (host->frame != NULL) {
		host->frame_pending = true;
		return;
	}
	host_present(host);
}

void host_request_frame(struct dynscope *ds) {
	if (ds == NULL || ds->host == NULL)
		return;
	struct host *host = ds->host;
	if (host->width <= 0 || host->height <= 0)
		return;
	if (host->frame != NULL) {
		host->frame_pending = true;
		return;
	}
	if (host->idle_source == NULL)
		host->idle_source = wl_event_loop_add_idle(ds->loop, host_frame_idle, host);
}

double host_get_scale(struct dynscope *ds) {
	if (ds == NULL || ds->host == NULL)
		return 1.0;
	if (ds->host->scale_120 > 0)
		return (double)ds->host->scale_120 / 120.0;
	return 1.0;
}

void host_set_initial_size(struct dynscope *ds, int width, int height) {
	if (ds == NULL || ds->host == NULL)
		return;
	struct host *host = ds->host;
	if (host->user_resized || width <= 0 || height <= 0)
		return;
	if (host->width == width && host->height == height)
		return;
	host->width = width;
	host->height = height;
	dynscope_log_debug("dynscope: matching initial host window size to game resolution %dx%d\n", width, height);
	host_request_frame(ds);
}

void host_set_cursor_hidden(struct dynscope *ds) {
	if (ds->host == NULL)
		return;
	host_cursor_clear_buffer(&ds->host->cursor);
	ds->host->cursor.mode = CURSOR_HIDDEN;
	host_apply_cursor_impl(ds->host);
}

void host_set_locked(struct dynscope *ds, bool locked) {
	struct host *host = ds->host;
	if (host == NULL || host->pointer == NULL || host->surface == NULL)
		return;
	if (host->pointer_constraints == NULL || host->relative_pointer_manager == NULL) {
		static bool warned = false;
		if (locked && !warned) {
			fprintf(stderr, "dynscope: host compositor lacks pointer-constraints/relative-pointer, cursor lock unavailable\n");
			warned = true;
		}
		return;
	}

	if (locked == (host->locked_pointer != NULL))
		return;

	if (host->locked_pointer != NULL) {
		zwp_locked_pointer_v1_destroy(host->locked_pointer);
		host->locked_pointer = NULL;
		zwp_relative_pointer_v1_destroy(host->relative_pointer);
		host->relative_pointer = NULL;
		host->pointer_locked = false;
	}

	if (locked) {
		host->locked_pointer = zwp_pointer_constraints_v1_lock_pointer(host->pointer_constraints, host->surface, host->pointer, NULL, ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_PERSISTENT);
		if (host->locked_pointer == NULL)
			return;
		zwp_locked_pointer_v1_add_listener(host->locked_pointer, &locked_pointer_listener, host);
		host->relative_pointer = zwp_relative_pointer_manager_v1_get_relative_pointer(host->relative_pointer_manager, host->pointer);
		if (host->relative_pointer != NULL)
			zwp_relative_pointer_v1_add_listener(host->relative_pointer, &relative_pointer_listener, host);
		dynscope_log_debug("dynscope: host lock requested\n");
	}

	host_apply_cursor_impl(host);
}

void host_apply_cursor(struct dynscope *ds) {
	if (ds->host != NULL)
		host_apply_cursor_impl(ds->host);
}

void host_set_title(struct dynscope *ds, const char *title) {
	struct host *host = ds->host;
	if (host == NULL || host->toplevel == NULL)
		return;
	xdg_toplevel_set_title(host->toplevel, title);
}



void host_set_cursor(struct dynscope *ds, const void *pixels, int width, int height, int hotspot_x, int hotspot_y) {
	struct host *host = ds->host;
	if (host == NULL)
		return;

	struct host_cursor *cursor = &host->cursor;
	if (pixels == NULL || width <= 0 || height <= 0) {
		host_cursor_clear_buffer(cursor);
		cursor->mode = CURSOR_DEFAULT;
	} else {
		uint32_t *copy = malloc((size_t)width * (size_t)height * 4);
		if (copy == NULL)
			return;
		memcpy(copy, pixels, (size_t)width * (size_t)height * 4);
		host_cursor_clear_buffer(cursor);
		cursor->pixels = copy;
		cursor->width = width;
		cursor->height = height;
		cursor->hotspot_x = hotspot_x;
		cursor->hotspot_y = hotspot_y;
		cursor->mode = CURSOR_PIXELS;
	}
	host_apply_cursor_impl(host);
}

static int host_fd_event(int fd, uint32_t mask, void *data) {
	(void)fd;
	struct host *host = data;

	if ((mask & (WL_EVENT_HANGUP | WL_EVENT_ERROR)) != 0) {
		fprintf(stderr, "dynscope: host compositor connection lost\n");
		dynscope_close(host->ds);
		return 0;
	}
	if ((mask & WL_EVENT_READABLE) != 0) {
		if (wl_display_dispatch(host->display) < 0) {
			fprintf(stderr, "dynscope: host compositor connection error: %s\n", strerror(errno));
			dynscope_close(host->ds);
			return 0;
		}
	}
	return 1;
}

static int host_flush_writable(int fd, uint32_t mask, void *data) {
	struct host *host = data;
	(void)fd;
	if ((mask & WL_EVENT_WRITABLE) == 0)
		return 1;

	if (wl_display_flush(host->display) >= 0 || errno != EAGAIN) {
		wl_event_source_remove(host->flush_src);
		host->flush_src = NULL;
	}
	return 1;
}

void host_flush(struct dynscope *ds) {
	struct host *host = ds->host;
	if (host == NULL || host->display == NULL)
		return;

	if (wl_display_flush(host->display) >= 0 || errno != EAGAIN)
		return;

	if (host->flush_src == NULL)
		host->flush_src = wl_event_loop_add_fd(ds->loop, wl_display_get_fd(host->display), WL_EVENT_WRITABLE, host_flush_writable, host);
}

int host_open(struct dynscope *ds) {
	struct host *host = calloc(1, sizeof(*host));
	if (host == NULL) {
		fprintf(stderr, "dynscope: out of memory\n");
		return -1;
	}
	host->ds = ds;
	wl_array_init(&host->outstanding);
	ds->host = host;

	host->display = wl_display_connect(NULL);
	if (host->display == NULL) {
		fprintf(stderr, "dynscope: failed to connect to host Wayland compositor\n");
		goto fail;
	}

	host->registry = wl_display_get_registry(host->display);
	wl_registry_add_listener(host->registry, &registry_listener, host);
	wl_display_roundtrip(host->display);

	if (host->compositor == NULL || host->wm_base == NULL || host->seat == NULL || host->dmabuf == NULL || host->shm == NULL) {
		fprintf(stderr, "dynscope: host compositor is missing required Wayland globals (compositor, shm, seat, xdg_wm_base, linux-dmabuf)\n");
		goto fail;
	}

	host->surface = wl_compositor_create_surface(host->compositor);
	if (host->viewporter != NULL)
		host->viewport = wp_viewporter_get_viewport(host->viewporter, host->surface);
	if (host->fractional_scale_manager != NULL) {
		host->fractional_scale = wp_fractional_scale_manager_v1_get_fractional_scale(host->fractional_scale_manager, host->surface);
		wp_fractional_scale_v1_add_listener(host->fractional_scale, &fractional_scale_listener, host);
	}
	host->xdg_surface = xdg_wm_base_get_xdg_surface(host->wm_base, host->surface);
	xdg_wm_base_add_listener(host->wm_base, &wm_base_listener, host);
	xdg_surface_add_listener(host->xdg_surface, &xdg_surface_listener, host);

	host->toplevel = xdg_surface_get_toplevel(host->xdg_surface);
	xdg_toplevel_set_title(host->toplevel, "dynscope");
	xdg_toplevel_set_app_id(host->toplevel, "dynscope");
	xdg_toplevel_add_listener(host->toplevel, &toplevel_listener, host);

	if (host->decoration_manager != NULL) {
		host->toplevel_decoration = zxdg_decoration_manager_v1_get_toplevel_decoration(host->decoration_manager, host->toplevel);
		zxdg_toplevel_decoration_v1_set_mode(host->toplevel_decoration, ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
		zxdg_toplevel_decoration_v1_add_listener(host->toplevel_decoration, &decoration_listener, host);
	}

	wl_seat_add_listener(host->seat, &seat_listener, host);

	if (host->data_device_manager != NULL) {
		host->data_device = wl_data_device_manager_get_data_device(host->data_device_manager, host->seat);
		wl_data_device_add_listener(host->data_device, &data_device_listener, host);
	}
	if (host->primary_manager != NULL) {
		host->primary_device = zwp_primary_selection_device_manager_v1_get_device(host->primary_manager, host->seat);
		zwp_primary_selection_device_v1_add_listener(host->primary_device, &primary_device_listener, host);
	}

	wl_surface_commit(host->surface);

	host->fd_src = wl_event_loop_add_fd(ds->loop, wl_display_get_fd(host->display), WL_EVENT_READABLE, host_fd_event, host);
	if (host->fd_src == NULL) {
		fprintf(stderr, "dynscope: failed to watch host compositor connection\n");
		goto fail;
	}

	return 0;

fail:
	host_close(ds);
	return -1;
}

void host_close(struct dynscope *ds) {
	struct host *host = ds->host;
	if (host == NULL)
		return;
	ds->host = NULL;

	if (host->close_timer != NULL)
		wl_event_source_remove(host->close_timer);
	if (host->idle_source != NULL)
		wl_event_source_remove(host->idle_source);
	if (host->frame != NULL)
		wl_callback_destroy(host->frame);
	if (host->toplevel_decoration != NULL)
		zxdg_toplevel_decoration_v1_destroy(host->toplevel_decoration);
	if (host->decoration_manager != NULL)
		zxdg_decoration_manager_v1_destroy(host->decoration_manager);
	if (host->fractional_scale != NULL)
		wp_fractional_scale_v1_destroy(host->fractional_scale);
	if (host->fractional_scale_manager != NULL)
		wp_fractional_scale_manager_v1_destroy(host->fractional_scale_manager);
	if (host->cursor_viewport != NULL)
		wp_viewport_destroy(host->cursor_viewport);
	if (host->viewport != NULL)
		wp_viewport_destroy(host->viewport);
	if (host->viewporter != NULL)
		wp_viewporter_destroy(host->viewporter);
	struct outstanding *outstanding;
	wl_array_for_each(outstanding, &host->outstanding)
		wl_buffer_destroy(outstanding->buffer);
	wl_array_release(&host->outstanding);
	if (host->fd_src != NULL)
		wl_event_source_remove(host->fd_src);
	if (host->flush_src != NULL)
		wl_event_source_remove(host->flush_src);
	for (int i = 0; i < 2; i++) {
		struct host_read *read = i == 0 ? &host->read_clip : &host->read_primary;
		if (read->src != NULL)
			wl_event_source_remove(read->src);
		if (read->fd >= 0)
			close(read->fd);
		free(read->buf);
	}
	if (host->pending_clip_offer != NULL) {
		wl_data_offer_destroy(host->pending_clip_offer->offer);
		free(host->pending_clip_offer);
	}
	if (host->pending_primary_offer != NULL) {
		zwp_primary_selection_offer_v1_destroy(host->pending_primary_offer->primary_offer);
		free(host->pending_primary_offer);
	}
	if (host->clipboard_source != NULL)
		wl_data_source_destroy(host->clipboard_source);
	if (host->primary_source != NULL)
		zwp_primary_selection_source_v1_destroy(host->primary_source);
	if (host->data_device != NULL)
		wl_data_device_release(host->data_device);
	if (host->primary_device != NULL)
		zwp_primary_selection_device_v1_destroy(host->primary_device);
	if (host->data_device_manager != NULL)
		wl_data_device_manager_destroy(host->data_device_manager);
	if (host->primary_manager != NULL)
		zwp_primary_selection_device_manager_v1_destroy(host->primary_manager);
	free(host->clip_data);
	free(host->primary_data);
	if (host->keyboard != NULL)
		wl_keyboard_destroy(host->keyboard);
	if (host->pointer != NULL)
		wl_pointer_destroy(host->pointer);
	if (host->relative_pointer != NULL)
		zwp_relative_pointer_v1_destroy(host->relative_pointer);
	if (host->locked_pointer != NULL)
		zwp_locked_pointer_v1_destroy(host->locked_pointer);
	if (host->relative_pointer_manager != NULL)
		zwp_relative_pointer_manager_v1_destroy(host->relative_pointer_manager);
	if (host->pointer_constraints != NULL)
		zwp_pointer_constraints_v1_destroy(host->pointer_constraints);
	if (host->cursor_shape_device != NULL)
		wp_cursor_shape_device_v1_destroy(host->cursor_shape_device);
	if (host->cursor_shape_manager != NULL)
		wp_cursor_shape_manager_v1_destroy(host->cursor_shape_manager);
	if (host->cursor_buffer != NULL)
		wl_buffer_destroy(host->cursor_buffer);
	if (host->cursor_pool != NULL)
		wl_shm_pool_destroy(host->cursor_pool);
	if (host->cursor_surface != NULL)
		wl_surface_destroy(host->cursor_surface);
	if (host->toplevel != NULL)
		xdg_toplevel_destroy(host->toplevel);
	if (host->xdg_surface != NULL)
		xdg_surface_destroy(host->xdg_surface);
	if (host->surface != NULL)
		wl_surface_destroy(host->surface);
	if (host->wm_base != NULL)
		xdg_wm_base_destroy(host->wm_base);
	if (host->dmabuf != NULL)
		zwp_linux_dmabuf_v1_destroy(host->dmabuf);
	if (host->shm != NULL)
		wl_shm_destroy(host->shm);
	if (host->seat != NULL)
		wl_seat_destroy(host->seat);
	if (host->compositor != NULL)
		wl_compositor_destroy(host->compositor);
	if (host->registry != NULL)
		wl_registry_destroy(host->registry);
	if (host->display != NULL)
		wl_display_disconnect(host->display);
	host_cursor_clear_buffer(&host->cursor);
	free(host);
}
