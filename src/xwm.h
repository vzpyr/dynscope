#ifndef XWM_H
#define XWM_H

#include "dynscope.h"

struct server;
struct wlr_render_pass;
struct wlr_surface;

int xwm_init(struct server *server);
void xwm_finish(struct server *server);

struct wlr_surface *xwm_focus_surface(struct server *server);
void xwm_game_size(struct server *server, int *width, int *height);
void xwm_pick_surface(struct server *server, double host_x, double host_y,
                      struct wlr_surface **surface, double *out_x,
                      double *out_y);
void xwm_draw(struct server *server, struct wlr_render_pass *pass, int width,
              int height);
void xwm_surface_activate(struct server *server, struct wlr_surface *surface);
int xwm_close_windows(struct server *server);

#endif
