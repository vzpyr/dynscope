#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_primary_selection.h>
#include <wlr/types/wlr_seat.h>

#include "dynscope.h"
#include "host.h"
#include "server.h"

#define READ_CHUNK 4096

struct clip_read {
  struct clipboard *cb;
  bool primary;
  int fd;
  char *buf;
  size_t len;
  size_t cap;
  bool dead;
  struct wl_event_source *src;
};

struct clipboard {
  struct server *server;
  struct wlr_data_source *clipboard_source;
  struct wlr_primary_selection_source *primary_source;
  char *clipboard_data;
  size_t clipboard_len;
  char *primary_data;
  size_t primary_len;
  char *pushed_clipboard;
  size_t pushed_clipboard_len;
  char *pushed_primary;
  size_t pushed_primary_len;
  bool pushing;
  struct clip_read reads[2];
  struct wl_listener request_set_selection;
  struct wl_listener request_set_primary_selection;
  struct wl_listener set_selection;
  struct wl_listener set_primary_selection;
};

static void clip_cache_set(char **data, size_t *len, const char *src,
                           size_t src_len) {
  free(*data);
  *data = NULL;
  *len = 0;
  if (src_len == 0)
    return;
  char *copy = malloc(src_len + 1);
  if (copy == NULL)
    return;
  memcpy(copy, src, src_len);
  copy[src_len] = '\0';
  *data = copy;
  *len = src_len;
}

static void clip_read_finish(struct clip_read *read) {
  if (read->dead)
    return;
  read->dead = true;
  if (read->src != NULL) {
    wl_event_source_remove(read->src);
    read->src = NULL;
  }
  close(read->fd);
  read->fd = -1;
  struct clipboard *cb = read->cb;
  char *data = read->buf;
  size_t len = read->len;
  read->buf = NULL;
  read->len = 0;
  read->cap = 0;
  bool primary = read->primary;
  if (data != NULL && len > 0) {
    char **pushed = primary ? &cb->pushed_primary : &cb->pushed_clipboard;
    size_t *pushed_len =
        primary ? &cb->pushed_primary_len : &cb->pushed_clipboard_len;
    clip_cache_set(pushed, pushed_len, data, len);
    host_set_selection(cb->server->ds, primary, data, len);
  }
  free(data);
}

static int clip_read_event(int fd, uint32_t mask, void *data) {
  struct clip_read *r = data;
  (void)fd;
  if (r->dead)
    return 0;
  if ((mask & WL_EVENT_READABLE) == 0) {
    if ((mask & (WL_EVENT_HANGUP | WL_EVENT_ERROR)) != 0)
      clip_read_finish(r);
    return 0;
  }
  while (true) {
    if (r->len + READ_CHUNK > r->cap) {
      size_t new_cap = r->cap == 0 ? READ_CHUNK : r->cap * 2;
      char *new_buf = realloc(r->buf, new_cap);
      if (new_buf == NULL)
        break;
      r->buf = new_buf;
      r->cap = new_cap;
    }
    ssize_t n = read(r->fd, r->buf + r->len, READ_CHUNK);
    if (n > 0) {
      r->len += (size_t)n;
      continue;
    }
    if (n < 0 && (errno == EAGAIN || errno == EINTR))
      return 1;
    clip_read_finish(r);
    return 0;
  }
  return 1;
}

static void clip_read_start(struct clipboard *cb, int fd, bool primary) {
  struct clip_read *r = &cb->reads[primary ? 1 : 0];
  if (r->src != NULL || r->buf != NULL) {
    close(fd);
    return;
  }
  r->cb = cb;
  r->primary = primary;
  r->fd = fd;
  r->buf = NULL;
  r->len = 0;
  r->cap = 0;
  r->dead = false;
  int flags = fcntl(fd, F_GETFL);
  if (flags >= 0)
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
  r->src = wl_event_loop_add_fd(cb->server->ds->loop, fd, WL_EVENT_READABLE,
                                clip_read_event, r);
  if (r->src == NULL)
    close(fd);
}

static const char *clip_pick_mime(struct wl_array *mime_types) {
  const char **mime;
  wl_array_for_each(mime, mime_types) {
    if (strcmp(*mime, "text/plain;charset=utf-8") == 0)
      return *mime;
  }
  wl_array_for_each(mime, mime_types) {
    if (strcmp(*mime, "text/plain") == 0)
      return *mime;
  }
  return NULL;
}

