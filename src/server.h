#ifndef SERVER_H
#define SERVER_H

#include <pixman.h>
#include <xkbcommon/xkbcommon.h>

#include <wlr/backend.h>
#include <wlr/render/allocator.h>
#include <wlr/render/egl.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_linux_drm_syncobj_v1.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_pointer_constraints_v1.h>
#include <wlr/types/wlr_relative_pointer_v1.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/xwayland/xwayland.h>

#include "dynscope.h"

struct dynscope;
struct clipboard;

#define FRAME_POOL_MAX 8

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
  struct server *server;
  xcb_connection_t *conn;
  xcb_screen_t *screen;
  uint8_t fixes_event_base;
  struct wl_event_source *fd_src;
};

struct game_constraint {
  struct server *server;
  struct wlr_pointer_constraint_v1 *constraint;
  struct wl_listener set_region;
  struct wl_listener destroy;
  struct wl_list link;
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
  struct wl_listener output_bind;
  struct wlr_xwayland *xwayland;
  struct wl_listener xwayland_destroy;
  struct wl_listener xwayland_ready;
  struct xkb_context *xkb_context;
  struct wlr_keyboard keyboard;
  struct xwm *xwm;
  struct xcursor *xcursor;
  struct clipboard *clipboard;
  struct wlr_pointer_constraints_v1 *constraints;
  struct wlr_relative_pointer_manager_v1 *relative_pointer;
  struct wlr_linux_drm_syncobj_manager_v1 *syncobj;
  struct wl_listener new_constraint;
  struct wl_listener new_surface;
  struct wl_list game_constraints;
  struct wlr_pointer_constraint_v1 *active_constraint;
  pixman_region32_t confine;
  bool constraint_requires_warp;
  bool cursor_image_empty;
  bool host_locked;
  struct frame_pool pool;
  struct fit fit;
  struct wlr_surface *pointer_surface;
  double pointer_x;
  double pointer_y;
};

int server_init(struct dynscope *ds);
void server_finish(struct dynscope *ds);
const char *server_display_name(struct dynscope *ds);

void server_present(struct dynscope *ds, int width, int height,
                    struct frame_info *out);
void server_frame_released(struct dynscope *ds, int frame_index,
                           int generation);

void server_pointer_enter(struct dynscope *ds, double host_x, double host_y);
void server_pointer_motion(struct dynscope *ds, uint32_t time_msec,
                           double host_x, double host_y);
void server_pointer_leave(struct dynscope *ds);
void server_pointer_rel_motion(struct dynscope *ds, uint32_t time_msec,
                               uint64_t time_usec, double dx, double dy);
void server_pointer_button(struct dynscope *ds, uint32_t time_msec,
                           uint32_t button, uint32_t state);
void server_pointer_axis(struct dynscope *ds, uint32_t time_msec,
                         uint32_t orientation, double value,
                         int32_t value_discrete, uint32_t source,
                         uint32_t relative_direction);

void server_constrain_focused(struct server *server);
void server_keyboard_focus(struct dynscope *ds, bool focused);
void server_update_lock(struct server *server);

void server_keyboard_keymap(struct dynscope *ds, const char *keymap_string);
void server_keyboard_key(struct dynscope *ds, uint32_t time_msec, uint32_t key,
                         bool pressed);
void server_keyboard_modifiers(struct dynscope *ds, uint32_t depressed,
                               uint32_t latched, uint32_t locked,
                               uint32_t group);
void server_keyboard_repeat_info(struct dynscope *ds, int32_t rate,
                                 int32_t delay);

void server_host_selection(struct dynscope *ds, bool primary, const char *data,
                           size_t len);

void server_update_output_mode(struct server *server, int width, int height);
int server_request_close(struct dynscope *ds);

#endif
