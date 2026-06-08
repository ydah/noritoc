#include "common.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

void *nrt_xmalloc(size_t n) {
  void *ptr = malloc(n == 0 ? 1 : n);
  if (ptr == NULL) {
    fprintf(stderr, "過ち有り：メモリを得られません\n");
    exit(1);
  }
  return ptr;
}

void *nrt_xcalloc(size_t count, size_t size) {
  void *ptr = calloc(count == 0 ? 1 : count, size == 0 ? 1 : size);
  if (ptr == NULL) {
    fprintf(stderr, "過ち有り：メモリを得られません\n");
    exit(1);
  }
  return ptr;
}

void *nrt_xrealloc(void *ptr, size_t n) {
  void *next = realloc(ptr, n == 0 ? 1 : n);
  if (next == NULL) {
    fprintf(stderr, "過ち有り：メモリを得られません\n");
    exit(1);
  }
  return next;
}

char *nrt_xstrndup(const char *s, size_t n) {
  char *out = nrt_xmalloc(n + 1);
  memcpy(out, s, n);
  out[n] = '\0';
  return out;
}

bool nrt_starts_with(const char *s, const char *prefix) {
  size_t n = strlen(prefix);
  return strncmp(s, prefix, n) == 0;
}

size_t nrt_u8_len(unsigned char c) {
  if ((c & 0x80u) == 0) {
    return 1;
  }
  if ((c & 0xE0u) == 0xC0u) {
    return 2;
  }
  if ((c & 0xF0u) == 0xE0u) {
    return 3;
  }
  if ((c & 0xF8u) == 0xF0u) {
    return 4;
  }
  return 1;
}

void nrt_fatal_at(int line, int col, const char *fmt, ...) {
  fprintf(stderr, "過ち有り：");
  va_list ap;
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);
  if (line > 0 && col > 0) {
    fprintf(stderr, "（%d:%d）", line, col);
  }
  fputc('\n', stderr);
  exit(1);
}

void nrt_warn_at(const char *path, int line, int col, const char *fmt, ...) {
  fprintf(stderr, "警め：");
  if (path != NULL) {
    fprintf(stderr, "%s:", path);
  }
  if (line > 0 && col > 0) {
    fprintf(stderr, "%d:%d: ", line, col);
  }
  va_list ap;
  va_start(ap, fmt);
  vfprintf(stderr, fmt, ap);
  va_end(ap);
  fputc('\n', stderr);
}
