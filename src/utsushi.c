#include "utsushi.h"

#include <string.h>

typedef struct {
  KiName *items;
  size_t len;
  size_t cap;
} NameSet;

typedef struct {
  KiName name;
  char *cell;
} NameBinding;

typedef struct {
  FILE *out;
  int indent;
  int temp_id;
  NameBinding bindings[64];
  size_t nbindings;
  char *sonokazu_stack[64];
  size_t sonokazu_depth;
} Emit;

static const char *SONOKAZU = "その数";

static bool name_equal(KiName a, KiName b) {
  return a.n == b.n && strncmp(a.s, b.s, a.n) == 0;
}

static bool is_sonokazu(KiName name) {
  return name.n == strlen(SONOKAZU) && strncmp(name.s, SONOKAZU, name.n) == 0;
}

static char hex_digit(unsigned int v) {
  return (char)(v < 10 ? '0' + v : 'a' + (v - 10));
}

static char *mangle(KiName name, const char *prefix) {
  size_t plen = strlen(prefix);
  char *out = nrt_xmalloc(plen + name.n * 2 + 1);
  memcpy(out, prefix, plen);
  for (size_t i = 0; i < name.n; i++) {
    unsigned int b = (unsigned char)name.s[i];
    out[plen + i * 2] = hex_digit((b >> 4) & 0xfu);
    out[plen + i * 2 + 1] = hex_digit(b & 0xfu);
  }
  out[plen + name.n * 2] = '\0';
  return out;
}

static void emit_indent(Emit *e) {
  for (int i = 0; i < e->indent; i++) {
    fputs("  ", e->out);
  }
}

static void emit_c_string(FILE *out, const char *s, size_t n) {
  fputc('"', out);
  for (size_t i = 0; i < n; i++) {
    unsigned char c = (unsigned char)s[i];
    switch (c) {
      case '\\':
        fputs("\\\\", out);
        break;
      case '"':
        fputs("\\\"", out);
        break;
      case '\n':
        fputs("\\n", out);
        break;
      case '\r':
        fputs("\\r", out);
        break;
      case '\t':
        fputs("\\t", out);
        break;
      default:
        if (c < 0x20) {
          fprintf(out, "\\x%02x", c);
        } else {
          fputc(c, out);
        }
        break;
    }
  }
  fputc('"', out);
}

static void names_add(NameSet *set, KiName name) {
  if (is_sonokazu(name)) {
    return;
  }
  for (size_t i = 0; i < set->len; i++) {
    if (name_equal(set->items[i], name)) {
      return;
    }
  }
  if (set->len == set->cap) {
    set->cap = set->cap == 0 ? 8 : set->cap * 2;
    set->items = nrt_xrealloc(set->items, set->cap * sizeof(set->items[0]));
  }
  set->items[set->len++] = name;
}

static bool is_hidden(KiName name, const KiNameArray *hidden) {
  for (size_t i = 0; hidden != NULL && i < hidden->len; i++) {
    if (name_equal(name, hidden->items[i])) {
      return true;
    }
  }
  return false;
}

static void collect_node(const Node *node, NameSet *set, KiNameArray *hidden);

static void collect_array(const NodeArray *array, NameSet *set, KiNameArray *hidden) {
  for (size_t i = 0; i < array->len; i++) {
    collect_node(array->items[i], set, hidden);
  }
}

static void collect_name(NameSet *set, KiNameArray *hidden, KiName name) {
  if (!is_hidden(name, hidden)) {
    names_add(set, name);
  }
}