struct clip_source {
  struct clipboard *cb;
  struct wlr_data_source base;
};

static void clipboard_source_send(struct wlr_data_source *source,
                                  const char *mime_type, int32_t fd) {
  (void)mime_type;
  struct clip_source *cs = wl_container_of(source, cs, base);
  struct clipboard *cb = cs->cb;
  if (cb->clipboard_data != NULL)
    (void)write(fd, cb->clipboard_data, cb->clipboard_len);
  close(fd);
}

static void clipboard_source_destroy(struct wlr_data_source *source) {
  struct clip_source *cs = wl_container_of(source, cs, base);
  if (cs->cb->clipboard_source == source)
    cs->cb->clipboard_source = NULL;
  free(cs);
}

static void primary_source_send(struct wlr_primary_selection_source *source,
                                const char *mime_type, int fd) {
  (void)mime_type;
  struct clipboard *cb = source->data;
  if (cb->primary_data != NULL)
    (void)write(fd, cb->primary_data, cb->primary_len);
  close(fd);
}

static void
primary_source_destroy(struct wlr_primary_selection_source *source) {
  struct clipboard *cb = source->data;
  if (cb->primary_source == source)
    cb->primary_source = NULL;
  free(source);
}

static const struct wlr_data_source_impl clipboard_source_impl = {
    .send = clipboard_source_send,
    .destroy = clipboard_source_destroy,
};

static const struct wlr_primary_selection_source_impl primary_source_impl = {
    .send = primary_source_send,
    .destroy = primary_source_destroy,
};

static void clipboard_push_to_game(struct clipboard *cb, bool primary,
                                   const char *data, size_t len) {
  struct server *s = cb->server;
  if (primary) {
    clip_cache_set(&cb->primary_data, &cb->primary_len, data, len);
    struct wlr_primary_selection_source *source = calloc(1, sizeof(*source));
    if (source == NULL)
      return;
    wlr_primary_selection_source_init(source, &primary_source_impl);
    source->data = cb;
    const char *mimes[] = {"text/plain;charset=utf-8", "text/plain"};
    for (size_t i = 0; i < sizeof(mimes) / sizeof(mimes[0]); i++) {
      char **entry = wl_array_add(&source->mime_types, sizeof(char *));
      if (entry == NULL)
        break;
      *entry = strdup(mimes[i]);
    }
    if (cb->primary_source != NULL) {
      struct wlr_primary_selection_source *old = cb->primary_source;
      cb->primary_source = NULL;
      wlr_primary_selection_source_destroy(old);
    }
    cb->primary_source = source;
    wlr_seat_set_primary_selection(s->seat, source, 0);
  } else {
    clip_cache_set(&cb->clipboard_data, &cb->clipboard_len, data, len);
    struct clip_source *cs = calloc(1, sizeof(*cs));
    if (cs == NULL)
      return;
    cs->cb = cb;
    wlr_data_source_init(&cs->base, &clipboard_source_impl);
    const char *mimes[] = {"text/plain;charset=utf-8", "text/plain"};
    for (size_t i = 0; i < sizeof(mimes) / sizeof(mimes[0]); i++) {
      char **entry = wl_array_add(&cs->base.mime_types, sizeof(*entry));
      if (entry == NULL)
        break;
      *entry = strdup(mimes[i]);
    }
    if (cb->clipboard_source != NULL) {
      struct wlr_data_source *old = cb->clipboard_source;
      cb->clipboard_source = NULL;
      wlr_data_source_destroy(old);
    }
    cb->clipboard_source = &cs->base;
    wlr_seat_set_selection(s->seat, &cs->base, 0);
  }
}

static void clipboard_handle_request_set_selection(struct wl_listener *listener,
                                                   void *data) {
  struct clipboard *cb = wl_container_of(listener, cb, request_set_selection);
  struct wlr_seat_request_set_selection_event *event = data;
  wlr_seat_set_selection(cb->server->seat, event->source, event->serial);
}

static void
clipboard_handle_request_set_primary_selection(struct wl_listener *listener,
                                               void *data) {
  struct clipboard *cb =
      wl_container_of(listener, cb, request_set_primary_selection);
  struct wlr_seat_request_set_primary_selection_event *event = data;
  wlr_seat_set_primary_selection(cb->server->seat, event->source,
                                 event->serial);
}

