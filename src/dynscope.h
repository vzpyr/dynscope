#ifndef DYNSCOPE_H
#define DYNSCOPE_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

#define DEFAULT_WIDTH 2560
#define DEFAULT_HEIGHT 1440

struct frame_info {
  int generation;
  int frame_index;
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
  int caught_signal;
  int host_width;
  int host_height;
  int host_refresh;
};

void dynscope_close(struct dynscope *ds);

static inline bool dynscope_debug_enabled(void) {
  static int enabled = -1;
  if (enabled < 0) {
    const char *env = getenv("DYNSCOPE_DEBUG");
    enabled =
        (env != NULL && strcmp(env, "0") != 0 && strcmp(env, "") != 0) ? 1 : 0;
  }
  return enabled == 1;
}

#define dynscope_log_debug(...)                                                \
  do {                                                                         \
    if (dynscope_debug_enabled())                                              \
      fprintf(stderr, __VA_ARGS__);                                            \
  } while (0)

#endif