static void collect_node(const Node *node, NameSet *set, KiNameArray *hidden) {
  switch (node->kind) {
    case N_NA:
      collect_name(set, hidden, node->u.na);
      return;
    case N_OKU:
    case N_ADDSET:
    case N_SUBSET:
      collect_name(set, hidden, node->u.assign.name);
      collect_node(node->u.assign.expr, set, hidden);
      return;
    case N_NORU:
    case N_OKONAFU:
    case N_KAHESU:
    case N_ISZERO:
    case N_NOT:
      collect_node(node->u.un.x, set, hidden);
      return;
    case N_ADD:
    case N_SUB:
    case N_MUL:
    case N_DIV:
    case N_MOD:
    case N_EQ:
    case N_GT:
    case N_LT:
    case N_LTE:
      collect_node(node->u.bin.lhs, set, hidden);
      collect_node(node->u.bin.rhs, set, hidden);
      return;
    case N_APPLY:
      collect_array(&node->u.call.args, set, hidden);
      return;
    case N_IF:
      for (size_t i = 0; i < node->u.iff.branches.len; i++) {
        if (node->u.iff.branches.items[i].cond != NULL) {
          collect_node(node->u.iff.branches.items[i].cond, set, hidden);
        }
        collect_array(&node->u.iff.branches.items[i].body, set, hidden);
      }
      return;
    case N_FOR: {
      collect_node(node->u.forr.from, set, hidden);
      collect_node(node->u.forr.to, set, hidden);
      collect_node(node->u.forr.step, set, hidden);
      KiNameArray next_hidden = hidden == NULL ? (KiNameArray){0} : *hidden;
      if (node->u.forr.named_iter) {
        ki_name_array_push(&next_hidden, node->u.forr.iter);
      }
      collect_array(&node->u.forr.body, set, &next_hidden);
      return;
    }
    case N_WHILE:
      collect_node(node->u.whil.cond, set, hidden);
      collect_array(&node->u.whil.body, set, hidden);
      return;
    case N_WAZA:
      collect_array(&node->u.waza.body, set, hidden);
      return;
    default:
      return;
  }
}

static bool name_in_params(KiName name, const KiNameArray *params) {
  for (size_t i = 0; params != NULL && i < params->len; i++) {
    if (name_equal(name, params->items[i])) {
      return true;
    }
  }
  return false;
}

static char *cell_for_name(Emit *e, KiName name) {
  for (size_t i = e->nbindings; i > 0; i--) {
    if (name_equal(e->bindings[i - 1].name, name)) {
      return e->bindings[i - 1].cell;
    }
  }
  return mangle(name, "v_");
}

static void emit_name_label(FILE *out, KiName name) {
  emit_c_string(out, name.s, name.n);
}

static void emit_expr(Emit *e, const Node *node);

static void emit_name_expr(Emit *e, KiName name) {
  if (is_sonokazu(name) && e->sonokazu_depth > 0) {
    fprintf(e->out, "nrt_get(&%s, \"その数\")",
            e->sonokazu_stack[e->sonokazu_depth - 1]);
    return;
  }
  char *cell = cell_for_name(e, name);
  fputs("nrt_get(&", e->out);
  fputs(cell, e->out);
  fputs(", ", e->out);
  emit_name_label(e->out, name);
  fputc(')', e->out);
}

static void emit_expr(Emit *e, const Node *node) {
  switch (node->kind) {
    case N_KAZU:
      fprintf(e->out, "nrt_kazu(%" PRId64 ")", node->u.kazu);
      return;
    case N_KOTO:
      fputs("nrt_koto(", e->out);
      emit_c_string(e->out, node->u.koto.s, node->u.koto.n);
      fprintf(e->out, ", %zu)", node->u.koto.n);
      return;
    case N_NA:
      emit_name_expr(e, node->u.na);
      return;
    case N_ADD:
      fputs("nrt_add(", e->out);
      break;
    case N_SUB:
      fputs("nrt_sub(", e->out);
      break;
    case N_MUL:
      fputs("nrt_mul(", e->out);
      break;
    case N_DIV:
      fputs("nrt_div(", e->out);
      break;
    case N_MOD:
      fputs("nrt_mod(", e->out);
      break;
    case N_EQ:
      fputs("nrt_eq(", e->out);
      break;
    case N_GT:
      fputs("nrt_gt(", e->out);
      break;
    case N_LT:
      fputs("nrt_lt(", e->out);
      break;
    case N_LTE:
      fputs("nrt_lte(", e->out);
      break;
    case N_ISZERO:
      fputs("nrt_iszero(", e->out);
      emit_expr(e, node->u.un.x);
      fputc(')', e->out);
      return;
    case N_NOT:
      fputs("nrt_not(", e->out);
      emit_expr(e, node->u.un.x);
      fputc(')', e->out);
      return;
    case N_APPLY: {
      char *fn = mangle(node->u.call.name, "w_");
      fputs(fn, e->out);
      fputc('(', e->out);
      for (size_t i = 0; i < node->u.call.args.len; i++) {
        if (i > 0) {
          fputs(", ", e->out);
        }
        emit_expr(e, node->u.call.args.items[i]);
      }
      fputc(')', e->out);
      return;
    }
    default:
      fputs("nrt_kazu(0)", e->out);
      return;
  }
  emit_expr(e, node->u.bin.lhs);
  fputs(", ", e->out);
  emit_expr(e, node->u.bin.rhs);
  fputc(')', e->out);
}

