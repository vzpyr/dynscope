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

#define DEFAULT_WIDTH 1280
#define DEFAULT_HEIGHT 720
#define MAX_OUTSTANDING 4

struct outstanding {
	int generation;
	struct wl_buffer *buffer;
};

struct host {
	struct wl_display *display;
	struct wl_registry *registry;
	struct wl_compositor *compositor;
	struct wl_seat *seat;
	struct zwp_linux_dmabuf_v1 *dmabuf;
	struct xdg_wm_base *wm_base;
	struct wl_surface *surface;
	struct xdg_surface *xdg_surface;
	struct xdg_toplevel *toplevel;

	struct wl_keyboard *keyboard;
	struct wl_array outstanding;

	struct wl_event_source *fd_src;
	struct wl_event_source *flush_src;
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
static const struct wl_buffer_listener buffer_listener;

static void registry_handle_global(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version) {
	struct host *host = data;

	if (strcmp(interface, wl_compositor_interface.name) == 0)
		host->compositor = wl_registry_bind(registry, name, &wl_compositor_interface, version < (uint32_t)wl_compositor_interface.version ? version : (uint32_t)wl_compositor_interface.version);
	else if (strcmp(interface, wl_seat_interface.name) == 0)
		host->seat = wl_registry_bind(registry, name, &wl_seat_interface, version < 8u ? version : 8u);
	else if (strcmp(interface, xdg_wm_base_interface.name) == 0)
		host->wm_base = wl_registry_bind(registry, name, &xdg_wm_base_interface, version < (uint32_t)xdg_wm_base_interface.version ? version : (uint32_t)xdg_wm_base_interface.version);
	else if (strcmp(interface, zwp_linux_dmabuf_v1_interface.name) == 0)
		host->dmabuf = wl_registry_bind(registry, name, &zwp_linux_dmabuf_v1_interface, version < 3u ? version : 3u);
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
	host->pending_width = width;
	host->pending_height = height;
}

static void toplevel_handle_close(void *data, struct xdg_toplevel *toplevel) {
	struct host *host = data;
	(void)toplevel;
	dynscope_close(host->ds);
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

	struct frame_info info;
	server_present(host->ds, host->width, host->height, &info);
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

	if (host->frame != NULL) {
		wl_callback_destroy(host->frame);
		host->frame = NULL;
	}
	host->frame = wl_surface_frame(host->surface);
	wl_callback_add_listener(host->frame, &frame_listener, host);

	wl_surface_attach(host->surface, buffer, 0, 0);
	wl_surface_damage(host->surface, 0, 0, INT32_MAX, INT32_MAX);
	wl_surface_commit(host->surface);
}

static void frame_handle_done(void *data, struct wl_callback *callback, uint32_t msecs) {
	struct host *host = data;
	(void)msecs;

	wl_callback_destroy(callback);
	host->frame = NULL;

	if (!host->ds->running)
		return;
	host_present(host);
}

static const struct wl_callback_listener frame_listener = {
	.done = frame_handle_done,
};

static void xdg_surface_handle_configure(void *data, struct xdg_surface *xdg_surface, uint32_t serial) {
	struct host *host = data;
	xdg_surface_ack_configure(xdg_surface, serial);

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

	host_present(host);
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
	(void)data;
	(void)keyboard;
	(void)serial;
	(void)surface;
	(void)keys;
}

static void keyboard_handle_leave(void *data, struct wl_keyboard *keyboard, uint32_t serial, struct wl_surface *surface) {
	(void)data;
	(void)keyboard;
	(void)serial;
	(void)surface;
}

static void keyboard_handle_key(void *data, struct wl_keyboard *keyboard, uint32_t serial, uint32_t time, uint32_t key, uint32_t state) {
	struct host *host = data;
	(void)keyboard;
	(void)serial;
	(void)time;
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

	if (host->compositor == NULL || host->wm_base == NULL || host->seat == NULL || host->dmabuf == NULL) {
		fprintf(stderr, "dynscope: host compositor is missing required Wayland globals (compositor, seat, xdg_wm_base, linux-dmabuf)\n");
		goto fail;
	}

	host->surface = wl_compositor_create_surface(host->compositor);
	host->xdg_surface = xdg_wm_base_get_xdg_surface(host->wm_base, host->surface);
	xdg_wm_base_add_listener(host->wm_base, &wm_base_listener, host);
	xdg_surface_add_listener(host->xdg_surface, &xdg_surface_listener, host);

	host->toplevel = xdg_surface_get_toplevel(host->xdg_surface);
	xdg_toplevel_set_title(host->toplevel, "dynscope");
	xdg_toplevel_set_app_id(host->toplevel, "dynscope");
	xdg_toplevel_add_listener(host->toplevel, &toplevel_listener, host);

	wl_seat_add_listener(host->seat, &seat_listener, host);

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

	if (host->frame != NULL)
		wl_callback_destroy(host->frame);
	struct outstanding *outstanding;
	wl_array_for_each(outstanding, &host->outstanding)
		wl_buffer_destroy(outstanding->buffer);
	wl_array_release(&host->outstanding);
	if (host->fd_src != NULL)
		wl_event_source_remove(host->fd_src);
	if (host->flush_src != NULL)
		wl_event_source_remove(host->flush_src);
	if (host->keyboard != NULL)
		wl_keyboard_destroy(host->keyboard);
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
	if (host->seat != NULL)
		wl_seat_destroy(host->seat);
	if (host->compositor != NULL)
		wl_compositor_destroy(host->compositor);
	if (host->registry != NULL)
		wl_registry_destroy(host->registry);
	if (host->display != NULL)
		wl_display_disconnect(host->display);
	free(host);
}
