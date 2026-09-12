#ifndef CLIPBOARD_H
#define CLIPBOARD_H

struct server;

int clipboard_init(struct server *server);
void clipboard_finish(struct server *server);

#endif
