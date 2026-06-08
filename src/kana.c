#include "kana.h"

#include "common.h"

#include <string.h>

typedef struct {
  char *items;
  size_t len;
  size_t cap;
} ByteBuf;

typedef struct {
  const char *src;
  size_t pos;
  int line;
  int col;
  ByteBuf out;
} KanaScan;

typedef struct {
  const char *src;
  const char *dst;
} KanaMap;

static const char *const KO_OTSU_MARKABLE[] = {
  "き", "ぎ", "し", "じ", "ち", "ぢ", "に", "ひ", "び", "ぴ", "み", "り",
  "け", "げ", "せ", "ぜ", "て", "で", "ね", "へ", "べ", "ぺ", "め", "れ",
  "こ", "ご", "そ", "ぞ", "と", "ど", "の", "ほ", "ぼ", "ぽ", "も", "よ",
  "ろ", "を",
};

static const KanaMap MANYOGANA[] = {
  {"阿", "あ"}, {"伊", "い"}, {"宇", "う"}, {"衣", "え"}, {"於", "お"},
  {"可", "か"}, {"吉", "き"}, {"久", "く"}, {"家", "け"}, {"古", "こ"},
  {"我", "が"}, {"藝", "ぎ"}, {"具", "ぐ"}, {"宜", "げ"}, {"吾", "ご"},
  {"佐", "さ"}, {"之", "し"}, {"須", "す"}, {"世", "せ"}, {"曽", "そ"},
  {"邪", "ざ"}, {"自", "じ"}, {"受", "ず"}, {"是", "ぜ"}, {"叙", "ぞ"},
  {"多", "た"}, {"知", "ち"}, {"都", "つ"}, {"弖", "て"}, {"登", "と"},
  {"太", "だ"}, {"提", "で"}, {"豆", "づ"},
  {"奈", "な"}, {"仁", "に"}, {"奴", "ぬ"}, {"祢", "ね"}, {"乃", "の"},
  {"波", "は"}, {"比", "ひ"}, {"布", "ふ"}, {"敝", "へ"}, {"保", "ほ"},
  {"婆", "ば"}, {"備", "び"}, {"夫", "ぶ"}, {"倍", "べ"}, {"煩", "ぼ"},
  {"麻", "ま"}, {"美", "み"}, {"牟", "む"}, {"売", "め"}, {"毛", "も"},
  {"夜", "や"}, {"由", "ゆ"}, {"与", "よ"},
  {"良", "ら"}, {"利", "り"}, {"流", "る"}, {"礼", "れ"}, {"呂", "ろ"},
  {"和", "わ"}, {"乎", "を"},
};

static void buf_push(ByteBuf *buf, const char *s, size_t n) {
  if (buf->len + n + 1 > buf->cap) {
    while (buf->len + n + 1 > buf->cap) {
      buf->cap = buf->cap == 0 ? 256 : buf->cap * 2;
    }
    buf->items = nrt_xrealloc(buf->items, buf->cap);
  }
  memcpy(buf->items + buf->len, s, n);
  buf->len += n;
  buf->items[buf->len] = '\0';
}

static bool starts_at(const KanaScan *scan, const char *s) {
  return nrt_starts_with(scan->src + scan->pos, s);
}

static bool is_u8_cont(unsigned char c) {
  return (c & 0xC0u) == 0x80u;
}

static size_t checked_len(const KanaScan *scan) {
  unsigned char c0 = (unsigned char)scan->src[scan->pos];
  if (c0 < 0x80u) {
    return 1;
  }
  unsigned char c1 = (unsigned char)scan->src[scan->pos + 1];
  unsigned char c2 = (unsigned char)scan->src[scan->pos + 2];
  unsigned char c3 = (unsigned char)scan->src[scan->pos + 3];
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
  nrt_fatal_at(scan->line, scan->col, "UTF-8 として読めないバイト列があります");
  return 1;
}

static void advance(KanaScan *scan, size_t n) {
  size_t end = scan->pos + n;
  while (scan->pos < end) {
    if (scan->src[scan->pos] == '\n') {
      scan->pos++;
      scan->line++;
      scan->col = 1;
    } else {
      size_t len = checked_len(scan);
      scan->pos += len;
      scan->col++;
    }
  }
}

static void copy_advance(KanaScan *scan, size_t n) {
  buf_push(&scan->out, scan->src + scan->pos, n);
  advance(scan, n);
}

static void copy_until(KanaScan *scan, const char *end) {
  while (scan->src[scan->pos] != '\0' && !starts_at(scan, end)) {
    copy_advance(scan, checked_len(scan));
  }
  if (starts_at(scan, end)) {
    copy_advance(scan, strlen(end));
  }
}

