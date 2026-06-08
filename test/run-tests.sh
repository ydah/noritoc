#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
TMPDIR=${TMPDIR:-/tmp}

ok_count=0

run_ok() {
  name=$1
  shift
  src="$ROOT/test/$name.nrt"
  expected="$ROOT/test/$name.expected"
  out=$(mktemp "$TMPDIR/noritoc.$name.out.XXXXXX")
  err=$(mktemp "$TMPDIR/noritoc.$name.err.XXXXXX")
  if ! "$ROOT/noritoc" "$src" "$@" >"$out" 2>"$err"; then
    echo "not ok: $name failed" >&2
    cat "$err" >&2
    exit 1
  fi
  if ! diff -u "$expected" "$out"; then
    echo "not ok: $name output mismatch" >&2
    cat "$err" >&2
    exit 1
  fi
  rm -f "$out" "$err"
  ok_count=$((ok_count + 1))
}

run_fail() {
  name=$1
  shift
  src="$ROOT/test/$name.nrt"
  out=$(mktemp "$TMPDIR/noritoc.$name.out.XXXXXX")
  err=$(mktemp "$TMPDIR/noritoc.$name.err.XXXXXX")
  if "$ROOT/noritoc" "$src" "$@" >"$out" 2>"$err"; then
    echo "not ok: $name unexpectedly succeeded" >&2
    cat "$out" >&2
    exit 1
  fi
  if ! grep -q "過ち有り" "$err"; then
    echo "not ok: $name did not report an error heading" >&2
    cat "$err" >&2
    exit 1
  fi
  rm -f "$out" "$err"
  ok_count=$((ok_count + 1))
}

run_invalid_utf8() {
  src=$(mktemp "$TMPDIR/noritoc.invalid_utf8.nrt.XXXXXX")
  out=$(mktemp "$TMPDIR/noritoc.invalid_utf8.out.XXXXXX")
  err=$(mktemp "$TMPDIR/noritoc.invalid_utf8.err.XXXXXX")
  printf '\377\n' >"$src"
  if "$ROOT/noritoc" "$src" >"$out" 2>"$err"; then
    echo "not ok: invalid_utf8 unexpectedly succeeded" >&2
    exit 1
  fi
  if ! grep -q "UTF-8" "$err"; then
    echo "not ok: invalid_utf8 did not report UTF-8" >&2
    cat "$err" >&2
    exit 1
  fi
  rm -f "$src" "$out" "$err"
  ok_count=$((ok_count + 1))
}

run_backend() {
  name=$1
  shift
  src="$ROOT/test/$name.nrt"
  expected="$ROOT/test/$name.expected"
  cfile=$(mktemp "$TMPDIR/noritoc.$name.c.XXXXXX")
  bin=$(mktemp "$TMPDIR/noritoc.$name.bin.XXXXXX")
  out=$(mktemp "$TMPDIR/noritoc.$name.backend.out.XXXXXX")
  err=$(mktemp "$TMPDIR/noritoc.$name.backend.err.XXXXXX")
  "$ROOT/noritoc" "$src" --backend=c "$@" >"$cfile"
  "${CC:-cc}" -x c -std=c11 -Wall -Wextra -Werror -O2 "$cfile" -o "$bin"
  if ! "$bin" >"$out" 2>"$err"; then
    echo "not ok: $name backend failed" >&2
    cat "$err" >&2
    exit 1
  fi
  if ! diff -u "$expected" "$out"; then
    echo "not ok: $name backend output mismatch" >&2
    cat "$err" >&2
    exit 1
  fi
  rm -f "$cfile" "$bin" "$out" "$err"
  ok_count=$((ok_count + 1))
}

run_backend_fail() {
  name=$1
  shift
  src="$ROOT/test/$name.nrt"
  cfile=$(mktemp "$TMPDIR/noritoc.$name.fail.c.XXXXXX")
  bin=$(mktemp "$TMPDIR/noritoc.$name.fail.bin.XXXXXX")
  out=$(mktemp "$TMPDIR/noritoc.$name.fail.out.XXXXXX")
  err=$(mktemp "$TMPDIR/noritoc.$name.fail.err.XXXXXX")
  "$ROOT/noritoc" "$src" --backend=c "$@" >"$cfile"
  "${CC:-cc}" -x c -std=c11 -Wall -Wextra -Werror -O2 "$cfile" -o "$bin"
  if "$bin" >"$out" 2>"$err"; then
    echo "not ok: $name backend unexpectedly succeeded" >&2
    cat "$out" >&2
    exit 1
  fi
  if ! grep -q "過ち有り" "$err"; then
    echo "not ok: $name backend did not report an error heading" >&2
    cat "$err" >&2
    exit 1
  fi
  rm -f "$cfile" "$bin" "$out" "$err"
  ok_count=$((ok_count + 1))
}

run_ok addition
run_ok factorial
run_ok sum
run_ok fizzbuzz
run_ok nested
run_ok warning_sourou
run_ok string_value
run_ok multiargs
run_ok yamato_numbers
run_ok local_scope
run_ok string_assign
run_ok norito_wrapper
run_ok ko_otsu --kana=ko-otsu
run_ok manyogana --kana=manyogana

"$ROOT/noritoc" "$ROOT/test/addition.nrt" --kaiseki >/dev/null
ok_count=$((ok_count + 1))

run_backend factorial
run_backend fizzbuzz
run_backend nested
run_backend string_value
run_backend multiargs
run_backend yamato_numbers
run_backend local_scope
run_backend string_assign
run_backend norito_wrapper
run_backend ko_otsu --kana=ko-otsu
run_backend manyogana --kana=manyogana
run_backend_fail error_undefined
run_backend_fail error_divzero

run_fail error_undefined
run_fail error_divzero
run_fail error_missing_period
run_fail error_missing_tonoru
run_fail error_dangling_if
run_fail error_dangling_for
run_fail error_empty_waza
run_fail error_ko_otsu_unmarked --kana=ko-otsu
run_fail error_ko_otsu_bad_marker --kana=ko-otsu
run_fail error_manyogana_modern_kana --kana=manyogana
run_invalid_utf8

echo "ok $ok_count tests"
