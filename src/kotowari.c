#include "kotowari.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  const char *text;
  KwId kw;
  bool hard_boundary;
} Keyword;

static const Keyword KEYWORDS[] = {
  {"に至るまで", KW_NIITARUMADE, true},
  {"数ふるごとに", KW_KAZOFURU_GOTONI, true},
  {"かけまくも畏き", KW_KAKEMAKUMO, true},
  {"賜はりて", KW_TAMAHARITE, true},
  {"然らずは", KW_SHIKARAZUWA, true},
  {"加へたる", KW_KUWAHETARU, true},
  {"引きたる", KW_HIKITARU, true},
  {"掛けたる", KW_KAKETARU, true},
  {"割りたる", KW_WARITARU, true},
  {"等しから", KW_HITOSHIKARA, true},
  {"と宣る", KW_TONORU, true},
  {"その数", KW_SONOKAZU, true},
  {"あひだ", KW_AHIDA, true},
  {"過ぎぬ", KW_SUGINU, true},
  {"宣る", KW_NOBERU, true},
  {"申せ", KW_MOUSE, true},
  {"こそ", KW_KOSO, true},
  {"行ふ", KW_OKONAFU, true},
  {"行ひ", KW_OKONAHI, true},
  {"返す", KW_KAHESU, true},
  {"返し", KW_KAHESHI, true},
  {"減らす", KW_HERASU, true},
  {"減らし", KW_HERASHI, true},
  {"告る", KW_NORU, true},
  {"告り", KW_NORI, true},
  {"置く", KW_OKU, true},
  {"置き", KW_OKI, true},
  {"増す", KW_MASU, true},
  {"増し", KW_MASHI, true},
  {"にて", KW_NITE, true},
  {"無く", KW_NAKU, true},
  {"勝ら", KW_MASARA, true},
  {"劣ら", KW_OTORA, true},
  {"との", KW_TONO, true},
  {"とふ", KW_TOFU, true},
  {"業", KW_WAZA, true},
  {"こと", KW_KOTO, true},
  {"する", KW_SURU, true},
  {"づつ", KW_ZUTSU, true},
  {"余り", KW_AMARI, true},
  {"より", KW_YORI, true},
  {"ば", KW_BA, false},
  {"ず", KW_ZU, false},
  {"す", KW_SU, true},
  {"を", KW_WO, true},
  {"に", KW_NI, true},
  {"と", KW_TO, true},
  {"が", KW_GA, true},
  {"の", KW_NO, true},
  {"は", KW_WA, true},
  {"て", KW_TE, true},
};

typedef struct {
  const char *path;
  const char *src;
  size_t pos;
  int line;
  int col;
  TokenList out;
} Lexer;

static void push_token(TokenList *list, Token tok) {
  if (list->len == list->cap) {
    list->cap = list->cap == 0 ? 64 : list->cap * 2;
    list->items = nrt_xrealloc(list->items, list->cap * sizeof(list->items[0]));
  }
  list->items[list->len++] = tok;
}

static bool at_bytes(const Lexer *lx, const char *s) {
  return nrt_starts_with(lx->src + lx->pos, s);
}

static bool is_u8_cont(unsigned char c) {
  return (c & 0xC0u) == 0x80u;
}

