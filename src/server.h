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
#include <wlr/types/wlr_output_layout.h>
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

struct fit {
	double scale;
	double x;
	double y;
	int w;
	int h;
};

struct xcursor {
	struct wlr_texture *texture;
	int tex_w;
	int tex_h;
	int tex_hot_x;
	int tex_hot_y;
	bool tex_valid;
	struct server *server;
	xcb_connection_t *conn;
	xcb_screen_t *screen;
	uint8_t fixes_event_base;
	struct wl_event_source *fd_src;
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
	struct wlr_output_layout *layout;
	struct wlr_xwayland *xwayland;
	struct wl_listener xwayland_destroy;
	struct wl_listener xwayland_ready;
	struct xkb_context *xkb_context;
	struct wlr_keyboard keyboard;
	struct xwm *xwm;
	struct xcursor *xcursor;
	struct frame_pool pool;
	struct fit fit;
	int win_w;
	int win_h;
	struct wlr_surface *pointer_surface;
	double pointer_x;
	double pointer_y;
	bool cursor_warned;
};

int server_init(struct dynscope *ds);
void server_finish(struct dynscope *ds);
const char *server_display_name(struct dynscope *ds);

void server_present(struct dynscope *ds, int width, int height, struct frame_info *out);
void server_frame_released(struct dynscope *ds, int generation);

void server_pointer_enter(struct dynscope *ds, double host_x, double host_y);
void server_pointer_motion(struct dynscope *ds, uint32_t time_msec, double host_x, double host_y);
void server_pointer_leave(struct dynscope *ds);
void server_pointer_button(struct dynscope *ds, uint32_t time_msec, uint32_t button, uint32_t state);
void server_pointer_axis(struct dynscope *ds, uint32_t time_msec, uint32_t orientation, double value, int32_t value_discrete, uint32_t source);

void server_keyboard_keymap(struct dynscope *ds, const char *keymap_string);
void server_keyboard_key(struct dynscope *ds, uint32_t key, bool pressed);
void server_keyboard_modifiers(struct dynscope *ds, uint32_t depressed, uint32_t latched, uint32_t locked, uint32_t group);

#endif