static void emit_array(Emit *e, const NodeArray *array);

static void emit_cell_decls(Emit *e, const NodeArray *body, const KiNameArray *params) {
  NameSet set = {0};
  KiNameArray hidden = {0};
  collect_array(body, &set, &hidden);
  for (size_t i = 0; i < set.len; i++) {
    emit_indent(e);
    char *cell = mangle(set.items[i], "v_");
    if (name_in_params(set.items[i], params)) {
      char *param = mangle(set.items[i], "p_");
      fprintf(e->out, "NrtCell %s = {1, %s};\n", cell, param);
    } else {
      fprintf(e->out, "NrtCell %s = {0, {NRT_KAZU, 0, NULL, 0}};\n", cell);
    }
  }
}

static void emit_set_cell(Emit *e, KiName name, const Node *expr) {
  char *cell = cell_for_name(e, name);
  fputs("nrt_set(&", e->out);
  fputs(cell, e->out);
  fputs(", ", e->out);
  emit_expr(e, expr);
  fputc(')', e->out);
}

static void push_binding(Emit *e, KiName name, char *cell) {
  if (e->nbindings >= sizeof(e->bindings) / sizeof(e->bindings[0])) {
    nrt_fatal_at(0, 0, "C 出力器の入れ子が深すぎます");
  }
  e->bindings[e->nbindings].name = name;
  e->bindings[e->nbindings].cell = cell;
  e->nbindings++;
}