static bool is_hiragana_or_katakana(uint32_t cp) {
  return (cp >= 0x3040u && cp <= 0x30FFu) || (cp >= 0x31F0u && cp <= 0x31FFu);
}

static uint32_t decode_current(const KanaScan *scan, size_t *len_out) {
  size_t len = checked_len(scan);
  const unsigned char *s = (const unsigned char *)scan->src + scan->pos;
  *len_out = len;
  if (len == 1) {
    return s[0];
  }
  if (len == 2) {
    return ((uint32_t)(s[0] & 0x1Fu) << 6) | (uint32_t)(s[1] & 0x3Fu);
  }
  if (len == 3) {
    return ((uint32_t)(s[0] & 0x0Fu) << 12) | ((uint32_t)(s[1] & 0x3Fu) << 6) |
           (uint32_t)(s[2] & 0x3Fu);
  }
  return ((uint32_t)(s[0] & 0x07u) << 18) | ((uint32_t)(s[1] & 0x3Fu) << 12) |
         ((uint32_t)(s[2] & 0x3Fu) << 6) | (uint32_t)(s[3] & 0x3Fu);
}

static bool match_markable(const KanaScan *scan, size_t *len) {
  for (size_t i = 0; i < sizeof(KO_OTSU_MARKABLE) / sizeof(KO_OTSU_MARKABLE[0]); i++) {
    size_t n = strlen(KO_OTSU_MARKABLE[i]);
    if (strncmp(scan->src + scan->pos, KO_OTSU_MARKABLE[i], n) == 0) {
      *len = n;
      return true;
    }
  }
  return false;
}

static bool at_marker_after(const KanaScan *scan, size_t kana_len) {
  return nrt_starts_with(scan->src + scan->pos + kana_len, "甲") ||
         nrt_starts_with(scan->src + scan->pos + kana_len, "乙");
}

static void normalize_ko_otsu_char(KanaScan *scan) {
  size_t kana_len = 0;
  if (match_markable(scan, &kana_len)) {
    if (!at_marker_after(scan, kana_len)) {
      nrt_fatal_at(scan->line, scan->col,
                   "甲乙厳格モードでは特殊仮名に「甲」または「乙」が要ります");
    }
    buf_push(&scan->out, scan->src + scan->pos, kana_len);
    advance(scan, kana_len);
    advance(scan, strlen(starts_at(scan, "甲") ? "甲" : "乙"));
    return;
  }
  if (starts_at(scan, "甲") || starts_at(scan, "乙")) {
    nrt_fatal_at(scan->line, scan->col,
                 "甲乙標識は特殊仮名の直後に置いてください");
  }
  copy_advance(scan, checked_len(scan));
}

static bool normalize_manyogana_char(KanaScan *scan) {
  for (size_t i = 0; i < sizeof(MANYOGANA) / sizeof(MANYOGANA[0]); i++) {
    size_t n = strlen(MANYOGANA[i].src);
    if (strncmp(scan->src + scan->pos, MANYOGANA[i].src, n) == 0) {
      buf_push(&scan->out, MANYOGANA[i].dst, strlen(MANYOGANA[i].dst));
      advance(scan, n);
      return true;
    }
  }
  size_t len = 0;
  uint32_t cp = decode_current(scan, &len);
  if (is_hiragana_or_katakana(cp)) {
    nrt_fatal_at(scan->line, scan->col,
                 "万葉仮名厳格モードではコード部に仮名を直接書けません");
  }
  copy_advance(scan, len);
  return false;
}

static bool handle_excluded(KanaScan *scan) {
  if (starts_at(scan, "「")) {
    copy_advance(scan, strlen("「"));
    copy_until(scan, "」");
    return true;
  }
  if (starts_at(scan, "※")) {
    while (scan->src[scan->pos] != '\0' && scan->src[scan->pos] != '\n') {
      copy_advance(scan, checked_len(scan));
    }
    return true;
  }
  if (starts_at(scan, "〈")) {
    copy_advance(scan, strlen("〈"));
    copy_until(scan, "〉");
    return true;
  }
  if (starts_at(scan, "（")) {
    copy_advance(scan, strlen("（"));
    copy_until(scan, "）");
    return true;
  }
  return false;
}

char *kana_normalize(const char *path, const char *src, KanaMode mode) {
  (void)path;
  if (mode == KANA_MODERN) {
    return (char *)src;
  }

  KanaScan scan;
  memset(&scan, 0, sizeof(scan));
  scan.src = src;
  scan.line = 1;
  scan.col = 1;

  while (scan.src[scan.pos] != '\0') {
    if (handle_excluded(&scan)) {
      continue;
    }
    if (mode == KANA_KO_OTSU) {
      normalize_ko_otsu_char(&scan);
    } else {
      (void)normalize_manyogana_char(&scan);
    }
  }
  buf_push(&scan.out, "", 1);
  return scan.out.items;
}
