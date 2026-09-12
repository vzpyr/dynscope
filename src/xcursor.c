#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <xcb/xcb.h>
#include <xcb/xfixes.h>

#include <drm_fourcc.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/render/wlr_texture.h>

#include "dynscope.h"
#include "host.h"
#include "server.h"
#include "xcursor.h"

#define XCURSOR_MAX 256


static void xcursor_clear_texture(struct xcursor *xc) {
	if (xc->tex_valid)
		wlr_texture_destroy(xc->texture);
	xc->texture = NULL;
	xc->tex_valid = false;
	xc->tex_w = 0;
	xc->tex_h = 0;
}

static void xcursor_update_texture(struct xcursor *xc, xcb_xfixes_get_cursor_image_reply_t *img) {
	struct server *s = xc->server;

	int w = img->width;
	int h = img->height;
	if (w <= 0 || h <= 0 || w > XCURSOR_MAX || h > XCURSOR_MAX) {
		xcursor_clear_texture(xc);
		return;
	}

	uint32_t *pixels = malloc((size_t)w * (size_t)h * 4);
	if (pixels == NULL)
		return;
	uint32_t *src = xcb_xfixes_get_cursor_image_cursor_image(img);
	bool any_visible = false;
	for (int i = 0; i < w * h; i++) {
		uint32_t argb = src[i];
		uint32_t a = (argb >> 24) & 0xFF;
		if (a != 0)
			any_visible = true;
		uint32_t r = (argb >> 16) & 0xFF;
		uint32_t g = (argb >> 8) & 0xFF;
		uint32_t b = argb & 0xFF;
		if (a > 0) {
			r = r * a / 0xFF;
			g = g * a / 0xFF;
			b = b * a / 0xFF;
		}
		pixels[i] = (a << 24) | (r << 16) | (g << 8) | b;
	}

	if (!any_visible) {
		free(pixels);
		xcursor_clear_texture(xc);
		dynscope_log_debug("dynscope: game cursor hidden (empty image)\n");
		s->cursor_image_empty = true;
		host_set_cursor_hidden(s->ds);
		server_update_lock(s);
		return;
	}

	if (xc->tex_valid)
		wlr_texture_destroy(xc->texture);
	struct wlr_texture *tex = wlr_texture_from_pixels(s->renderer, DRM_FORMAT_ARGB8888, w * 4, w, h, pixels);
	if (tex == NULL) {
		free(pixels);
		return;
	}
	xc->texture = tex;
	xc->tex_w = w;
	xc->tex_h = h;
	xc->tex_hot_x = img->xhot;
	xc->tex_hot_y = img->yhot;
	xc->tex_valid = true;
	s->cursor_image_empty = false;

	double scale = host_get_scale(s->ds);
	int out_w = (int)((double)w * scale + 0.5);
	int out_h = (int)((double)h * scale + 0.5);
	if (out_w < 1)
		out_w = 1;
	if (out_h < 1)
		out_h = 1;
	if (out_w > 256 || out_h > 256) {
		free(pixels);
		host_set_cursor(s->ds, NULL, 0, 0, 0, 0);
		return;
	}

	uint32_t *scaled = malloc((size_t)out_w * (size_t)out_h * 4);
	if (scaled == NULL) {
		free(pixels);
		return;
	}
	for (int y = 0; y < out_h; y++) {
		int src_y = (int)((double)y / scale);
		if (src_y >= h)
			src_y = h - 1;
		for (int x = 0; x < out_w; x++) {
			int src_x = (int)((double)x / scale);
			if (src_x >= w)
				src_x = w - 1;
			scaled[y * out_w + x] = pixels[src_y * w + src_x];
		}
	}
	free(pixels);

	int hx = (int)((double)img->xhot * scale + 0.5);
	int hy = (int)((double)img->yhot * scale + 0.5);
	if (hx >= out_w)
		hx = out_w - 1;
	if (hy >= out_h)
		hy = out_h - 1;
	host_set_cursor(s->ds, scaled, out_w, out_h, hx, hy);
	free(scaled);
	server_update_lock(s);

	dynscope_log_debug("dynscope: game cursor %dx%d hotspot=%d,%d scale=%.2f\n", w, h, img->xhot, img->yhot, scale);
}

