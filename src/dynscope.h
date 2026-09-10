#ifndef DYNSCOPE_H
#define DYNSCOPE_H

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>
#include <wayland-server-core.h>

struct child {
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

struct host;
struct server;

struct frame_info {
	int generation;
	int fd;
	uint32_t format;
	int32_t width;
	int32_t height;
	int32_t offset;
	int32_t stride;
	uint64_t modifier;
};

struct dynscope {
	struct wl_event_loop *loop;
	struct child child;
	struct host *host;
	struct server *server;
	const char *xwayland_display;
	bool running;
	bool closing;
	int exit_code;
};

void dynscope_close(struct dynscope *ds);

#endif