static size_t checked_u8_len(const Lexer *lx) {
  unsigned char c0 = (unsigned char)lx->src[lx->pos];
  if (c0 < 0x80u) {
    return 1;
  }
  unsigned char c1 = (unsigned char)lx->src[lx->pos + 1];
  unsigned char c2 = (unsigned char)lx->src[lx->pos + 2];
  unsigned char c3 = (unsigned char)lx->src[lx->pos + 3];
  if (c0 >= 0xC2u && c0 <= 0xDFu && is_u8_cont(c1)) {
    return 2;
  }
  if (c0 == 0xE0u && c1 >= 0xA0u && c1 <= 0xBFu && is_u8_cont(c2)) {
    return 3;
  }
  if (((c0 >= 0xE1u && c0 <= 0xECu) || (c0 >= 0xEEu && c0 <= 0xEFu)) &&
      is_u8_cont(c1) && is_u8_cont(c2)) {
    return 3;
  }
  if (c0 == 0xEDu && c1 >= 0x80u && c1 <= 0x9Fu && is_u8_cont(c2)) {
    return 3;
  }
  if (c0 == 0xF0u && c1 >= 0x90u && c1 <= 0xBFu && is_u8_cont(c2) &&
      is_u8_cont(c3)) {
    return 4;
  }
  if (c0 >= 0xF1u && c0 <= 0xF3u && is_u8_cont(c1) && is_u8_cont(c2) &&
      is_u8_cont(c3)) {
    return 4;
  }
  if (c0 == 0xF4u && c1 >= 0x80u && c1 <= 0x8Fu && is_u8_cont(c2) &&
      is_u8_cont(c3)) {
    return 4;
  }
  nrt_fatal_at(lx->line, lx->col, "UTF-8 として読めないバイト列があります");
  return 1;
}

static void advance_one(Lexer *lx) {
  unsigned char c = (unsigned char)lx->src[lx->pos];
  if (c == '\0') {
    return;
  }
  if (c == '\n') {
    lx->pos++;
    lx->line++;
    lx->col = 1;
    return;
  }
  size_t n = checked_u8_len(lx);
  lx->pos += n;
  lx->col++;
}

static void add_simple(Lexer *lx, TokKind kind, KwId kw, int line, int col) {
  Token tok;
  memset(&tok, 0, sizeof(tok));
  tok.kind = kind;
  tok.kw = kw;
  tok.line = line;
  tok.col = col;
  push_token(&lx->out, tok);
}

static bool raw_boundary_after_keyword(const char *s) {
  char c = *s;
  return c == '\0' || c == ' ' || c == '\t' || c == '\n' || c == '\r' ||
         nrt_starts_with(s, "　") || nrt_starts_with(s, "。") ||
         nrt_starts_with(s, "、") || nrt_starts_with(s, "「") ||
         nrt_starts_with(s, "」") || nrt_starts_with(s, "※") ||
         nrt_starts_with(s, "〈") || nrt_starts_with(s, "〉") ||
         nrt_starts_with(s, "（") || nrt_starts_with(s, "）") || c == '(' ||
         c == ')';
}

static const Keyword *match_keyword(const char *s, bool hard_only) {
  const Keyword *best = NULL;
  size_t best_len = 0;
  size_t nitems = sizeof(KEYWORDS) / sizeof(KEYWORDS[0]);
  for (size_t i = 0; i < nitems; i++) {
    if (hard_only && !KEYWORDS[i].hard_boundary) {
      continue;
    }
    size_t n = strlen(KEYWORDS[i].text);
    if (KEYWORDS[i].kw == KW_TOFU && strncmp(s, KEYWORDS[i].text, n) == 0 &&
        !nrt_starts_with(s + n, "業") && !raw_boundary_after_keyword(s + n)) {
      continue;
    }
    if (n > best_len && strncmp(s, KEYWORDS[i].text, n) == 0) {
      best = &KEYWORDS[i];
      best_len = n;
    }
  }
  return best;
}

static int digit_value(const char *s, size_t n) {
  if (n == strlen("一") && strncmp(s, "一", n) == 0) return 1;
  if (n == strlen("二") && strncmp(s, "二", n) == 0) return 2;
  if (n == strlen("三") && strncmp(s, "三", n) == 0) return 3;
  if (n == strlen("四") && strncmp(s, "四", n) == 0) return 4;
  if (n == strlen("五") && strncmp(s, "五", n) == 0) return 5;
  if (n == strlen("六") && strncmp(s, "六", n) == 0) return 6;
  if (n == strlen("七") && strncmp(s, "七", n) == 0) return 7;
  if (n == strlen("八") && strncmp(s, "八", n) == 0) return 8;
  if (n == strlen("九") && strncmp(s, "九", n) == 0) return 9;
  return -1;
}

static int small_unit_value(const char *s, size_t n) {
  if (n == strlen("十") && strncmp(s, "十", n) == 0) return 10;
  if (n == strlen("百") && strncmp(s, "百", n) == 0) return 100;
  if (n == strlen("千") && strncmp(s, "千", n) == 0) return 1000;
  return 0;
}