void xcursor_refresh(struct server *server) {
	struct xcursor *xc = server->xcursor;
	if (xc == NULL || xc->conn == NULL)
		return;

	xcb_xfixes_get_cursor_image_cookie_t cookie = xcb_xfixes_get_cursor_image_unchecked(xc->conn);
	xcb_generic_error_t *error = NULL;
	xcb_xfixes_get_cursor_image_reply_t *img = xcb_xfixes_get_cursor_image_reply(xc->conn, cookie, &error);
	if (error != NULL) {
		free(error);
		return;
	}
	if (img == NULL)
		return;
	xcursor_update_texture(xc, img);
	free(img);
}

static int xcursor_fd_event(int fd, uint32_t mask, void *data) {
	struct xcursor *xc = data;
	(void)fd;
	(void)mask;

	if (xc->conn == NULL)
		return 0;

	xcb_generic_event_t *event;
	bool got_notify = false;
	while ((event = xcb_poll_for_event(xc->conn)) != NULL) {
		if (xc->fixes_event_base > 0 && event->response_type == xc->fixes_event_base + XCB_XFIXES_CURSOR_NOTIFY)
			got_notify = true;
		free(event);
	}
	if (got_notify)
		xcursor_refresh(xc->server);
	return 1;
}

static void xcursor_disconnect(struct xcursor *xc) {
	if (xc->fd_src != NULL) {
		wl_event_source_remove(xc->fd_src);
		xc->fd_src = NULL;
	}
	xcursor_clear_texture(xc);
	if (xc->conn != NULL) {
		xcb_disconnect(xc->conn);
		xc->conn = NULL;
	}
}

int xcursor_init(struct server *server) {
	struct xcursor *xc = calloc(1, sizeof(*xc));
	if (xc == NULL)
		return -1;
	xc->server = server;
	server->xcursor = xc;

	fprintf(stderr, "dynscope: xcursor connecting to %s\n", server->xwayland->display_name);
	xc->conn = xcb_connect(server->xwayland->display_name, NULL);
	if (xcb_connection_has_error(xc->conn)) {
		fprintf(stderr, "dynscope: failed to connect to Xwayland for cursor tracking\n");
		xcursor_disconnect(xc);
		return -1;
	}

	xc->screen = xcb_setup_roots_iterator(xcb_get_setup(xc->conn)).data;
	if (xc->screen == NULL) {
		fprintf(stderr, "dynscope: failed to get X screen for cursor tracking\n");
		xcursor_disconnect(xc);
		return -1;
	}

	const xcb_query_extension_reply_t *ext = xcb_get_extension_data(xc->conn, &xcb_xfixes_id);
	if (ext == NULL || !ext->present) {
		fprintf(stderr, "dynscope: XFixes not available on Xwayland, cursor passthrough disabled\n");
		xcursor_disconnect(xc);
		return -1;
	}
	xc->fixes_event_base = ext->first_event;

	xcb_xfixes_query_version_cookie_t cookie = xcb_xfixes_query_version(xc->conn, 4, 0);
	xcb_xfixes_query_version_reply_t *version = xcb_xfixes_query_version_reply(xc->conn, cookie, NULL);
	if (version != NULL)
		free(version);

	xcb_xfixes_select_cursor_input(xc->conn, xc->screen->root, XCB_XFIXES_CURSOR_NOTIFY_MASK_DISPLAY_CURSOR);
	xcb_generic_error_t *err = xcb_request_check(xc->conn, xcb_xfixes_select_cursor_input_checked(xc->conn, xc->screen->root, XCB_XFIXES_CURSOR_NOTIFY_MASK_DISPLAY_CURSOR));
	if (err != NULL) {
		fprintf(stderr, "dynscope: select_cursor_input failed: %u\n", err->error_code);
		free(err);
	}
	xcb_flush(xc->conn);
	fprintf(stderr, "dynscope: xcursor init done\n");

	int fd = xcb_get_file_descriptor(xc->conn);
	xc->fd_src = wl_event_loop_add_fd(server->ds->loop, fd, WL_EVENT_READABLE, xcursor_fd_event, xc);
	if (xc->fd_src == NULL) {
		fprintf(stderr, "dynscope: failed to watch Xwayland cursor connection\n");
		xcursor_disconnect(xc);
		return -1;
	}

	xcursor_refresh(server);
	return 0;
}

void xcursor_finish(struct server *server) {
	struct xcursor *xc = server->xcursor;
	if (xc == NULL)
		return;
	server->xcursor = NULL;
	xcursor_disconnect(xc);
	free(xc);
}
