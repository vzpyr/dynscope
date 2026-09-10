#ifndef HOST_H
#define HOST_H

struct dynscope;

int host_open(struct dynscope *ds);
void host_close(struct dynscope *ds);
void host_flush(struct dynscope *ds);

#endif