static int64_t large_unit_value(const char *s, size_t n) {
  if (n == strlen("万") && strncmp(s, "万", n) == 0) return 10000;
  if (n == strlen("億") && strncmp(s, "億", n) == 0) return 100000000;
  return 0;
}

typedef struct {
  const char *text;
  int value;
} YamatoNumber;

static const YamatoNumber YAMATO_ONES[] = {
  {"ひとつ", 1}, {"ひと", 1},     {"ふたつ", 2}, {"ふた", 2},
  {"みっつ", 3}, {"みつ", 3},     {"よっつ", 4}, {"よつ", 4},
  {"いつつ", 5}, {"いつ", 5},     {"むっつ", 6}, {"むつ", 6},
  {"ななつ", 7}, {"なな", 7},     {"やっつ", 8}, {"やつ", 8},
  {"ここのつ", 9}, {"ここの", 9},
};

static const YamatoNumber YAMATO_TENS[] = {
  {"とを", 10}, {"とお", 10}, {"とう", 10}, {"はたち", 20}, {"はた", 20},
  {"みそ", 30}, {"よそ", 40}, {"いそ", 50}, {"むそ", 60},
  {"ななそ", 70}, {"やそ", 80}, {"ここのそ", 90},
};

static bool yamato_exact(const YamatoNumber *items, size_t nitems, const char *s,
                         size_t len, int *out) {
  for (size_t i = 0; i < nitems; i++) {
    size_t n = strlen(items[i].text);
    if (len == n && strncmp(s, items[i].text, n) == 0) {
      *out = items[i].value;
      return true;
    }
  }
  return false;
}

static bool parse_yamato_kazu(const char *s, size_t len, int64_t *out) {
  int value = 0;
  if (yamato_exact(YAMATO_ONES, sizeof(YAMATO_ONES) / sizeof(YAMATO_ONES[0]), s,
                   len, &value) ||
      yamato_exact(YAMATO_TENS, sizeof(YAMATO_TENS) / sizeof(YAMATO_TENS[0]), s,
                   len, &value)) {
    *out = value;
    return true;
  }
  if ((len == strlen("もも") && strncmp(s, "もも", len) == 0) ||
      (len == strlen("ほ") && strncmp(s, "ほ", len) == 0)) {
    *out = 100;
    return true;
  }
  if (len == strlen("ち") && strncmp(s, "ち", len) == 0) {
    *out = 1000;
    return true;
  }
  if ((len == strlen("よろづ") && strncmp(s, "よろづ", len) == 0) ||
      (len == strlen("よろず") && strncmp(s, "よろず", len) == 0)) {
    *out = 10000;
    return true;
  }

  for (size_t i = 0; i < sizeof(YAMATO_TENS) / sizeof(YAMATO_TENS[0]); i++) {
    const char *ten = YAMATO_TENS[i].text;
    size_t ten_len = strlen(ten);
    if (len <= ten_len || strncmp(s, ten, ten_len) != 0) {
      continue;
    }
    int one = 0;
    if (yamato_exact(YAMATO_ONES, sizeof(YAMATO_ONES) / sizeof(YAMATO_ONES[0]),
                     s + ten_len, len - ten_len, &one)) {
      *out = YAMATO_TENS[i].value + one;
      return true;
    }
    const char *amari = "あまり";
    size_t amari_len = strlen(amari);
    if (len > ten_len + amari_len &&
        strncmp(s + ten_len, amari, amari_len) == 0 &&
        yamato_exact(YAMATO_ONES, sizeof(YAMATO_ONES) / sizeof(YAMATO_ONES[0]),
                     s + ten_len + amari_len, len - ten_len - amari_len, &one)) {
      *out = YAMATO_TENS[i].value + one;
      return true;
    }
  }
  return false;
}

