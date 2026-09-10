#ifndef XWM_H
#define XWM_H

#include "dynscope.h"

struct wlr_render_pass;
struct server;

int xwm_init(struct server *server);
void xwm_finish(struct server *server);

void xwm_draw(struct server *server, struct wlr_render_pass *pass, int width, int height);

#endif
