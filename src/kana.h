#ifndef NORITO_KANA_H
#define NORITO_KANA_H

typedef enum {
  KANA_MODERN,
  KANA_KO_OTSU,
  KANA_MANYOGANA
} KanaMode;

char *kana_normalize(const char *path, const char *src, KanaMode mode);

#endif