static bool parse_kazu(const char *s, size_t len, int64_t *out) {
  if (len == 0) {
    return false;
  }
  bool ascii = true;
  size_t start = 0;
  if (s[0] == '-') {
    start = 1;
  }
  for (size_t i = start; i < len; i++) {
    if (!isdigit((unsigned char)s[i])) {
      ascii = false;
      break;
    }
  }
  if (ascii && start < len) {
    char *tmp = nrt_xstrndup(s, len);
    char *end = NULL;
    int64_t v = strtoll(tmp, &end, 10);
    bool ok = end != tmp && *end == '\0';
    free(tmp);
    if (ok) {
      *out = v;
      return true;
    }
  }
  if ((len == strlen("無") && strncmp(s, "無", len) == 0) ||
      (len == strlen("零") && strncmp(s, "零", len) == 0) ||
      (len == strlen("〇") && strncmp(s, "〇", len) == 0) ||
      (len == strlen("なし") && strncmp(s, "なし", len) == 0)) {
    *out = 0;
    return true;
  }
  if (parse_yamato_kazu(s, len, out)) {
    return true;
  }

  int64_t total = 0;
  int64_t section = 0;
  int digit = -1;
  bool saw = false;
  for (size_t pos = 0; pos < len;) {
    size_t clen = nrt_u8_len((unsigned char)s[pos]);
    if (pos + clen > len) {
      return false;
    }
    int d = digit_value(s + pos, clen);
    if (d >= 0) {
      digit = d;
      saw = true;
      pos += clen;
      continue;
    }
    int small = small_unit_value(s + pos, clen);
    if (small > 0) {
      section += (digit < 0 ? 1 : digit) * small;
      digit = -1;
      saw = true;
      pos += clen;
      continue;
    }
    int64_t large = large_unit_value(s + pos, clen);
    if (large > 0) {
      if (digit >= 0) {
        section += digit;
      }
      if (section == 0) {
        section = 1;
      }
      total += section * large;
      section = 0;
      digit = -1;
      saw = true;
      pos += clen;
      continue;
    }
    return false;
  }
  if (!saw) {
    return false;
  }
  if (digit >= 0) {
    section += digit;
  }
  *out = total + section;
  return true;
}

static bool is_space_at(const Lexer *lx) {
  unsigned char c = (unsigned char)lx->src[lx->pos];
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || at_bytes(lx, "　");
}

static bool is_stop_char_at(const Lexer *lx) {
  return lx->src[lx->pos] == '\0' || is_space_at(lx) || at_bytes(lx, "。") ||
         at_bytes(lx, "、") || at_bytes(lx, "「") || at_bytes(lx, "」") ||
         at_bytes(lx, "※") || at_bytes(lx, "〈") || at_bytes(lx, "〉") ||
         at_bytes(lx, "（") || at_bytes(lx, "）") || lx->src[lx->pos] == '(' ||
         lx->src[lx->pos] == ')';
}

static bool is_literal_boundary(const Lexer *lx, size_t pos) {
  char c = lx->src[pos];
  if (c == '\0' || c == ' ' || c == '\t' || c == '\n' || c == '\r' ||
      nrt_starts_with(lx->src + pos, "　") || nrt_starts_with(lx->src + pos, "。") ||
      nrt_starts_with(lx->src + pos, "、") || nrt_starts_with(lx->src + pos, "「") ||
      nrt_starts_with(lx->src + pos, "」") || nrt_starts_with(lx->src + pos, "※") ||
      nrt_starts_with(lx->src + pos, "〈") || nrt_starts_with(lx->src + pos, "〉") ||
      nrt_starts_with(lx->src + pos, "（") || nrt_starts_with(lx->src + pos, "）") ||
      c == '(' || c == ')') {
    return true;
  }
  return match_keyword(lx->src + pos, false) != NULL;
}

static void consider_kazu_match(const Lexer *lx, const char *text, int64_t value,
                                size_t *best_len, int64_t *best_value) {
  size_t len = strlen(text);
  if (len > *best_len && strncmp(lx->src + lx->pos, text, len) == 0 &&
      is_literal_boundary(lx, lx->pos + len)) {
    *best_len = len;
    *best_value = value;
  }
}

