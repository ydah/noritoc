#ifndef NORITO_KOTOWARI_H
#define NORITO_KOTOWARI_H

#include "common.h"

typedef enum {
  T_KAZU,
  T_KOTO,
  T_NA,
  T_KW,
  T_KUTEN,
  T_TOUTEN,
  T_LPAREN,
  T_RPAREN,
  T_EOF
} TokKind;

typedef enum {
  KW_NONE,
  KW_WAZA,
  KW_TOFU,
  KW_WA,
  KW_WO,
  KW_NI,
  KW_TO,
  KW_GA,
  KW_NO,
  KW_TONO,
  KW_TE,
  KW_OKU,
  KW_OKI,
  KW_MASU,
  KW_MASHI,
  KW_HERASU,
  KW_HERASHI,
  KW_NORU,
  KW_NORI,
  KW_BA,
  KW_SHIKARAZUWA,
  KW_YORI,
  KW_NIITARUMADE,
  KW_ZUTSU,
  KW_KAZOFURU_GOTONI,
  KW_AHIDA,
  KW_KOTO,
  KW_SU,
  KW_SURU,
  KW_KUWAHETARU,
  KW_HIKITARU,
  KW_KAKETARU,
  KW_WARITARU,
  KW_AMARI,
  KW_NITE,
  KW_NAKU,
  KW_HITOSHIKARA,
  KW_MASARA,
  KW_OTORA,
  KW_ZU,
  KW_SONOKAZU,
  KW_TAMAHARITE,
  KW_KAHESU,
  KW_KAHESHI,
  KW_OKONAFU,
  KW_OKONAHI,
  KW_TONORU,
  KW_NOBERU,
  KW_SUGINU,
  KW_KAKEMAKUMO,
  KW_KOSO,
  KW_MOUSE
} KwId;

typedef struct {
  TokKind kind;
  KwId kw;
  int64_t kazu;
  const char *lex;
  size_t len;
  int line;
  int col;
} Token;

typedef struct {
  Token *items;
  size_t len;
  size_t cap;
} TokenList;

TokenList kotowari_lex(const char *path, const char *src);
const char *kotowari_kw_name(KwId kw);
const char *kotowari_token_name(const Token *tok);

#endif
