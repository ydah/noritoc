#include "fumikumi.h"

#include <string.h>

typedef struct {
  const Token *tokens;
  size_t len;
  size_t pos;
} Parser;

typedef struct {
  bool kuten;
  bool eof;
  KwId kw1;
  KwId kw2;
} StopSet;

static const char *SONOKAZU = "その数";

static const Token *peek(const Parser *p) {
  return &p->tokens[p->pos];
}

static const Token *at(const Parser *p, size_t off) {
  size_t i = p->pos + off;
  if (i >= p->len) {
    return &p->tokens[p->len - 1];
  }
  return &p->tokens[i];
}

static bool is_kw_tok(const Token *tok, KwId kw) {
  return tok->kind == T_KW && tok->kw == kw;
}

static bool at_kw(const Parser *p, KwId kw) {
  return is_kw_tok(peek(p), kw);
}

static bool at_tonoru(const Parser *p) {
  return at_kw(p, KW_TONORU) || (at_kw(p, KW_TO) && is_kw_tok(at(p, 1), KW_NOBERU));
}

static bool match_kw(Parser *p, KwId kw) {
  if (!at_kw(p, kw)) {
    return false;
  }
  p->pos++;
  return true;
}

static bool match_kind(Parser *p, TokKind kind) {
  if (peek(p)->kind != kind) {
    return false;
  }
  p->pos++;
  return true;
}

static void parse_error_at(const Token *tok, const char *msg) {
  nrt_fatal_at(tok->line, tok->col, "%s（見えたもの：%s）", msg,
               kotowari_token_name(tok));
}

static const Token *expect_kind(Parser *p, TokKind kind, const char *msg) {
  const Token *tok = peek(p);
  if (tok->kind != kind) {
    parse_error_at(tok, msg);
  }
  p->pos++;
  return tok;
}

static const Token *expect_kw(Parser *p, KwId kw, const char *msg) {
  const Token *tok = peek(p);
  if (!is_kw_tok(tok, kw)) {
    parse_error_at(tok, msg);
  }
  p->pos++;
  return tok;
}

static void expect_tonoru(Parser *p) {
  if (match_kw(p, KW_TONORU)) {
    return;
  }
  if (match_kw(p, KW_TO)) {
    expect_kw(p, KW_NOBERU, "業は「と宣る」で閉じます");
    return;
  }
  parse_error_at(peek(p), "業は「と宣る」で閉じます");
}

static KiName token_name(const Token *tok) {
  KiName name;
  name.s = tok->lex;
  name.n = tok->len;
  return name;
}

static Node *parse_expr_optional(Parser *p);
static Node *parse_expr_internal(Parser *p, bool allow_omitted_div, bool allow_apply);
static bool try_parse_predicate(Parser *p, Node **out);
static void parse_stmt_list(Parser *p, NodeArray *out, StopSet stop);

static StopSet stop_kuten(void) {
  StopSet stop;
  memset(&stop, 0, sizeof(stop));
  stop.kuten = true;
  return stop;
}

static StopSet stop_koto(void) {
  StopSet stop;
  memset(&stop, 0, sizeof(stop));
  stop.kw1 = KW_KOTO;
  return stop;
}

static StopSet stop_branch(void) {
  StopSet stop;
  memset(&stop, 0, sizeof(stop));
  stop.kuten = true;
  stop.kw1 = KW_SHIKARAZUWA;
  return stop;
}

static bool is_stop(const Parser *p, StopSet stop) {
  const Token *tok = peek(p);
  if (stop.eof && tok->kind == T_EOF) {
    return true;
  }
  if (stop.kuten && tok->kind == T_KUTEN) {
    return true;
  }
  if (stop.kw1 != KW_NONE && is_kw_tok(tok, stop.kw1)) {
    return true;
  }
  if (stop.kw2 != KW_NONE && is_kw_tok(tok, stop.kw2)) {
    return true;
  }
  return false;
}

