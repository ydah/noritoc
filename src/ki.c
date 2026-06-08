#include "ki.h"

#include <string.h>

Node *ki_node(NodeKind kind, int line, int col) {
  Node *node = nrt_xcalloc(1, sizeof(*node));
  node->kind = kind;
  node->line = line;
  node->col = col;
  return node;
}

Node *ki_name_node(const Token *tok) {
  Node *node = ki_node(N_NA, tok->line, tok->col);
  node->u.na.s = tok->lex;
  node->u.na.n = tok->len;
  return node;
}

void ki_node_array_push(NodeArray *array, Node *node) {
  if (array->len == array->cap) {
    array->cap = array->cap == 0 ? 8 : array->cap * 2;
    array->items = nrt_xrealloc(array->items, array->cap * sizeof(array->items[0]));
  }
  array->items[array->len++] = node;
}

void ki_name_array_push(KiNameArray *array, KiName name) {
  if (array->len == array->cap) {
    array->cap = array->cap == 0 ? 4 : array->cap * 2;
    array->items = nrt_xrealloc(array->items, array->cap * sizeof(array->items[0]));
  }
  array->items[array->len++] = name;
}

void ki_branch_array_push(BranchArray *array, Branch branch) {
  if (array->len == array->cap) {
    array->cap = array->cap == 0 ? 4 : array->cap * 2;
    array->items = nrt_xrealloc(array->items, array->cap * sizeof(array->items[0]));
  }
  array->items[array->len++] = branch;
}

void ki_program_push(Program *program, Node *node) {
  ki_node_array_push(&program->items, node);
}

const char *ki_kind_name(NodeKind kind) {
  switch (kind) {
    case N_WAZA:
      return "業";
    case N_OKU:
      return "置く";
    case N_ADDSET:
      return "増す";
    case N_SUBSET:
      return "減らす";
    case N_NORU:
      return "告る";
    case N_IF:
      return "条件";
    case N_FOR:
      return "範囲反復";
    case N_WHILE:
      return "継続反復";
    case N_OKONAFU:
      return "行ふ";
    case N_KAHESU:
      return "返す";
    case N_KAZU:
      return "数";
    case N_KOTO:
      return "言";
    case N_NA:
      return "名";
    case N_ADD:
      return "加";
    case N_SUB:
      return "減";
    case N_MUL:
      return "掛";
    case N_DIV:
      return "割";
    case N_MOD:
      return "余";
    case N_APPLY:
      return "適用";
    case N_EQ:
      return "等";
    case N_GT:
      return "勝";
    case N_LT:
      return "劣";
    case N_LTE:
      return "過ぎぬ";
    case N_ISZERO:
      return "無";
    case N_NOT:
      return "ず";
  }
  return "?";
}

static void print_indent(FILE *out, int depth) {
  for (int i = 0; i < depth; i++) {
    fputs("  ", out);
  }
}

static void print_name(FILE *out, KiName name) {
  fprintf(out, "%.*s", (int)name.n, name.s);
}

static void print_node(FILE *out, const Node *node, int depth);

static void print_array(FILE *out, const NodeArray *array, int depth) {
  for (size_t i = 0; i < array->len; i++) {
    print_node(out, array->items[i], depth);
  }
}

static void print_expr_pair(FILE *out, const Node *node, int depth) {
  print_node(out, node->u.bin.lhs, depth + 1);
  print_node(out, node->u.bin.rhs, depth + 1);
}

static void print_node(FILE *out, const Node *node, int depth) {
  print_indent(out, depth);
  fprintf(out, "%s", ki_kind_name(node->kind));
  switch (node->kind) {
    case N_KAZU:
      fprintf(out, " %" PRId64 "\n", node->u.kazu);
      return;
    case N_KOTO:
      fprintf(out, " 「%.*s」\n", (int)node->u.koto.n, node->u.koto.s);
      return;
    case N_NA:
      fputc(' ', out);
      print_name(out, node->u.na);
      fputc('\n', out);
      return;
    case N_WAZA:
      fputc(' ', out);
      print_name(out, node->u.waza.name);
      fputc('(', out);
      for (size_t i = 0; i < node->u.waza.params.len; i++) {
        if (i > 0) {
          fputs(", ", out);
        }
        print_name(out, node->u.waza.params.items[i]);
      }
      fputs(")\n", out);
      print_array(out, &node->u.waza.body, depth + 1);
      return;
    case N_OKU:
    case N_ADDSET:
    case N_SUBSET:
      fputc(' ', out);
      print_name(out, node->u.assign.name);
      fputc('\n', out);
      print_node(out, node->u.assign.expr, depth + 1);
      return;
    case N_NORU:
    case N_OKONAFU:
    case N_KAHESU:
    case N_ISZERO:
    case N_NOT:
      fputc('\n', out);
      print_node(out, node->u.un.x, depth + 1);
      return;
    case N_IF:
      fputc('\n', out);
      for (size_t i = 0; i < node->u.iff.branches.len; i++) {
        const Branch *branch = &node->u.iff.branches.items[i];
        print_indent(out, depth + 1);
        fputs(branch->cond == NULL ? "然らずは\n" : "ば\n", out);
        if (branch->cond != NULL) {
          print_node(out, branch->cond, depth + 2);
        }
        print_array(out, &branch->body, depth + 2);
      }
      return;
    case N_FOR:
      fputc(' ', out);
      if (node->u.forr.named_iter) {
        print_name(out, node->u.forr.iter);
      } else {
        fputs("その数", out);
      }
      fputc('\n', out);
      print_node(out, node->u.forr.from, depth + 1);
      print_node(out, node->u.forr.to, depth + 1);
      print_node(out, node->u.forr.step, depth + 1);
      print_array(out, &node->u.forr.body, depth + 1);
      return;
    case N_WHILE:
      fputc('\n', out);
      print_node(out, node->u.whil.cond, depth + 1);
      print_array(out, &node->u.whil.body, depth + 1);
      return;
    case N_APPLY:
      fputc(' ', out);
      print_name(out, node->u.call.name);
      fputc('\n', out);
      print_array(out, &node->u.call.args, depth + 1);
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
      fputc('\n', out);
      print_expr_pair(out, node, depth);
      return;
  }
}

void ki_print_program(const Program *program, FILE *out) {
  fputs("源文\n", out);
  print_array(out, &program->items, 1);
}