static void clipboard_handle_set_selection(struct wl_listener *listener,
                                           void *data) {
  struct clipboard *cb = wl_container_of(listener, cb, set_selection);
  (void)data;
  if (cb->pushing)
    return;
  struct wlr_data_source *source = cb->server->seat->selection_source;
  if (source == NULL || source == cb->clipboard_source)
    return;

  const char *mime = clip_pick_mime(&source->mime_types);
  if (mime == NULL)
    return;
  int fds[2];
  if (pipe(fds) < 0)
    return;
  wlr_data_source_send(source, mime, fds[1]);
  clip_read_start(cb, fds[0], false);
}

static void clipboard_handle_set_primary_selection(struct wl_listener *listener,
                                                   void *data) {
  struct clipboard *cb = wl_container_of(listener, cb, set_primary_selection);
  (void)data;
  if (cb->pushing)
    return;
  struct wlr_primary_selection_source *source =
      cb->server->seat->primary_selection_source;
  if (source == NULL || source == cb->primary_source)
    return;

  const char *mime = clip_pick_mime(&source->mime_types);
  if (mime == NULL)
    return;
  int fds[2];
  if (pipe(fds) < 0)
    return;
  wlr_primary_selection_source_send(source, mime, fds[1]);
  clip_read_start(cb, fds[0], true);
}

void server_host_selection(struct dynscope *ds, bool primary, const char *data,
                           size_t len) {
  struct server *s = ds->server;
  if (s == NULL)
    return;
  struct clipboard *cb = s->clipboard;
  if (cb == NULL)
    return;

  char **pushed = primary ? &cb->pushed_primary : &cb->pushed_clipboard;
  size_t *pushed_len =
      primary ? &cb->pushed_primary_len : &cb->pushed_clipboard_len;
  if (*pushed != NULL && *pushed_len == len && memcmp(*pushed, data, len) == 0)
    return;
  clip_cache_set(pushed, pushed_len, data, len);

  cb->pushing = true;
  clipboard_push_to_game(cb, primary, data, len);
  cb->pushing = false;
}

int clipboard_init(struct server *server) {
  struct clipboard *cb = calloc(1, sizeof(*cb));
  if (cb == NULL)
    return -1;
  cb->server = server;
  cb->reads[0].fd = -1;
  cb->reads[1].fd = -1;

  cb->request_set_selection.notify = clipboard_handle_request_set_selection;
  wl_signal_add(&server->seat->events.request_set_selection,
                &cb->request_set_selection);

  cb->request_set_primary_selection.notify =
      clipboard_handle_request_set_primary_selection;
  wl_signal_add(&server->seat->events.request_set_primary_selection,
                &cb->request_set_primary_selection);

  cb->set_selection.notify = clipboard_handle_set_selection;
  wl_signal_add(&server->seat->events.set_selection, &cb->set_selection);

  cb->set_primary_selection.notify = clipboard_handle_set_primary_selection;
  wl_signal_add(&server->seat->events.set_primary_selection,
                &cb->set_primary_selection);

  server->clipboard = cb;
  return 0;
}

void clipboard_finish(struct server *server) {
  struct clipboard *cb = server->clipboard;
  if (cb == NULL)
    return;
  server->clipboard = NULL;
  wl_list_remove(&cb->request_set_selection.link);
  wl_list_remove(&cb->request_set_primary_selection.link);
  wl_list_remove(&cb->set_selection.link);
  wl_list_remove(&cb->set_primary_selection.link);
  for (int i = 0; i < 2; i++) {
    if (cb->reads[i].src != NULL) {
      wl_event_source_remove(cb->reads[i].src);
      cb->reads[i].src = NULL;
    }
    if (cb->reads[i].fd >= 0)
      close(cb->reads[i].fd);
    free(cb->reads[i].buf);
  }
  if (cb->clipboard_source != NULL) {
    struct wlr_data_source *source = cb->clipboard_source;
    cb->clipboard_source = NULL;
    wlr_data_source_destroy(source);
  }
  if (cb->primary_source != NULL) {
    struct wlr_primary_selection_source *source = cb->primary_source;
    cb->primary_source = NULL;
    wlr_primary_selection_source_destroy(source);
  }
  free(cb->clipboard_data);
  free(cb->primary_data);
  free(cb->pushed_clipboard);
  free(cb->pushed_primary);
  free(cb);
}