static bool has_kw_before_stop(const Parser *p, KwId kw, StopSet stop) {
  Parser q = *p;
  while (q.pos < q.len && !is_stop(&q, stop) && peek(&q)->kind != T_EOF) {
    if (at_kw(&q, kw)) {
      return true;
    }
    q.pos++;
  }
  return false;
}

static void require_body(const NodeArray *body, const Token *tok, const char *what) {
  if (body->len == 0) {
    nrt_fatal_at(tok->line, tok->col, "%sの本体がありません", what);
  }
}

static Node *number_node(int64_t value, int line, int col) {
  Node *node = ki_node(N_KAZU, line, col);
  node->u.kazu = value;
  return node;
}

static Node *sonokazu_node(int line, int col) {
  Node *node = ki_node(N_NA, line, col);
  node->u.na.s = SONOKAZU;
  node->u.na.n = strlen(SONOKAZU);
  return node;
}

static Node *binary_node(NodeKind kind, Node *lhs, Node *rhs) {
  Node *node = ki_node(kind, lhs->line, lhs->col);
  node->u.bin.lhs = lhs;
  node->u.bin.rhs = rhs;
  return node;
}

static Node *unary_node(NodeKind kind, Node *x) {
  Node *node = ki_node(kind, x->line, x->col);
  node->u.un.x = x;
  return node;
}

static bool is_oku_form(KwId kw) {
  return kw == KW_OKU || kw == KW_OKI;
}

static bool is_masu_form(KwId kw) {
  return kw == KW_MASU || kw == KW_MASHI;
}

static bool is_herasu_form(KwId kw) {
  return kw == KW_HERASU || kw == KW_HERASHI;
}

static bool is_noru_form(KwId kw) {
  return kw == KW_NORU || kw == KW_NORI;
}

static bool is_kahesu_form(KwId kw) {
  return kw == KW_KAHESU || kw == KW_KAHESHI;
}

static bool is_okonafu_form(KwId kw) {
  return kw == KW_OKONAFU || kw == KW_OKONAHI;
}

static bool match_any_form(Parser *p, bool (*pred)(KwId)) {
  const Token *tok = peek(p);
  if (tok->kind != T_KW || !pred(tok->kw)) {
    return false;
  }
  p->pos++;
  return true;
}

static Node *parse_atom_optional(Parser *p) {
  const Token *tok = peek(p);
  if (match_kind(p, T_KAZU)) {
    return number_node(tok->kazu, tok->line, tok->col);
  }
  if (match_kind(p, T_KOTO)) {
    Node *node = ki_node(N_KOTO, tok->line, tok->col);
    node->u.koto.s = tok->lex;
    node->u.koto.n = tok->len;
    return node;
  }
  if (match_kind(p, T_NA)) {
    return ki_name_node(tok);
  }
  if (match_kw(p, KW_SONOKAZU)) {
    return sonokazu_node(tok->line, tok->col);
  }
  if (match_kind(p, T_LPAREN)) {
    Node *expr = parse_expr_optional(p);
    if (expr == NULL) {
      parse_error_at(peek(p), "括弧の中に式がありません");
    }
    expect_kind(p, T_RPAREN, "閉じ括弧がありません");
    return expr;
  }
  return NULL;
}

static Node *parse_expr_required(Parser *p) {
  Node *expr = parse_expr_optional(p);
  if (expr == NULL) {
    parse_error_at(peek(p), "式が要ります");
  }
  return expr;
}

static Node *make_apply(NodeArray args, const Token *name) {
  Node *node = ki_node(N_APPLY, name->line, name->col);
  node->u.call.name = token_name(name);
  node->u.call.args = args;
  return node;
}

static Node *parse_expr_optional(Parser *p) {
  return parse_expr_internal(p, true, true);
}

