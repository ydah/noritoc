#ifndef NORITO_COMMON_H
#define NORITO_COMMON_H

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

void *nrt_xmalloc(size_t n);
void *nrt_xcalloc(size_t count, size_t size);
void *nrt_xrealloc(void *ptr, size_t n);
char *nrt_xstrndup(const char *s, size_t n);

bool nrt_starts_with(const char *s, const char *prefix);
size_t nrt_u8_len(unsigned char c);
void nrt_fatal_at(int line, int col, const char *fmt, ...);
void nrt_warn_at(const char *path, int line, int col, const char *fmt, ...);

#endif
