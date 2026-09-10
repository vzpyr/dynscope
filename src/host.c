#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <wayland-client.h>

#include "dynscope.h"
#include "host.h"
#include "xdg-shell-client-protocol.h"

#define DEFAULT_WIDTH 1280
#define DEFAULT_HEIGHT 720

struct host {
	struct wl_display *display;
	struct wl_registry *registry;
	struct wl_compositor *compositor;
	struct wl_shm *shm;
	struct xdg_wm_base *wm_base;
	struct wl_surface *surface;
	struct xdg_surface *xdg_surface;
	struct xdg_toplevel *toplevel;

	struct wl_event_source *fd_src;
	struct wl_event_source *flush_src;
	struct wl_callback *frame;

	struct wl_shm_pool *pool;
	struct wl_buffer *buffer;
	int shm_fd;
	void *data;
	size_t size;
	int width;
	int height;

	int pending_width;
	int pending_height;

	struct dynscope *ds;
};

static const struct wl_registry_listener registry_listener;
static const struct xdg_wm_base_listener wm_base_listener;
static const struct xdg_toplevel_listener toplevel_listener;
static const struct xdg_surface_listener xdg_surface_listener;
static const struct wl_callback_listener frame_listener;
static const struct wl_buffer_listener buffer_listener;

static uint32_t bind_version(uint32_t advertised, uint32_t supported) {
	return advertised < supported ? advertised : supported;
}

static void registry_handle_global(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version) {
	struct host *host = data;

	if (strcmp(interface, wl_compositor_interface.name) == 0)
		host->compositor = wl_registry_bind(registry, name, &wl_compositor_interface, bind_version(version, wl_compositor_interface.version));
	else if (strcmp(interface, wl_shm_interface.name) == 0)
		host->shm = wl_registry_bind(registry, name, &wl_shm_interface, bind_version(version, wl_shm_interface.version));
	else if (strcmp(interface, xdg_wm_base_interface.name) == 0)
		host->wm_base = wl_registry_bind(registry, name, &xdg_wm_base_interface, bind_version(version, xdg_wm_base_interface.version));
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

static void frame_handle_done(void *data, struct wl_callback *callback, uint32_t msecs) {
	struct host *host = data;
	(void)msecs;

	wl_callback_destroy(callback);
	host->frame = NULL;

	if (!host->ds->running || host->buffer == NULL)
		return;

	host->frame = wl_surface_frame(host->surface);
	wl_callback_add_listener(host->frame, &frame_listener, host);
	wl_surface_commit(host->surface);
}

static const struct wl_callback_listener frame_listener = {
	.done = frame_handle_done,
};

static void buffer_handle_release(void *data, struct wl_buffer *buffer) {
	(void)data;
	(void)buffer;
}

static const struct wl_buffer_listener buffer_listener = {
	.release = buffer_handle_release,
};

static void host_ensure_buffer(struct host *host, int width, int height) {
	if (host->buffer != NULL) {
		wl_buffer_destroy(host->buffer);
		host->buffer = NULL;
	}
	if (host->pool != NULL) {
		wl_shm_pool_destroy(host->pool);
		host->pool = NULL;
	}
	if (host->data != NULL) {
		munmap(host->data, host->size);
		host->data = NULL;
	}
	if (host->shm_fd >= 0) {
		close(host->shm_fd);
		host->shm_fd = -1;
	}

	size_t size = (size_t)width * (size_t)height * 4;
	int fd = memfd_create("dynscope-shm", MFD_CLOEXEC);
	if (fd < 0) {
		fprintf(stderr, "dynscope: failed to create shm buffer: %s\n", strerror(errno));
		return;
	}
	if (ftruncate(fd, (off_t)size) < 0) {
		fprintf(stderr, "dynscope: failed to size shm buffer: %s\n", strerror(errno));
		close(fd);
		return;
	}
	void *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (data == MAP_FAILED) {
		fprintf(stderr, "dynscope: failed to map shm buffer: %s\n", strerror(errno));
		close(fd);
		return;
	}
	memset(data, 0, size);

	struct wl_shm_pool *pool = wl_shm_create_pool(host->shm, fd, (int32_t)size);
	struct wl_buffer *buffer = wl_shm_pool_create_buffer(pool, 0, width, height, width * 4, WL_SHM_FORMAT_XRGB8888);
	wl_buffer_add_listener(buffer, &buffer_listener, host);

	host->shm_fd = fd;
	host->data = data;
	host->size = size;
	host->pool = pool;
	host->buffer = buffer;
	host->width = width;
	host->height = height;
}

static void xdg_surface_handle_configure(void *data, struct xdg_surface *xdg_surface, uint32_t serial) {
	struct host *host = data;
	xdg_surface_ack_configure(xdg_surface, serial);

	if (host->frame != NULL) {
		wl_callback_destroy(host->frame);
		host->frame = NULL;
	}

	int width = host->pending_width > 0 ? host->pending_width : DEFAULT_WIDTH;
	int height = host->pending_height > 0 ? host->pending_height : DEFAULT_HEIGHT;
	host_ensure_buffer(host, width, height);
	if (host->buffer == NULL)
		return;

	wl_surface_attach(host->surface, host->buffer, 0, 0);
	wl_surface_damage(host->surface, 0, 0, INT32_MAX, INT32_MAX);
	host->frame = wl_surface_frame(host->surface);
	wl_callback_add_listener(host->frame, &frame_listener, host);
	wl_surface_commit(host->surface);
}

static const struct xdg_surface_listener xdg_surface_listener = {
	.configure = xdg_surface_handle_configure,
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
	host->shm_fd = -1;
	ds->host = host;

	host->display = wl_display_connect(NULL);
	if (host->display == NULL) {
		fprintf(stderr, "dynscope: failed to connect to host Wayland compositor\n");
		goto fail;
	}

	host->registry = wl_display_get_registry(host->display);
	wl_registry_add_listener(host->registry, &registry_listener, host);
	wl_display_roundtrip(host->display);

	if (host->compositor == NULL || host->wm_base == NULL || host->shm == NULL) {
		fprintf(stderr, "dynscope: host compositor is missing required Wayland globals\n");
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
	if (host->fd_src != NULL)
		wl_event_source_remove(host->fd_src);
	if (host->flush_src != NULL)
		wl_event_source_remove(host->flush_src);
	if (host->buffer != NULL)
		wl_buffer_destroy(host->buffer);
	if (host->pool != NULL)
		wl_shm_pool_destroy(host->pool);
	if (host->data != NULL)
		munmap(host->data, host->size);
	if (host->shm_fd >= 0)
		close(host->shm_fd);
	if (host->toplevel != NULL)
		xdg_toplevel_destroy(host->toplevel);
	if (host->xdg_surface != NULL)
		xdg_surface_destroy(host->xdg_surface);
	if (host->surface != NULL)
		wl_surface_destroy(host->surface);
	if (host->wm_base != NULL)
		xdg_wm_base_destroy(host->wm_base);
	if (host->shm != NULL)
		wl_shm_destroy(host->shm);
	if (host->compositor != NULL)
		wl_compositor_destroy(host->compositor);
	if (host->registry != NULL)
		wl_registry_destroy(host->registry);
	if (host->display != NULL)
		wl_display_disconnect(host->display);
	free(host);
}