static Node *parse_expr_internal(Parser *p, bool allow_omitted_div, bool allow_apply) {
  Node *left = parse_atom_optional(p);
  if (left == NULL) {
    return NULL;
  }

  for (;;) {
    size_t save = p->pos;
    if (match_kw(p, KW_NI)) {
      Node *rhs = parse_expr_internal(p, false, allow_apply);
      if (rhs != NULL && match_kw(p, KW_WO) && match_kw(p, KW_KUWAHETARU)) {
        left = binary_node(N_ADD, left, rhs);
        continue;
      }
      p->pos = save;
      break;
    }

    save = p->pos;
    if (match_kw(p, KW_YORI)) {
      Node *rhs = parse_expr_internal(p, false, allow_apply);
      if (rhs != NULL && match_kw(p, KW_WO) && match_kw(p, KW_HIKITARU)) {
        left = binary_node(N_SUB, left, rhs);
        continue;
      }
      p->pos = save;
      break;
    }

    save = p->pos;
    if (match_kw(p, KW_WO)) {
      Node *rhs = parse_expr_internal(p, false, allow_apply);
      if (rhs != NULL && match_kw(p, KW_NI) && match_kw(p, KW_KAKETARU)) {
        left = binary_node(N_MUL, left, rhs);
        continue;
      }
      p->pos = save;
      if (match_kw(p, KW_WO)) {
        rhs = parse_expr_internal(p, false, allow_apply);
        if (rhs != NULL && match_kw(p, KW_NITE) && match_kw(p, KW_WARITARU)) {
          bool mod = match_kw(p, KW_AMARI);
          left = binary_node(mod ? N_MOD : N_DIV, left, rhs);
          continue;
        }
      }
      p->pos = save;
      break;
    }

    save = p->pos;
    if (allow_omitted_div && match_kw(p, KW_NITE)) {
      if (match_kw(p, KW_WARITARU)) {
        bool mod = match_kw(p, KW_AMARI);
        Node *subject = sonokazu_node(left->line, left->col);
        left = binary_node(mod ? N_MOD : N_DIV, subject, left);
        continue;
      }
      p->pos = save;
      break;
    }

    save = p->pos;
    if (allow_apply && match_kw(p, KW_NO)) {
      const Token *name = expect_kind(p, T_NA, "属格適用の業名が要ります");
      NodeArray args = {0};
      ki_node_array_push(&args, left);
      left = make_apply(args, name);
      continue;
    }

    save = p->pos;
    if (allow_apply && match_kw(p, KW_TO)) {
      NodeArray args = {0};
      ki_node_array_push(&args, left);
      Node *arg = parse_expr_internal(p, true, false);
      if (arg == NULL) {
        p->pos = save;
        break;
      }
      ki_node_array_push(&args, arg);
      for (;;) {
        if (match_kw(p, KW_TONO)) {
          const Token *name = expect_kind(p, T_NA, "多引数適用の業名が要ります");
          left = make_apply(args, name);
          break;
        }
        if (!match_kw(p, KW_TO)) {
          p->pos = save;
          break;
        }
        if (match_kw(p, KW_NO)) {
          const Token *name = expect_kind(p, T_NA, "多引数適用の業名が要ります");
          left = make_apply(args, name);
          break;
        }
        arg = parse_expr_internal(p, true, false);
        if (arg == NULL) {
          p->pos = save;
          break;
        }
        ki_node_array_push(&args, arg);
      }
      if (p->pos == save) {
        break;
      }
      continue;
    }

    break;
  }
  return left;
}