static bool match_yamato_literal(const Lexer *lx, size_t *len, int64_t *value) {
  size_t best_len = 0;
  int64_t best_value = 0;
  for (size_t i = 0; i < sizeof(YAMATO_ONES) / sizeof(YAMATO_ONES[0]); i++) {
    consider_kazu_match(lx, YAMATO_ONES[i].text, YAMATO_ONES[i].value, &best_len,
                        &best_value);
  }
  for (size_t i = 0; i < sizeof(YAMATO_TENS) / sizeof(YAMATO_TENS[0]); i++) {
    consider_kazu_match(lx, YAMATO_TENS[i].text, YAMATO_TENS[i].value, &best_len,
                        &best_value);
    for (size_t j = 0; j < sizeof(YAMATO_ONES) / sizeof(YAMATO_ONES[0]); j++) {
      char joined[128];
      snprintf(joined, sizeof(joined), "%s%s", YAMATO_TENS[i].text,
               YAMATO_ONES[j].text);
      consider_kazu_match(lx, joined, YAMATO_TENS[i].value + YAMATO_ONES[j].value,
                          &best_len, &best_value);
      snprintf(joined, sizeof(joined), "%sあまり%s", YAMATO_TENS[i].text,
               YAMATO_ONES[j].text);
      consider_kazu_match(lx, joined, YAMATO_TENS[i].value + YAMATO_ONES[j].value,
                          &best_len, &best_value);
    }
  }
  consider_kazu_match(lx, "もも", 100, &best_len, &best_value);
  consider_kazu_match(lx, "ほ", 100, &best_len, &best_value);
  consider_kazu_match(lx, "ち", 1000, &best_len, &best_value);
  consider_kazu_match(lx, "よろづ", 10000, &best_len, &best_value);
  consider_kazu_match(lx, "よろず", 10000, &best_len, &best_value);
  if (best_len == 0) {
    return false;
  }
  *len = best_len;
  *value = best_value;
  return true;
}

static void skip_fullwidth_paren(Lexer *lx) {
  while (lx->src[lx->pos] != '\0' && !at_bytes(lx, "）")) {
    advance_one(lx);
  }
  if (at_bytes(lx, "）")) {
    advance_one(lx);
  }
}

static void skip_angle_comment(Lexer *lx) {
  while (lx->src[lx->pos] != '\0' && !at_bytes(lx, "〉")) {
    advance_one(lx);
  }
  if (at_bytes(lx, "〉")) {
    advance_one(lx);
  }
}

static void warn_if_forbidden(Lexer *lx) {
  static const char *const BAD[] = {"候", "御座", "仍如件", "之儀", "段",
                                    "不然", "以て", "ハヽ"};
  for (size_t i = 0; i < sizeof(BAD) / sizeof(BAD[0]); i++) {
    if (at_bytes(lx, BAD[i])) {
      nrt_warn_at(lx->path, lx->line, lx->col,
                  "候文の語「%s」は仕様では用いません", BAD[i]);
    }
  }
}

