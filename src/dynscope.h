#ifndef DYNSCOPE_H
#define DYNSCOPE_H

#include <stdbool.h>
#include <sys/types.h>
#include <wayland-server-core.h>

struct child {
	struct wl_event_loop *loop;
	pid_t supervisor;
	pid_t game;
	int pid_fd;
	int status_fd;
	struct wl_event_source *pid_src;
	struct wl_event_source *status_src;
	struct wl_event_source *kill_timer;
	bool spawned;
	bool terminated_by_us;
	bool finished;
};

struct dynscope {
	struct wl_event_loop *loop;
	struct child child;
	struct host *host;
	bool running;
	bool closing;
	int exit_code;
};

void dynscope_close(struct dynscope *ds);

#endif