static bool try_parse_predicate(Parser *p, Node **out) {
  size_t save = p->pos;

  Node *passed_lhs = parse_atom_optional(p);
  if (passed_lhs != NULL && match_kw(p, KW_NO)) {
    Node *rhs = parse_expr_optional(p);
    if (rhs != NULL && match_kw(p, KW_WO) && match_kw(p, KW_SUGINU)) {
      Node *cond = binary_node(N_LTE, passed_lhs, rhs);
      if (match_kw(p, KW_ZU)) {
        cond = unary_node(N_NOT, cond);
      }
      *out = cond;
      return true;
    }
  }
  p->pos = save;

  Node *lhs = parse_expr_optional(p);
  if (lhs == NULL) {
    p->pos = save;
    return false;
  }

  Node *cond = NULL;
  if (match_kw(p, KW_NAKU)) {
    cond = unary_node(N_ISZERO, lhs);
  } else if (match_kw(p, KW_TO)) {
    Node *rhs = parse_expr_optional(p);
    if (rhs == NULL) {
      p->pos = save;
      return false;
    }
    (void)match_kw(p, KW_TO);
    if (!match_kw(p, KW_HITOSHIKARA)) {
      p->pos = save;
      return false;
    }
    cond = binary_node(N_EQ, lhs, rhs);
  } else if (match_kw(p, KW_GA)) {
    Node *rhs = parse_expr_optional(p);
    if (rhs == NULL || !match_kw(p, KW_NI)) {
      p->pos = save;
      return false;
    }
    if (match_kw(p, KW_MASARA)) {
      cond = binary_node(N_GT, lhs, rhs);
    } else if (match_kw(p, KW_OTORA)) {
      cond = binary_node(N_LT, lhs, rhs);
    } else {
      p->pos = save;
      return false;
    }
  } else if (match_kw(p, KW_NO)) {
    Node *rhs = parse_expr_optional(p);
    if (rhs == NULL || !match_kw(p, KW_WO) || !match_kw(p, KW_SUGINU)) {
      p->pos = save;
      return false;
    }
    cond = binary_node(N_LTE, lhs, rhs);
  } else {
    p->pos = save;
    return false;
  }

  if (match_kw(p, KW_ZU)) {
    cond = unary_node(N_NOT, cond);
  }
  *out = cond;
  return true;
}

static Node *parse_control_kernel(Parser *p, NodeArray body) {
  size_t save = p->pos;
  KiName iter = {0};
  bool named_iter = false;
  if (peek(p)->kind == T_NA && is_kw_tok(at(p, 1), KW_WO)) {
    iter = token_name(peek(p));
    p->pos += 2;
    named_iter = true;
  }
  Node *from = parse_expr_optional(p);
  if (from != NULL && match_kw(p, KW_YORI)) {
    Node *to = parse_expr_required(p);
    expect_kw(p, KW_NIITARUMADE, "「に至るまで」が要ります");
    Node *step = number_node(1, to->line, to->col);
    if (!at_kw(p, KW_KAZOFURU_GOTONI)) {
      step = parse_expr_required(p);
      expect_kw(p, KW_ZUTSU, "歩幅には「づつ」が要ります");
    }
    expect_kw(p, KW_KAZOFURU_GOTONI, "「数ふるごとに」が要ります");
    Node *node = ki_node(N_FOR, from->line, from->col);
    node->u.forr.iter = iter;
    node->u.forr.named_iter = named_iter;
    node->u.forr.from = from;
    node->u.forr.to = to;
    node->u.forr.step = step;
    node->u.forr.body = body;
    return node;
  }
  p->pos = save;

  Node *cond = NULL;
  if (try_parse_predicate(p, &cond) && match_kw(p, KW_AHIDA)) {
    Node *node = ki_node(N_WHILE, cond->line, cond->col);
    node->u.whil.cond = cond;
    node->u.whil.body = body;
    return node;
  }

  p->pos = save;
  parse_error_at(peek(p), "入れ子の制御核が要ります");
  return NULL;
}

