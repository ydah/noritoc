#include "kazohi.h"

#include "sakahi.h"

#include <string.h>

typedef struct {
  const Node *node;
} WazaEntry;

typedef struct {
  const Program *program;
  WazaEntry *waza;
  size_t nwaza;
  size_t cwaza;
  Sakahi *global;
} Runtime;

typedef struct {
  bool has_return;
  Value value;
} ExecResult;

static Value eval_expr(Runtime *rt, Sakahi *env, const Node *node);
static ExecResult exec_array(Runtime *rt, Sakahi *env, const NodeArray *array);

static Value value_kazu(int64_t v) {
  Value value;
  memset(&value, 0, sizeof(value));
  value.kind = V_KAZU;
  value.kazu = v;
  return value;
}

static Value value_koto(const char *s, size_t n) {
  Value value;
  memset(&value, 0, sizeof(value));
  value.kind = V_KOTO;
  value.koto = s;
  value.koto_len = n;
  return value;
}

static int64_t as_kazu(const Node *node, Value value, const char *context) {
  if (value.kind != V_KAZU) {
    nrt_fatal_at(node->line, node->col, "%sには数が要ります", context);
  }
  return value.kazu;
}

static bool name_eq(KiName name, const char *s, size_t n) {
  return name.n == n && strncmp(name.s, s, n) == 0;
}

static const Node *find_waza(Runtime *rt, KiName name) {
  for (size_t i = 0; i < rt->nwaza; i++) {
    const Node *node = rt->waza[i].node;
    if (name_eq(node->u.waza.name, name.s, name.n)) {
      return node;
    }
  }
  return NULL;
}

static void register_waza(Runtime *rt, const Node *node) {
  if (rt->nwaza == rt->cwaza) {
    rt->cwaza = rt->cwaza == 0 ? 8 : rt->cwaza * 2;
    rt->waza = nrt_xrealloc(rt->waza, rt->cwaza * sizeof(rt->waza[0]));
  }
  if (find_waza(rt, node->u.waza.name) != NULL) {
    nrt_fatal_at(node->line, node->col, "同じ名の業が重なっています");
  }
  rt->waza[rt->nwaza++].node = node;
}

static bool value_truth(Runtime *rt, Sakahi *env, const Node *node) {
  Value value = eval_expr(rt, env, node);
  return as_kazu(node, value, "述語") != 0;
}

static Value call_waza(Runtime *rt, Sakahi *caller_env, const Node *call_node) {
  const Node *waza = find_waza(rt, call_node->u.call.name);
  if (waza == NULL) {
    nrt_fatal_at(call_node->line, call_node->col, "業「%.*s」がありません",
                 (int)call_node->u.call.name.n, call_node->u.call.name.s);
  }
  if (waza->u.waza.params.len != call_node->u.call.args.len) {
    nrt_fatal_at(call_node->line, call_node->col,
                 "業「%.*s」の引数の数が違います",
                 (int)call_node->u.call.name.n, call_node->u.call.name.s);
  }

  Sakahi *local = sakahi_new(rt->global);
  for (size_t i = 0; i < call_node->u.call.args.len; i++) {
    Value arg = eval_expr(rt, caller_env, call_node->u.call.args.items[i]);
    KiName param = waza->u.waza.params.items[i];
    sakahi_set_local(local, param.s, param.n, arg);
  }

  ExecResult result = exec_array(rt, local, &waza->u.waza.body);
  if (result.has_return) {
    return result.value;
  }
  return value_kazu(0);
}

