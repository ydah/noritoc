#ifndef NORITO_SAKAHI_H
#define NORITO_SAKAHI_H

#include "common.h"

typedef enum {
  V_KAZU,
  V_KOTO
} ValKind;

typedef struct {
  ValKind kind;
  int64_t kazu;
  const char *koto;
  size_t koto_len;
} Value;

typedef struct Sakahi Sakahi;

Sakahi *sakahi_new(Sakahi *oya);
Sakahi *sakahi_new_transparent(Sakahi *oya);
bool sakahi_get(Sakahi *env, const char *name, size_t nlen, Value *out);
void sakahi_set_local(Sakahi *env, const char *name, size_t nlen, Value value);
void sakahi_assign(Sakahi *env, const char *name, size_t nlen, Value value);

#endif