static bool try_parse_for(Parser *p, Node **out, StopSet outer_stop) {
  size_t save = p->pos;
  KiName iter = {0};
  bool named_iter = false;
  if (peek(p)->kind == T_NA && is_kw_tok(at(p, 1), KW_WO)) {
    iter = token_name(peek(p));
    p->pos += 2;
    named_iter = true;
  }
  Node *from = parse_expr_optional(p);
  if (from == NULL || !match_kw(p, KW_YORI)) {
    p->pos = save;
    return false;
  }
  Node *to = parse_expr_required(p);
  expect_kw(p, KW_NIITARUMADE, "「に至るまで」が要ります");
  Node *step = number_node(1, to->line, to->col);
  if (!at_kw(p, KW_KAZOFURU_GOTONI)) {
    step = parse_expr_required(p);
    expect_kw(p, KW_ZUTSU, "歩幅には「づつ」が要ります");
  }
  expect_kw(p, KW_KAZOFURU_GOTONI, "「数ふるごとに」が要ります");
  (void)match_kind(p, T_TOUTEN);

  Node *node = ki_node(N_FOR, from->line, from->col);
  node->u.forr.iter = iter;
  node->u.forr.named_iter = named_iter;
  node->u.forr.from = from;
  node->u.forr.to = to;
  node->u.forr.step = step;
  parse_stmt_list(p, &node->u.forr.body, outer_stop);
  require_body(&node->u.forr.body, peek(p), "範囲反復");
  *out = node;
  return true;
}

static bool try_parse_while(Parser *p, Node **out, StopSet outer_stop) {
  size_t save = p->pos;
  Node *cond = NULL;
  if (!try_parse_predicate(p, &cond) || !match_kw(p, KW_AHIDA)) {
    p->pos = save;
    return false;
  }
  (void)match_kind(p, T_TOUTEN);
  Node *node = ki_node(N_WHILE, cond->line, cond->col);
  node->u.whil.cond = cond;
  parse_stmt_list(p, &node->u.whil.body, outer_stop);
  require_body(&node->u.whil.body, peek(p), "継続反復");
  *out = node;
  return true;
}

static bool match_if_delim(Parser *p) {
  return match_kw(p, KW_BA) || match_kw(p, KW_WA);
}

static bool try_parse_if(Parser *p, Node **out) {
  size_t save = p->pos;
  Node *cond = NULL;
  if (!try_parse_predicate(p, &cond) || !match_if_delim(p)) {
    p->pos = save;
    return false;
  }

  Node *node = ki_node(N_IF, cond->line, cond->col);
  Branch first;
  memset(&first, 0, sizeof(first));
  first.cond = cond;
  parse_stmt_list(p, &first.body, stop_branch());
  require_body(&first.body, peek(p), "条件枝");
  ki_branch_array_push(&node->u.iff.branches, first);

  while (match_kw(p, KW_SHIKARAZUWA)) {
    size_t branch_save = p->pos;
    Node *next_cond = NULL;
    Branch branch;
    memset(&branch, 0, sizeof(branch));
    if (try_parse_predicate(p, &next_cond) && match_if_delim(p)) {
      branch.cond = next_cond;
      parse_stmt_list(p, &branch.body, stop_branch());
      require_body(&branch.body, peek(p), "条件枝");
      ki_branch_array_push(&node->u.iff.branches, branch);
      continue;
    }
    p->pos = branch_save;
    branch.cond = NULL;
    parse_stmt_list(p, &branch.body, stop_kuten());
    require_body(&branch.body, peek(p), "条件枝");
    ki_branch_array_push(&node->u.iff.branches, branch);
    break;
  }
  *out = node;
  return true;
}

static Node *parse_nested(Parser *p) {
  NodeArray body = {0};
  parse_stmt_list(p, &body, stop_koto());
  require_body(&body, peek(p), "入れ子");
  expect_kw(p, KW_KOTO, "入れ子には「こと」が要ります");
  expect_kw(p, KW_WO, "「こと」の後に「を」が要ります");
  (void)match_kind(p, T_TOUTEN);
  Node *current = parse_control_kernel(p, body);
  while (match_kw(p, KW_SURU)) {
    expect_kw(p, KW_KOTO, "「する」の後に「こと」が要ります");
    expect_kw(p, KW_WO, "「こと」の後に「を」が要ります");
    (void)match_kind(p, T_TOUTEN);
    NodeArray wrapped = {0};
    ki_node_array_push(&wrapped, current);
    current = parse_control_kernel(p, wrapped);
  }
  expect_kw(p, KW_SU, "入れ子は「す」で閉じます");
  return current;
}