static Value eval_expr(Runtime *rt, Sakahi *env, const Node *node) {
  switch (node->kind) {
    case N_KAZU:
      return value_kazu(node->u.kazu);
    case N_KOTO:
      return value_koto(node->u.koto.s, node->u.koto.n);
    case N_NA: {
      Value value;
      if (!sakahi_get(env, node->u.na.s, node->u.na.n, &value)) {
        nrt_fatal_at(node->line, node->col, "名「%.*s」は未定義です",
                     (int)node->u.na.n, node->u.na.s);
      }
      return value;
    }
    case N_ADD:
    case N_SUB:
    case N_MUL:
    case N_DIV:
    case N_MOD:
    case N_EQ:
    case N_GT:
    case N_LT:
    case N_LTE: {
      Value lv = eval_expr(rt, env, node->u.bin.lhs);
      Value rv = eval_expr(rt, env, node->u.bin.rhs);
      if (node->kind == N_EQ && lv.kind == V_KOTO && rv.kind == V_KOTO) {
        bool eq = lv.koto_len == rv.koto_len &&
                  strncmp(lv.koto, rv.koto, lv.koto_len) == 0;
        return value_kazu(eq ? 1 : 0);
      }
      int64_t l = as_kazu(node->u.bin.lhs, lv, "式");
      int64_t r = as_kazu(node->u.bin.rhs, rv, "式");
      switch (node->kind) {
        case N_ADD:
          return value_kazu(l + r);
        case N_SUB:
          return value_kazu(l - r);
        case N_MUL:
          return value_kazu(l * r);
        case N_DIV:
          if (r == 0) {
            nrt_fatal_at(node->line, node->col, "零で割りました");
          }
          return value_kazu(l / r);
        case N_MOD:
          if (r == 0) {
            nrt_fatal_at(node->line, node->col, "零で割りました");
          }
          return value_kazu(l % r);
        case N_EQ:
          return value_kazu(l == r ? 1 : 0);
        case N_GT:
          return value_kazu(l > r ? 1 : 0);
        case N_LT:
          return value_kazu(l < r ? 1 : 0);
        case N_LTE:
          return value_kazu(l <= r ? 1 : 0);
        default:
          break;
      }
      break;
    }
    case N_ISZERO: {
      Value value = eval_expr(rt, env, node->u.un.x);
      return value_kazu(as_kazu(node->u.un.x, value, "述語") == 0 ? 1 : 0);
    }
    case N_NOT:
      return value_kazu(value_truth(rt, env, node->u.un.x) ? 0 : 1);
    case N_APPLY:
      return call_waza(rt, env, node);
    default:
      nrt_fatal_at(node->line, node->col, "式ではない木を値にしようとしました");
  }
  nrt_fatal_at(node->line, node->col, "値を作れません");
  return value_kazu(0);
}

static void print_value(Value value) {
  if (value.kind == V_KOTO) {
    printf("%.*s\n", (int)value.koto_len, value.koto);
    return;
  }
  printf("%" PRId64 "\n", value.kazu);
}