static void emit_statement(Emit *e, const Node *node) {
  switch (node->kind) {
    case N_OKU:
      emit_indent(e);
      emit_set_cell(e, node->u.assign.name, node->u.assign.expr);
      fputs(";\n", e->out);
      return;
    case N_ADDSET:
    case N_SUBSET: {
      emit_indent(e);
      char *cell = cell_for_name(e, node->u.assign.name);
      fputs("nrt_set(&", e->out);
      fputs(cell, e->out);
      fputs(node->kind == N_ADDSET ? ", nrt_add(" : ", nrt_sub(", e->out);
      fputs("nrt_get(&", e->out);
      fputs(cell, e->out);
      fputs(", ", e->out);
      emit_name_label(e->out, node->u.assign.name);
      fputs("), ", e->out);
      emit_expr(e, node->u.assign.expr);
      fputs("));\n", e->out);
      return;
    }
    case N_NORU:
      emit_indent(e);
      fputs("nrt_print(", e->out);
      emit_expr(e, node->u.un.x);
      fputs(");\n", e->out);
      return;
    case N_KAHESU:
      emit_indent(e);
      fputs("return ", e->out);
      emit_expr(e, node->u.un.x);
      fputs(";\n", e->out);
      return;
    case N_OKONAFU:
      emit_indent(e);
      emit_expr(e, node->u.un.x);
      fputs(";\n", e->out);
      return;
    case N_IF:
      for (size_t i = 0; i < node->u.iff.branches.len; i++) {
        const Branch *branch = &node->u.iff.branches.items[i];
        emit_indent(e);
        if (i == 0) {
          fputs("if (nrt_truth(", e->out);
          emit_expr(e, branch->cond);
          fputs(")) {\n", e->out);
        } else if (branch->cond != NULL) {
          fputs("else if (nrt_truth(", e->out);
          emit_expr(e, branch->cond);
          fputs(")) {\n", e->out);
        } else {
          fputs("else {\n", e->out);
        }
        e->indent++;
        emit_array(e, &branch->body);
        e->indent--;
        emit_indent(e);
        fputs("}\n", e->out);
      }
      return;
    case N_FOR: {
      int id = e->temp_id++;
      char from[32];
      char to[32];
      char step[32];
      char idx[32];
      char itcell[32];
      snprintf(from, sizeof(from), "_from%d", id);
      snprintf(to, sizeof(to), "_to%d", id);
      snprintf(step, sizeof(step), "_step%d", id);
      snprintf(idx, sizeof(idx), "_i%d", id);
      snprintf(itcell, sizeof(itcell), "_itv%d", id);

      emit_indent(e);
      fputs("{\n", e->out);
      e->indent++;
      emit_indent(e);
      fprintf(e->out, "int64_t %s = nrt_as_kazu(", from);
      emit_expr(e, node->u.forr.from);
      fputs(", \"反復の始め\");\n", e->out);
      emit_indent(e);
      fprintf(e->out, "int64_t %s = nrt_as_kazu(", to);
      emit_expr(e, node->u.forr.to);
      fputs(", \"反復の終り\");\n", e->out);
      emit_indent(e);
      fprintf(e->out, "int64_t %s = nrt_as_kazu(", step);
      emit_expr(e, node->u.forr.step);
      fputs(", \"反復の歩幅\");\n", e->out);
      emit_indent(e);
      fprintf(e->out, "if (%s <= 0) nrt_die(\"反復の歩幅は正でなければなりません\");\n", step);
      emit_indent(e);
      fprintf(e->out, "NrtCell %s = {0, {NRT_KAZU, 0, NULL, 0}};\n", itcell);

      size_t saved_bindings = e->nbindings;
      size_t saved_sonokazu = e->sonokazu_depth;
      if (node->u.forr.named_iter) {
        push_binding(e, node->u.forr.iter, nrt_xstrndup(itcell, strlen(itcell)));
      } else if (e->sonokazu_depth < sizeof(e->sonokazu_stack) / sizeof(e->sonokazu_stack[0])) {
        e->sonokazu_stack[e->sonokazu_depth++] = nrt_xstrndup(itcell, strlen(itcell));
      }

      emit_indent(e);
      fprintf(e->out, "if (%s <= %s) {\n", from, to);
      e->indent++;
      emit_indent(e);
      fprintf(e->out, "for (int64_t %s = %s; %s <= %s; %s += %s) {\n", idx, from,
              idx, to, idx, step);
      e->indent++;
      emit_indent(e);
      fprintf(e->out, "nrt_set(&%s, nrt_kazu(%s));\n", itcell, idx);
      emit_array(e, &node->u.forr.body);
      e->indent--;
      emit_indent(e);
      fputs("}\n", e->out);
      e->indent--;
      emit_indent(e);
      fputs("} else {\n", e->out);
      e->indent++;
      emit_indent(e);
      fprintf(e->out, "for (int64_t %s = %s; %s >= %s; %s -= %s) {\n", idx, from,
              idx, to, idx, step);
      e->indent++;
      emit_indent(e);
      fprintf(e->out, "nrt_set(&%s, nrt_kazu(%s));\n", itcell, idx);
      emit_array(e, &node->u.forr.body);
      e->indent--;
      emit_indent(e);
      fputs("}\n", e->out);
      e->indent--;
      emit_indent(e);
      fputs("}\n", e->out);
      e->nbindings = saved_bindings;
      e->sonokazu_depth = saved_sonokazu;
      e->indent--;
      emit_indent(e);
      fputs("}\n", e->out);
      return;
    }
    case N_WHILE:
      emit_indent(e);
      fputs("while (nrt_truth(", e->out);
      emit_expr(e, node->u.whil.cond);
      fputs(")) {\n", e->out);
      e->indent++;
      emit_array(e, &node->u.whil.body);
      e->indent--;
      emit_indent(e);
      fputs("}\n", e->out);
      return;
    case N_WAZA:
      return;
    default:
      emit_indent(e);
      fputs("nrt_die(\"実行できない木があります\");\n", e->out);
      return;
  }
}

static void emit_array(Emit *e, const NodeArray *array) {
  for (size_t i = 0; i < array->len; i++) {
    emit_statement(e, array->items[i]);
  }
}

