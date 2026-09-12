#ifndef HOST_H
#define HOST_H

#include <stdbool.h>
#include <stddef.h>

struct dynscope;

int host_open(struct dynscope *ds);
void host_close(struct dynscope *ds);
void host_flush(struct dynscope *ds);

void host_set_title(struct dynscope *ds, const char *title);
void host_request_frame(struct dynscope *ds);
void host_set_cursor(struct dynscope *ds, const void *pixels, int width, int height, int hotspot_x, int hotspot_y);
void host_apply_cursor(struct dynscope *ds);
void host_set_cursor_hidden(struct dynscope *ds);
void host_set_locked(struct dynscope *ds, bool locked);
void host_set_selection(struct dynscope *ds, bool primary, const char *data, size_t len);
double host_get_scale(struct dynscope *ds);
void host_set_initial_size(struct dynscope *ds, int width, int height);

#endif
