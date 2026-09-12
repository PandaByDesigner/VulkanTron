#!/bin/sh
set -eu
repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build_dir=$(mktemp -d "${TMPDIR:-/tmp}/gltron-artpack-regression.XXXXXX")
trap 'rm -rf "$build_dir"' EXIT HUP INT TERM
case "${1:-plain}" in
  plain) flags='-O2 -g' ;;
  asan) flags='-O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=all' ;;
  *) printf 'usage: %s [plain|asan]\n' "$0" >&2; exit 2 ;;
esac
"${CC:-cc}" -std=gnu99 $flags -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare -Wno-implicit-fallthrough \
  -DGLTRON_USE_SDL3=1 -DHAVE_MKSTEMP=1 -D_POSIX_C_SOURCE=200809L \
  -I"$repo_dir/nebu/include" -I"$repo_dir/nebu/include/scripting" \
  -I"$repo_dir/src/include" -I"$repo_dir/lua/include" -I"$repo_dir/lua/src" \
  "$repo_dir/tests/artpack_enumeration_test.c" "$repo_dir/src/video/artpack.c" \
  "$repo_dir/nebu/scripting/scripting.c" \
  "$repo_dir"/lua/src/*.c "$repo_dir"/lua/src/lib/*.c -lm \
  -o "$build_dir/artpack-enumeration-test"
ASAN_OPTIONS=detect_leaks=${GLTRON_TEST_LEAKS:-0}:halt_on_error=1 \
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
  "$build_dir/artpack-enumeration-test" "$repo_dir"
