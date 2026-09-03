#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build_dir=$(mktemp -d "${TMPDIR:-/tmp}/gltron-settings-regression.XXXXXX")
preferences_dir="$build_dir/preferences"
trap 'rm -rf "$build_dir"' EXIT HUP INT TERM
mkdir "$preferences_dir"

cc=${CC:-cc}
sdl_cflags=$(sdl-config --cflags)
mode=${1:-plain}

case "$mode" in
  plain)
    sanitizer_flags=
    ;;
  asan)
    sanitizer_flags='-O1 -fno-omit-frame-pointer -fsanitize=address,undefined'
    ;;
  *)
    echo "usage: $0 [plain|asan]" >&2
    exit 2
    ;;
esac

"$cc" -std=gnu99 -O2 -g -Wall -Wextra \
  -Wno-unused-parameter -Wno-sign-compare \
  -D_POSIX_C_SOURCE=200809L -DHAVE_MKSTEMP=1 \
  $sanitizer_flags \
  $sdl_cflags \
  -I"$repo_dir/nebu/include" \
  -I"$repo_dir/nebu/include/scripting" \
  -I"$repo_dir/src/include" \
  -I"$repo_dir/lua/include" \
  -I"$repo_dir/lua/src" \
  "$repo_dir/tests/settings_persistence_regression.c" \
  "$repo_dir/src/configuration/settings.c" \
  "$repo_dir/nebu/scripting/scripting.c" \
  "$repo_dir/lua/src/lapi.c" \
  "$repo_dir/lua/src/lcode.c" \
  "$repo_dir/lua/src/ldebug.c" \
  "$repo_dir/lua/src/ldo.c" \
  "$repo_dir/lua/src/lfunc.c" \
  "$repo_dir/lua/src/lgc.c" \
  "$repo_dir/lua/src/llex.c" \
  "$repo_dir/lua/src/lmem.c" \
  "$repo_dir/lua/src/lobject.c" \
  "$repo_dir/lua/src/lparser.c" \
  "$repo_dir/lua/src/lstate.c" \
  "$repo_dir/lua/src/lstring.c" \
  "$repo_dir/lua/src/ltable.c" \
  "$repo_dir/lua/src/ltests.c" \
  "$repo_dir/lua/src/ltm.c" \
  "$repo_dir/lua/src/lundump.c" \
  "$repo_dir/lua/src/lvm.c" \
  "$repo_dir/lua/src/lzio.c" \
  "$repo_dir/lua/src/lib/lauxlib.c" \
  "$repo_dir/lua/src/lib/lbaselib.c" \
  "$repo_dir/lua/src/lib/liolib.c" \
  "$repo_dir/lua/src/lib/lstrlib.c" \
  -lm -o "$build_dir/settings-persistence-regression"

if [ "$mode" = asan ]; then
  ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 \
  UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
    "$build_dir/settings-persistence-regression" "$repo_dir" "$preferences_dir"
else
  "$build_dir/settings-persistence-regression" "$repo_dir" "$preferences_dir"
fi