static Node *parse_action(Parser *p) {
  const Token *tok = peek(p);
  if (tok->kind == T_KOTO && is_kw_tok(at(p, 1), KW_TO) &&
      at(p, 2)->kind == T_KW && is_noru_form(at(p, 2)->kw)) {
    p->pos += 3;
    Node *lit = ki_node(N_KOTO, tok->line, tok->col);
    lit->u.koto.s = tok->lex;
    lit->u.koto.n = tok->len;
    return unary_node(N_NORU, lit);
  }

  Node *expr = parse_expr_required(p);
  if (match_kw(p, KW_WO)) {
    if (peek(p)->kind == T_NA && is_kw_tok(at(p, 1), KW_NI) &&
        at(p, 2)->kind == T_KW && is_oku_form(at(p, 2)->kw)) {
      const Token *name = peek(p);
      p->pos += 3;
      Node *node = ki_node(N_OKU, expr->line, expr->col);
      node->u.assign.name = token_name(name);
      node->u.assign.expr = expr;
      return node;
    }
    if (peek(p)->kind == T_KW && is_noru_form(peek(p)->kw)) {
      p->pos++;
      return unary_node(N_NORU, expr);
    }
    if (peek(p)->kind == T_KW && is_kahesu_form(peek(p)->kw)) {
      p->pos++;
      return unary_node(N_KAHESU, expr);
    }
    if (peek(p)->kind == T_KW && is_okonafu_form(peek(p)->kw)) {
      p->pos++;
      return unary_node(N_OKONAFU, expr);
    }
    parse_error_at(peek(p), "「を」の後の動作が分かりません");
  }

  if (expr->kind == N_NA && match_kw(p, KW_NI)) {
    Node *rhs = parse_expr_required(p);
    expect_kw(p, KW_WO, "増す動作には「を」が要ります");
    if (!match_any_form(p, is_masu_form)) {
      parse_error_at(peek(p), "「増す」が要ります");
    }
    Node *node = ki_node(N_ADDSET, expr->line, expr->col);
    node->u.assign.name = expr->u.na;
    node->u.assign.expr = rhs;
    return node;
  }

  if (expr->kind == N_NA && match_kw(p, KW_YORI)) {
    Node *rhs = parse_expr_required(p);
    expect_kw(p, KW_WO, "減らす動作には「を」が要ります");
    if (!match_any_form(p, is_herasu_form)) {
      parse_error_at(peek(p), "「減らす」が要ります");
    }
    Node *node = ki_node(N_SUBSET, expr->line, expr->col);
    node->u.assign.name = expr->u.na;
    node->u.assign.expr = rhs;
    return node;
  }

  parse_error_at(peek(p), "文の動作が分かりません");
  return NULL;
}

static bool consume_separator(Parser *p) {
  if (match_kind(p, T_TOUTEN)) {
    return true;
  }
  return match_kw(p, KW_TE);
}

static void parse_stmt_list(Parser *p, NodeArray *out, StopSet stop) {
  while (!is_stop(p, stop)) {
    if (peek(p)->kind == T_EOF) {
      parse_error_at(peek(p), "文が閉じないまま終わりました");
    }

    if (has_kw_before_stop(p, KW_KOTO, stop)) {
      Node *nested = parse_nested(p);
      ki_node_array_push(out, nested);
    } else {
      Node *control = NULL;
      if (try_parse_if(p, &control) || try_parse_for(p, &control, stop) ||
          try_parse_while(p, &control, stop)) {
        ki_node_array_push(out, control);
      } else {
        Node *action = parse_action(p);
        ki_node_array_push(out, action);
      }
    }

    if (is_stop(p, stop)) {
      break;
    }
    if (!consume_separator(p)) {
      parse_error_at(peek(p), "動作の後には「、」または文末が要ります");
    }
  }
}