static void emit_runtime(FILE *out) {
  fputs("#include <inttypes.h>\n", out);
  fputs("#include <stdint.h>\n", out);
  fputs("#include <stdio.h>\n", out);
  fputs("#include <stdlib.h>\n", out);
  fputs("#include <string.h>\n\n", out);
  fputs("#if defined(__GNUC__)\n#define NRT_UNUSED __attribute__((unused))\n#else\n#define NRT_UNUSED\n#endif\n\n", out);
  fputs("typedef enum { NRT_KAZU, NRT_KOTO } NrtKind;\n", out);
  fputs("typedef struct { NrtKind kind; int64_t kazu; const char *koto; size_t koto_len; } NrtValue;\n", out);
  fputs("typedef struct { int set; NrtValue value; } NrtCell;\n", out);
  fputs("static NRT_UNUSED void nrt_die(const char *msg) { fprintf(stderr, \"過ち有り：%s\\n\", msg); exit(1); }\n", out);
  fputs("static NRT_UNUSED NrtValue nrt_kazu(int64_t v) { NrtValue x = {NRT_KAZU, v, NULL, 0}; return x; }\n", out);
  fputs("static NRT_UNUSED NrtValue nrt_koto(const char *s, size_t n) { NrtValue x = {NRT_KOTO, 0, s, n}; return x; }\n", out);
  fputs("static NRT_UNUSED void nrt_set(NrtCell *cell, NrtValue value) { cell->set = 1; cell->value = value; }\n", out);
  fputs("static NRT_UNUSED NrtValue nrt_get(const NrtCell *cell, const char *name) { if (!cell->set) { fprintf(stderr, \"過ち有り：名「%s」は未定義です\\n\", name); exit(1); } return cell->value; }\n", out);
  fputs("static NRT_UNUSED int64_t nrt_as_kazu(NrtValue value, const char *ctx) { if (value.kind != NRT_KAZU) { fprintf(stderr, \"過ち有り：%sには数が要ります\\n\", ctx); exit(1); } return value.kazu; }\n", out);
  fputs("static NRT_UNUSED NrtValue nrt_add(NrtValue a, NrtValue b) { return nrt_kazu(nrt_as_kazu(a, \"式\") + nrt_as_kazu(b, \"式\")); }\n", out);
  fputs("static NRT_UNUSED NrtValue nrt_sub(NrtValue a, NrtValue b) { return nrt_kazu(nrt_as_kazu(a, \"式\") - nrt_as_kazu(b, \"式\")); }\n", out);
  fputs("static NRT_UNUSED NrtValue nrt_mul(NrtValue a, NrtValue b) { return nrt_kazu(nrt_as_kazu(a, \"式\") * nrt_as_kazu(b, \"式\")); }\n", out);
  fputs("static NRT_UNUSED NrtValue nrt_div(NrtValue a, NrtValue b) { int64_t r = nrt_as_kazu(b, \"式\"); if (r == 0) nrt_die(\"零で割りました\"); return nrt_kazu(nrt_as_kazu(a, \"式\") / r); }\n", out);
  fputs("static NRT_UNUSED NrtValue nrt_mod(NrtValue a, NrtValue b) { int64_t r = nrt_as_kazu(b, \"式\"); if (r == 0) nrt_die(\"零で割りました\"); return nrt_kazu(nrt_as_kazu(a, \"式\") % r); }\n", out);
  fputs("static NRT_UNUSED NrtValue nrt_eq(NrtValue a, NrtValue b) { if (a.kind == NRT_KOTO && b.kind == NRT_KOTO) return nrt_kazu(a.koto_len == b.koto_len && memcmp(a.koto, b.koto, a.koto_len) == 0); return nrt_kazu(nrt_as_kazu(a, \"式\") == nrt_as_kazu(b, \"式\")); }\n", out);
  fputs("static NRT_UNUSED NrtValue nrt_gt(NrtValue a, NrtValue b) { return nrt_kazu(nrt_as_kazu(a, \"式\") > nrt_as_kazu(b, \"式\")); }\n", out);
  fputs("static NRT_UNUSED NrtValue nrt_lt(NrtValue a, NrtValue b) { return nrt_kazu(nrt_as_kazu(a, \"式\") < nrt_as_kazu(b, \"式\")); }\n", out);
  fputs("static NRT_UNUSED NrtValue nrt_lte(NrtValue a, NrtValue b) { return nrt_kazu(nrt_as_kazu(a, \"式\") <= nrt_as_kazu(b, \"式\")); }\n", out);
  fputs("static NRT_UNUSED NrtValue nrt_iszero(NrtValue x) { return nrt_kazu(nrt_as_kazu(x, \"述語\") == 0); }\n", out);
  fputs("static NRT_UNUSED NrtValue nrt_not(NrtValue x) { return nrt_kazu(nrt_as_kazu(x, \"述語\") == 0); }\n", out);
  fputs("static NRT_UNUSED int nrt_truth(NrtValue x) { return nrt_as_kazu(x, \"述語\") != 0; }\n", out);
  fputs("static NRT_UNUSED void nrt_print(NrtValue x) { if (x.kind == NRT_KOTO) printf(\"%.*s\\n\", (int)x.koto_len, x.koto); else printf(\"%\" PRId64 \"\\n\", x.kazu); }\n\n", out);
}

