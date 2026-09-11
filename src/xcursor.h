#ifndef XCURSOR_H
#define XCURSOR_H

#include "dynscope.h"

struct server;

int xcursor_init(struct server *server);
void xcursor_finish(struct server *server);
void xcursor_refresh(struct server *server);

#endif