static void parse_sentence_into(Parser *p, NodeArray *out) {
  parse_stmt_list(p, out, stop_kuten());
  expect_kind(p, T_KUTEN, "文末の「。」がありません");
}

static bool looks_like_waza(const Parser *p) {
  return peek(p)->kind == T_NA && is_kw_tok(at(p, 1), KW_TOFU) &&
         is_kw_tok(at(p, 2), KW_WAZA) && is_kw_tok(at(p, 3), KW_WA);
}

static bool match_norito_prelude(Parser *p) {
  if (!match_kw(p, KW_KAKEMAKUMO)) {
    return false;
  }
  while (peek(p)->kind != T_EOF && peek(p)->kind != T_TOUTEN) {
    p->pos++;
  }
  expect_kind(p, T_TOUTEN, "祝詞の前置句は「、」で閉じます");
  return true;
}

static bool match_norito_closing(Parser *p) {
  if (!at_kw(p, KW_TO) || !is_kw_tok(at(p, 1), KW_KOSO) ||
      !is_kw_tok(at(p, 2), KW_MOUSE) || at(p, 3)->kind != T_KUTEN) {
    return false;
  }
  p->pos += 4;
  return true;
}

static bool try_parse_params(Parser *p, KiNameArray *params) {
  size_t save = p->pos;
  if (peek(p)->kind != T_NA) {
    return false;
  }
  for (;;) {
    const Token *name = expect_kind(p, T_NA, "引数名が要ります");
    ki_name_array_push(params, token_name(name));
    if (!match_kw(p, KW_TO)) {
      break;
    }
    if (peek(p)->kind != T_NA) {
      break;
    }
  }
  (void)match_kw(p, KW_TO);
  if (!match_kw(p, KW_WO) || !match_kw(p, KW_TAMAHARITE)) {
    p->pos = save;
    params->len = 0;
    return false;
  }
  (void)match_kind(p, T_TOUTEN);
  return true;
}

static Node *parse_waza(Parser *p) {
  const Token *name = expect_kind(p, T_NA, "業名が要ります");
  expect_kw(p, KW_TOFU, "業名の後に「とふ」が要ります");
  expect_kw(p, KW_WAZA, "「業」が要ります");
  expect_kw(p, KW_WA, "「は」が要ります");
  (void)match_kind(p, T_TOUTEN);

  Node *node = ki_node(N_WAZA, name->line, name->col);
  node->u.waza.name = token_name(name);
  (void)try_parse_params(p, &node->u.waza.params);

  while (!at_tonoru(p)) {
    if (peek(p)->kind == T_EOF) {
      parse_error_at(peek(p), "業が「と宣る。」で閉じていません");
    }
    parse_sentence_into(p, &node->u.waza.body);
  }
  require_body(&node->u.waza.body, peek(p), "業");
  expect_tonoru(p);
  expect_kind(p, T_KUTEN, "「と宣る」の後に「。」が要ります");
  return node;
}

Program *fumikumi_parse(const TokenList *tokens) {
  Parser p;
  memset(&p, 0, sizeof(p));
  p.tokens = tokens->items;
  p.len = tokens->len;

  Program *program = nrt_xcalloc(1, sizeof(*program));
  while (peek(&p)->kind != T_EOF) {
    if (match_norito_prelude(&p) || match_norito_closing(&p)) {
      continue;
    }
    if (looks_like_waza(&p)) {
      ki_program_push(program, parse_waza(&p));
    } else {
      parse_sentence_into(&p, &program->items);
    }
  }
  return program;
}