static ExecResult exec_node(Runtime *rt, Sakahi *env, const Node *node) {
  ExecResult none;
  memset(&none, 0, sizeof(none));

  switch (node->kind) {
    case N_WAZA:
      return none;
    case N_OKU: {
      Value value = eval_expr(rt, env, node->u.assign.expr);
      sakahi_assign(env, node->u.assign.name.s, node->u.assign.name.n, value);
      return none;
    }
    case N_ADDSET:
    case N_SUBSET: {
      Value current;
      if (!sakahi_get(env, node->u.assign.name.s, node->u.assign.name.n, &current)) {
        nrt_fatal_at(node->line, node->col, "名「%.*s」は未定義です",
                     (int)node->u.assign.name.n, node->u.assign.name.s);
      }
      int64_t base = as_kazu(node, current, "増減");
      int64_t delta = as_kazu(node->u.assign.expr,
                              eval_expr(rt, env, node->u.assign.expr), "増減");
      sakahi_assign(env, node->u.assign.name.s, node->u.assign.name.n,
                    value_kazu(node->kind == N_ADDSET ? base + delta : base - delta));
      return none;
    }
    case N_NORU:
      print_value(eval_expr(rt, env, node->u.un.x));
      return none;
    case N_KAHESU: {
      ExecResult result;
      memset(&result, 0, sizeof(result));
      result.has_return = true;
      result.value = eval_expr(rt, env, node->u.un.x);
      return result;
    }
    case N_OKONAFU:
      (void)eval_expr(rt, env, node->u.un.x);
      return none;
    case N_IF:
      for (size_t i = 0; i < node->u.iff.branches.len; i++) {
        const Branch *branch = &node->u.iff.branches.items[i];
        if (branch->cond == NULL || value_truth(rt, env, branch->cond)) {
          return exec_array(rt, env, &branch->body);
        }
      }
      return none;
    case N_FOR: {
      int64_t from = as_kazu(node->u.forr.from, eval_expr(rt, env, node->u.forr.from),
                             "反復の始め");
      int64_t to = as_kazu(node->u.forr.to, eval_expr(rt, env, node->u.forr.to),
                           "反復の終り");
      int64_t step = as_kazu(node->u.forr.step, eval_expr(rt, env, node->u.forr.step),
                             "反復の歩幅");
      if (step <= 0) {
        nrt_fatal_at(node->u.forr.step->line, node->u.forr.step->col,
                     "反復の歩幅は正でなければなりません");
      }
      const char *iter_name = node->u.forr.named_iter ? node->u.forr.iter.s : "その数";
      size_t iter_len = node->u.forr.named_iter ? node->u.forr.iter.n : strlen("その数");
      Sakahi *loop_env = sakahi_new_transparent(env);
      if (from <= to) {
        for (int64_t i = from; i <= to; i += step) {
          sakahi_set_local(loop_env, iter_name, iter_len, value_kazu(i));
          ExecResult result = exec_array(rt, loop_env, &node->u.forr.body);
          if (result.has_return) {
            return result;
          }
          if (INT64_MAX - i < step) {
            break;
          }
        }
      } else {
        for (int64_t i = from; i >= to; i -= step) {
          sakahi_set_local(loop_env, iter_name, iter_len, value_kazu(i));
          ExecResult result = exec_array(rt, loop_env, &node->u.forr.body);
          if (result.has_return) {
            return result;
          }
          if (i < INT64_MIN + step) {
            break;
          }
        }
      }
      return none;
    }
    case N_WHILE:
      while (value_truth(rt, env, node->u.whil.cond)) {
        ExecResult result = exec_array(rt, env, &node->u.whil.body);
        if (result.has_return) {
          return result;
        }
      }
      return none;
    default:
      nrt_fatal_at(node->line, node->col, "実行できない木があります");
  }
  return none;
}

static ExecResult exec_array(Runtime *rt, Sakahi *env, const NodeArray *array) {
  ExecResult none;
  memset(&none, 0, sizeof(none));
  for (size_t i = 0; i < array->len; i++) {
    ExecResult result = exec_node(rt, env, array->items[i]);
    if (result.has_return) {
      return result;
    }
  }
  return none;
}

void kazohi_run(const Program *program) {
  Runtime rt;
  memset(&rt, 0, sizeof(rt));
  rt.program = program;
  rt.global = sakahi_new(NULL);

  size_t main_count = 0;
  const Node *first_zero_arg = NULL;
  for (size_t i = 0; i < program->items.len; i++) {
    const Node *node = program->items.items[i];
    if (node->kind == N_WAZA) {
      register_waza(&rt, node);
      if (first_zero_arg == NULL && node->u.waza.params.len == 0) {
        first_zero_arg = node;
      }
    } else {
      main_count++;
    }
  }

  for (size_t i = 0; i < program->items.len; i++) {
    const Node *node = program->items.items[i];
    if (node->kind != N_WAZA) {
      (void)exec_node(&rt, rt.global, node);
    }
  }

  if (main_count == 0 && first_zero_arg != NULL) {
    Node call;
    memset(&call, 0, sizeof(call));
    call.kind = N_APPLY;
    call.line = first_zero_arg->line;
    call.col = first_zero_arg->col;
    call.u.call.name = first_zero_arg->u.waza.name;
    (void)call_waza(&rt, rt.global, &call);
  }
}