TokenList kotowari_lex(const char *path, const char *src) {
  Lexer lx;
  memset(&lx, 0, sizeof(lx));
  lx.path = path;
  lx.src = src;
  lx.line = 1;
  lx.col = 1;

  while (lx.src[lx.pos] != '\0') {
    warn_if_forbidden(&lx);

    if (is_space_at(&lx)) {
      advance_one(&lx);
      continue;
    }
    if (at_bytes(&lx, "※")) {
      while (lx.src[lx.pos] != '\0' && lx.src[lx.pos] != '\n') {
        warn_if_forbidden(&lx);
        advance_one(&lx);
      }
      continue;
    }
    if (at_bytes(&lx, "〈")) {
      skip_angle_comment(&lx);
      continue;
    }
    if (at_bytes(&lx, "（")) {
      skip_fullwidth_paren(&lx);
      continue;
    }

    int line = lx.line;
    int col = lx.col;
    if (at_bytes(&lx, "。")) {
      advance_one(&lx);
      add_simple(&lx, T_KUTEN, KW_NONE, line, col);
      continue;
    }
    if (at_bytes(&lx, "、")) {
      advance_one(&lx);
      add_simple(&lx, T_TOUTEN, KW_NONE, line, col);
      continue;
    }
    if (lx.src[lx.pos] == '(') {
      advance_one(&lx);
      add_simple(&lx, T_LPAREN, KW_NONE, line, col);
      continue;
    }
    if (lx.src[lx.pos] == ')') {
      advance_one(&lx);
      add_simple(&lx, T_RPAREN, KW_NONE, line, col);
      continue;
    }
    if (at_bytes(&lx, "「")) {
      advance_one(&lx);
      size_t start = lx.pos;
      int sline = lx.line;
      int scol = lx.col;
      while (lx.src[lx.pos] != '\0' && !at_bytes(&lx, "」")) {
        advance_one(&lx);
      }
      if (lx.src[lx.pos] == '\0') {
        nrt_fatal_at(sline, scol, "言の閉じ括弧「」」がありません");
      }
      size_t len = lx.pos - start;
      Token tok;
      memset(&tok, 0, sizeof(tok));
      tok.kind = T_KOTO;
      tok.lex = nrt_xstrndup(lx.src + start, len);
      tok.len = len;
      tok.line = line;
      tok.col = col;
      push_token(&lx.out, tok);
      advance_one(&lx);
      continue;
    }

    size_t kazu_len = 0;
    int64_t kazu_value = 0;
    if (match_yamato_literal(&lx, &kazu_len, &kazu_value)) {
      size_t end = lx.pos + kazu_len;
      while (lx.pos < end) {
        advance_one(&lx);
      }
      Token tok;
      memset(&tok, 0, sizeof(tok));
      tok.kind = T_KAZU;
      tok.kazu = kazu_value;
      tok.line = line;
      tok.col = col;
      push_token(&lx.out, tok);
      continue;
    }

    const Keyword *kw = match_keyword(lx.src + lx.pos, false);
    if (kw != NULL) {
      size_t n = strlen(kw->text);
      size_t end = lx.pos + n;
      while (lx.pos < end) {
        advance_one(&lx);
      }
      Token tok;
      memset(&tok, 0, sizeof(tok));
      tok.kind = T_KW;
      tok.kw = kw->kw;
      tok.lex = kw->text;
      tok.len = n;
      tok.line = line;
      tok.col = col;
      push_token(&lx.out, tok);
      continue;
    }

    size_t start = lx.pos;
    while (!is_stop_char_at(&lx)) {
      if (lx.pos != start && match_keyword(lx.src + lx.pos, true) != NULL) {
        break;
      }
      advance_one(&lx);
    }
    if (lx.pos == start) {
      nrt_fatal_at(line, col, "読めない字があります");
    }
    size_t len = lx.pos - start;
    int64_t val = 0;
    Token tok;
    memset(&tok, 0, sizeof(tok));
    tok.line = line;
    tok.col = col;
    if (parse_kazu(lx.src + start, len, &val)) {
      tok.kind = T_KAZU;
      tok.kazu = val;
    } else {
      tok.kind = T_NA;
      tok.lex = nrt_xstrndup(lx.src + start, len);
      tok.len = len;
    }
    push_token(&lx.out, tok);
  }

  Token eof;
  memset(&eof, 0, sizeof(eof));
  eof.kind = T_EOF;
  eof.line = lx.line;
  eof.col = lx.col;
  push_token(&lx.out, eof);
  return lx.out;
}

const char *kotowari_kw_name(KwId kw) {
  for (size_t i = 0; i < sizeof(KEYWORDS) / sizeof(KEYWORDS[0]); i++) {
    if (KEYWORDS[i].kw == kw) {
      return KEYWORDS[i].text;
    }
  }
  return "?";
}

const char *kotowari_token_name(const Token *tok) {
  switch (tok->kind) {
    case T_KAZU:
      return "数";
    case T_KOTO:
      return "言";
    case T_NA:
      return "名";
    case T_KW:
      return kotowari_kw_name(tok->kw);
    case T_KUTEN:
      return "。";
    case T_TOUTEN:
      return "、";
    case T_LPAREN:
      return "(";
    case T_RPAREN:
      return ")";
    case T_EOF:
      return "終";
  }
  return "?";
}
