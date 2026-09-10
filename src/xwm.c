#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include <wlr/render/pass.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/xwayland/xwayland.h>

#include "dynscope.h"
#include "server.h"
#include "xwm.h"

#define MAX_DRAWN_SURFACES 64

struct xwindow {
	struct xwm *xwm;
	struct wlr_xwayland_surface *xs;
	struct wl_listener destroy;
	struct wl_listener map_request;
	struct wl_listener associate;
	struct wl_listener dissociate;
	struct wl_listener request_activate;
	struct wl_list link;
};

struct xwm {
	struct server *server;
	struct wl_listener new_surface;
	struct wl_list windows;
	struct wlr_xwayland_surface *game;
};

static void keyboard_focus(struct xwm *xwm, struct wlr_xwayland_surface *xs) {
	if (xs->surface == NULL)
		return;
	wlr_xwayland_surface_activate(xs, true);
	wlr_seat_keyboard_notify_enter(xwm->server->seat, xs->surface, NULL, 0, NULL);
}

static struct wlr_xwayland_surface *pick_focus(struct xwm *xwm) {
	struct xwindow *win;
	struct wlr_xwayland_surface *last = NULL;
	wl_list_for_each_reverse(win, &xwm->windows, link) {
		struct wlr_xwayland_surface *xs = win->xs;
		if (xs->override_redirect)
			continue;
		if (last == NULL)
			last = xs;
		if (xs->surface != NULL && xs->surface->mapped)
			return xs;
	}
	return last;
}

static void claim_focus(struct xwm *xwm, struct wlr_xwayland_surface *xs) {
	if (xs->override_redirect)
		return;
	xwm->game = xs;
	keyboard_focus(xwm, xs);
}

static void handle_destroy(struct wl_listener *listener, void *data) {
	struct xwindow *win = wl_container_of(listener, win, destroy);
	struct xwm *xwm = win->xwm;
	struct wlr_xwayland_surface *xs = data;
	(void)xs;

	wl_list_remove(&win->destroy.link);
	wl_list_remove(&win->map_request.link);
	wl_list_remove(&win->associate.link);
	wl_list_remove(&win->dissociate.link);
	wl_list_remove(&win->request_activate.link);
	wl_list_remove(&win->link);
	free(win);

	if (xwm->game == xs) {
		xwm->game = pick_focus(xwm);
		if (xwm->game != NULL)
			keyboard_focus(xwm, xwm->game);
	}
}

static void handle_map_request(struct wl_listener *listener, void *data) {
	struct xwindow *win = wl_container_of(listener, win, map_request);
	struct wlr_xwayland_surface *xs = win->xs;
	(void)data;
	struct xwm *xwm = win->xwm;

	if (xs->override_redirect)
		return;

	wlr_xwayland_surface_configure(xs, 0, 0, xwm->server->output->width, xwm->server->output->height);
	wlr_xwayland_surface_restack(xs, NULL, XCB_STACK_MODE_ABOVE);
	claim_focus(xwm, xs);
}

static void handle_associate(struct wl_listener *listener, void *data) {
	struct xwindow *win = wl_container_of(listener, win, associate);
	struct wlr_xwayland_surface *xs = win->xs;
	struct xwm *xwm = win->xwm;

	(void)data;
	if (xwm->game == xs)
		keyboard_focus(xwm, xs);
}

static void handle_dissociate(struct wl_listener *listener, void *data) {
	(void)listener;
	(void)data;
}

static void handle_request_activate(struct wl_listener *listener, void *data) {
	struct xwindow *win = wl_container_of(listener, win, request_activate);
	struct wlr_xwayland_surface *xs = win->xs;
	(void)data;
	claim_focus(win->xwm, xs);
}

static void handle_new_surface(struct wl_listener *listener, void *data) {
	struct xwm *xwm = wl_container_of(listener, xwm, new_surface);
	struct wlr_xwayland_surface *xs = data;

	struct xwindow *win = calloc(1, sizeof(*win));
	if (win == NULL)
		return;
	win->xwm = xwm;
	win->xs = xs;
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
	wl_list_insert(xwm->windows.prev, &win->link);
}

static void draw_surface_tree(struct wlr_render_pass *pass, struct wlr_surface *surface, int x, int y, struct wlr_surface **drawn, int *ndrawn) {
	struct wlr_subsurface *sub;
	wl_list_for_each(sub, &surface->current.subsurfaces_below, current.link)
		draw_surface_tree(pass, sub->surface, x + sub->current.x, y + sub->current.y, drawn, ndrawn);

	struct wlr_texture *texture = wlr_surface_get_texture(surface);
	if (texture != NULL) {
		struct wlr_render_texture_options options = {
			.texture = texture,
			.dst_box = {x, y, surface->current.width, surface->current.height},
		};
		wlr_render_pass_add_texture(pass, &options);
	}
	if (*ndrawn < MAX_DRAWN_SURFACES)
		drawn[(*ndrawn)++] = surface;

	wl_list_for_each(sub, &surface->current.subsurfaces_above, current.link)
		draw_surface_tree(pass, sub->surface, x + sub->current.x, y + sub->current.y, drawn, ndrawn);
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
		free(win);
	}
	free(xwm);
}

void xwm_draw(struct server *server, struct wlr_render_pass *pass, int width, int height) {
	struct xwm *xwm = server->xwm;
	if (xwm == NULL)
		return;

	struct wlr_surface *drawn[MAX_DRAWN_SURFACES];
	int ndrawn = 0;

	if (xwm->game != NULL && xwm->game->surface != NULL && xwm->game->surface->mapped) {
		struct wlr_surface *surface = xwm->game->surface;
		int dx = (width - surface->current.width) / 2;
		int dy = (height - surface->current.height) / 2;
		draw_surface_tree(pass, surface, dx, dy, drawn, &ndrawn);
	}

	struct xwindow *win;
	wl_list_for_each(win, &xwm->windows, link) {
		struct wlr_xwayland_surface *xs = win->xs;
		if (!xs->override_redirect || xs->surface == NULL || !xs->surface->mapped)
			continue;
		draw_surface_tree(pass, xs->surface, xs->x, xs->y, drawn, &ndrawn);
	}

	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);
	for (int i = 0; i < ndrawn; i++)
		wlr_surface_send_frame_done(drawn[i], &now);
}
