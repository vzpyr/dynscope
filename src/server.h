#ifndef SERVER_H
#define SERVER_H

#include <xkbcommon/xkbcommon.h>

#include <wlr/backend.h>
#include <wlr/render/allocator.h>
#include <wlr/render/egl.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/xwayland/xwayland.h>

#include "dynscope.h"

struct dynscope;

#define FRAME_POOL_MAX 4

struct frame {
	struct wlr_buffer *buffer;
	struct wlr_dmabuf_attributes attrs;
	int generation;
	bool in_flight;
};

struct frame_pool {
	struct frame frames[FRAME_POOL_MAX];
	int nframes;
	int width;
	int height;
	int next_generation;
};

struct server {
	struct dynscope *ds;
	struct wl_display *display;
	struct wlr_backend *backend;
	struct wlr_renderer *renderer;
	struct wlr_egl *egl;
	struct wlr_allocator *allocator;
	struct wlr_compositor *compositor;
	struct wlr_subcompositor *subcompositor;
	struct wlr_seat *seat;
	struct wlr_data_device_manager *data_device;
	struct wlr_output *output;
	struct wlr_xwayland *xwayland;
	struct wl_listener xwayland_destroy;
	struct wl_listener xwayland_ready;
	struct xkb_context *xkb_context;
	struct wlr_keyboard keyboard;
	struct xwm *xwm;
	struct frame_pool pool;
};

int server_init(struct dynscope *ds);
void server_finish(struct dynscope *ds);
const char *server_display_name(struct dynscope *ds);

void server_present(struct dynscope *ds, int width, int height, struct frame_info *out);
void server_frame_released(struct dynscope *ds, int generation);

void server_keyboard_keymap(struct dynscope *ds, const char *keymap_string);
void server_keyboard_key(struct dynscope *ds, uint32_t key, bool pressed);
void server_keyboard_modifiers(struct dynscope *ds, uint32_t depressed, uint32_t latched, uint32_t locked, uint32_t group);

#endif
