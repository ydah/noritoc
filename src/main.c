#include "fumikumi.h"
#include "kana.h"
#include "kazohi.h"
#include "kotowari.h"
#include "utsushi.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

static char *read_file(const char *path) {
  FILE *fp = fopen(path, "rb");
  if (fp == NULL) {
    nrt_fatal_at(0, 0, "%s を開けません：%s", path, strerror(errno));
  }
  if (fseek(fp, 0, SEEK_END) != 0) {
    fclose(fp);
    nrt_fatal_at(0, 0, "%s の大きさを読めません", path);
  }
  long size = ftell(fp);
  if (size < 0) {
    fclose(fp);
    nrt_fatal_at(0, 0, "%s の大きさを読めません", path);
  }
  rewind(fp);
  char *buf = nrt_xmalloc((size_t)size + 1);
  size_t got = fread(buf, 1, (size_t)size, fp);
  if (got != (size_t)size && ferror(fp)) {
    fclose(fp);
    nrt_fatal_at(0, 0, "%s を読めません", path);
  }
  fclose(fp);
  buf[got] = '\0';
  return buf;
}

static void usage(void) {
  fputs("使ひ方: noritoc <源文.nrt> [--kaiseki|--backend=c] [--kana=modern|--kana=ko-otsu|--kana=manyogana]\n",
        stderr);
}

int main(int argc, char **argv) {
  if (argc < 2 || argc > 5) {
    usage();
    return 2;
  }
  const char *path = argv[1];
  bool kaiseki = false;
  bool backend_c = false;
  KanaMode kana_mode = KANA_MODERN;
  for (int i = 2; i < argc; i++) {
    if (strcmp(argv[i], "--kaiseki") == 0) {
      kaiseki = true;
    } else if (strcmp(argv[i], "--backend=c") == 0) {
      backend_c = true;
    } else if (strcmp(argv[i], "--kana=modern") == 0) {
      kana_mode = KANA_MODERN;
    } else if (strcmp(argv[i], "--kana=ko-otsu") == 0 ||
               strcmp(argv[i], "--strict-kana") == 0) {
      kana_mode = KANA_KO_OTSU;
    } else if (strcmp(argv[i], "--kana=manyogana") == 0 ||
               strcmp(argv[i], "--manyogana") == 0) {
      kana_mode = KANA_MANYOGANA;
    } else {
      usage();
      return 2;
    }
  }
  if (kaiseki && backend_c) {
    usage();
    return 2;
  }

  char *src = read_file(path);
  char *normalized = kana_normalize(path, src, kana_mode);
  TokenList tokens = kotowari_lex(path, normalized);
  Program *program = fumikumi_parse(&tokens);

  if (kaiseki) {
    ki_print_program(program, stdout);
    return 0;
  }
  if (backend_c) {
    utsushi_emit(program, stdout);
    return 0;
  }
  kazohi_run(program);
  return 0;
}
