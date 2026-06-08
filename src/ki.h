#ifndef NORITO_KI_H
#define NORITO_KI_H

#include "common.h"
#include "kotowari.h"

typedef struct Node Node;

typedef enum {
  N_WAZA,
  N_OKU,
  N_ADDSET,
  N_SUBSET,
  N_NORU,
  N_IF,
  N_FOR,
  N_WHILE,
  N_OKONAFU,
  N_KAHESU,
  N_KAZU,
  N_KOTO,
  N_NA,
  N_ADD,
  N_SUB,
  N_MUL,
  N_DIV,
  N_MOD,
  N_APPLY,
  N_EQ,
  N_GT,
  N_LT,
  N_LTE,
  N_ISZERO,
  N_NOT
} NodeKind;

typedef struct {
  const char *s;
  size_t n;
} KiName;

typedef struct {
  KiName *items;
  size_t len;
  size_t cap;
} KiNameArray;

typedef struct {
  Node **items;
  size_t len;
  size_t cap;
} NodeArray;

typedef struct {
  Node *cond;
  NodeArray body;
} Branch;

typedef struct {
  Branch *items;
  size_t len;
  size_t cap;
} BranchArray;

typedef struct {
  NodeArray items;
} Program;

struct Node {
  NodeKind kind;
  int line;
  int col;
  union {
    int64_t kazu;
    struct {
      const char *s;
      size_t n;
    } koto;
    KiName na;
    struct {
      Node *lhs;
      Node *rhs;
    } bin;
    struct {
      Node *x;
    } un;
    struct {
      KiName name;
      KiNameArray params;
      NodeArray body;
    } waza;
    struct {
      KiName name;
      Node *expr;
    } assign;
    struct {
      BranchArray branches;
    } iff;
    struct {
      KiName iter;
      bool named_iter;
      Node *from;
      Node *to;
      Node *step;
      NodeArray body;
    } forr;
    struct {
      Node *cond;
      NodeArray body;
    } whil;
    struct {
      KiName name;
      NodeArray args;
    } call;
  } u;
};

Node *ki_node(NodeKind kind, int line, int col);
Node *ki_name_node(const Token *tok);
void ki_node_array_push(NodeArray *array, Node *node);
void ki_name_array_push(KiNameArray *array, KiName name);
void ki_branch_array_push(BranchArray *array, Branch branch);
void ki_program_push(Program *program, Node *node);
const char *ki_kind_name(NodeKind kind);
void ki_print_program(const Program *program, FILE *out);

#endif