static void emit_prototype(FILE *out, const Node *waza) {
  char *fn = mangle(waza->u.waza.name, "w_");
  fprintf(out, "static NRT_UNUSED NrtValue %s(", fn);
  for (size_t i = 0; i < waza->u.waza.params.len; i++) {
    if (i > 0) {
      fputs(", ", out);
    }
    char *param = mangle(waza->u.waza.params.items[i], "p_");
    fprintf(out, "NrtValue %s", param);
  }
  fputs(");\n", out);
}

static void emit_function(FILE *out, const Node *waza) {
  Emit e;
  memset(&e, 0, sizeof(e));
  e.out = out;
  char *fn = mangle(waza->u.waza.name, "w_");
  fprintf(out, "static NRT_UNUSED NrtValue %s(", fn);
  for (size_t i = 0; i < waza->u.waza.params.len; i++) {
    if (i > 0) {
      fputs(", ", out);
    }
    char *param = mangle(waza->u.waza.params.items[i], "p_");
    fprintf(out, "NrtValue %s", param);
  }
  fputs(") {\n", out);
  e.indent = 1;
  for (size_t i = 0; i < waza->u.waza.params.len; i++) {
    emit_indent(&e);
    char *param = mangle(waza->u.waza.params.items[i], "p_");
    fprintf(out, "(void)%s;\n", param);
  }
  emit_cell_decls(&e, &waza->u.waza.body, &waza->u.waza.params);
  emit_array(&e, &waza->u.waza.body);
  emit_indent(&e);
  fputs("return nrt_kazu(0);\n", out);
  fputs("}\n\n", out);
}

void utsushi_emit(const Program *program, FILE *out) {
  emit_runtime(out);

  const Node *first_zero_arg = NULL;
  size_t main_count = 0;
  for (size_t i = 0; i < program->items.len; i++) {
    const Node *node = program->items.items[i];
    if (node->kind == N_WAZA) {
      emit_prototype(out, node);
      if (first_zero_arg == NULL && node->u.waza.params.len == 0) {
        first_zero_arg = node;
      }
    } else {
      main_count++;
    }
  }
  fputc('\n', out);

  for (size_t i = 0; i < program->items.len; i++) {
    if (program->items.items[i]->kind == N_WAZA) {
      emit_function(out, program->items.items[i]);
    }
  }

  Emit e;
  memset(&e, 0, sizeof(e));
  e.out = out;
  fputs("int main(void) {\n", out);
  e.indent = 1;
  NodeArray main_body = {0};
  for (size_t i = 0; i < program->items.len; i++) {
    if (program->items.items[i]->kind != N_WAZA) {
      ki_node_array_push(&main_body, program->items.items[i]);
    }
  }
  emit_cell_decls(&e, &main_body, NULL);
  emit_array(&e, &main_body);
  if (main_count == 0 && first_zero_arg != NULL) {
    emit_indent(&e);
    char *fn = mangle(first_zero_arg->u.waza.name, "w_");
    fprintf(out, "%s();\n", fn);
  }
  emit_indent(&e);
  fputs("return 0;\n", out);
  fputs("}\n", out);
}
