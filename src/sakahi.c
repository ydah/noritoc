#include "sakahi.h"

#include <string.h>

typedef struct {
  const char *name;
  size_t nlen;
  Value value;
} Binding;

struct Sakahi {
  Sakahi *oya;
  bool transparent;
  Binding *items;
  size_t len;
  size_t cap;
};

Sakahi *sakahi_new(Sakahi *oya) {
  Sakahi *env = nrt_xcalloc(1, sizeof(*env));
  env->oya = oya;
  return env;
}

Sakahi *sakahi_new_transparent(Sakahi *oya) {
  Sakahi *env = sakahi_new(oya);
  env->transparent = true;
  return env;
}

static Binding *find_local(Sakahi *env, const char *name, size_t nlen) {
  for (size_t i = 0; i < env->len; i++) {
    if (env->items[i].nlen == nlen && strncmp(env->items[i].name, name, nlen) == 0) {
      return &env->items[i];
    }
  }
  return NULL;
}

bool sakahi_get(Sakahi *env, const char *name, size_t nlen, Value *out) {
  for (Sakahi *cur = env; cur != NULL; cur = cur->oya) {
    Binding *binding = find_local(cur, name, nlen);
    if (binding != NULL) {
      *out = binding->value;
      return true;
    }
  }
  return false;
}

void sakahi_set_local(Sakahi *env, const char *name, size_t nlen, Value value) {
  Binding *binding = find_local(env, name, nlen);
  if (binding != NULL) {
    binding->value = value;
    return;
  }
  if (env->len == env->cap) {
    env->cap = env->cap == 0 ? 8 : env->cap * 2;
    env->items = nrt_xrealloc(env->items, env->cap * sizeof(env->items[0]));
  }
  env->items[env->len].name = name;
  env->items[env->len].nlen = nlen;
  env->items[env->len].value = value;
  env->len++;
}

void sakahi_assign(Sakahi *env, const char *name, size_t nlen, Value value) {
  Binding *binding = find_local(env, name, nlen);
  if (binding != NULL) {
    binding->value = value;
    return;
  }
  if (env->transparent && env->oya != NULL) {
    sakahi_assign(env->oya, name, nlen, value);
    return;
  }
  sakahi_set_local(env, name, nlen, value);
}
