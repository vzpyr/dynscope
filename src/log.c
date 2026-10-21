#include <stdio.h>

#include "dynscope.h"

#define DYNSCOPE_LOG_PATH "/tmp/dynscope.log"

static FILE *log_file;

void dynscope_log_open(void) {
  if (log_file != NULL)
    return;

  FILE *file = fopen(DYNSCOPE_LOG_PATH, "w");
  if (file == NULL) {
    log_file = stderr;
    return;
  }

  setvbuf(file, NULL, _IOLBF, 0);
  log_file = file;
}

void dynscope_log_close(void) {
  if (log_file != NULL && log_file != stderr)
    fclose(log_file);
  log_file = NULL;
}

void dynscope_log_vprintf(const char *fmt, va_list args) {
  FILE *out = log_file != NULL ? log_file : stderr;
  vfprintf(out, fmt, args);
}

void dynscope_log_printf(const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  dynscope_log_vprintf(fmt, args);
  va_end(args);
}
