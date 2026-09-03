#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build_dir=$(mktemp -d "${TMPDIR:-/tmp}/gltron-faithful-regression.XXXXXX")
trap 'rm -rf "$build_dir"' EXIT HUP INT TERM

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
  -Wno-unused-parameter -Wno-absolute-value -Wno-empty-body \
  -Wno-sign-compare -Wno-unused-but-set-variable \
  $sanitizer_flags \
  $sdl_cflags \
  -I"$repo_dir/nebu/include" \
  -I"$repo_dir/src/include" \
  -I"$repo_dir/lua/include" \
  "$repo_dir/tests/classic_gameplay_regression.c" \
  "$repo_dir/src/game/engine.c" \
  "$repo_dir/src/game/event.c" \
  "$repo_dir/src/game/computer.c" \
  "$repo_dir/src/game/computer_utilities.c" \
  "$repo_dir/src/game/globals.c" \
  "$repo_dir/src/video/player_visual.c" \
  "$repo_dir/nebu/base/vector.c" \
  "$repo_dir/nebu/base/random.c" \
  "$repo_dir/nebu/base/util.c" \
  -lm -o "$build_dir/classic-gameplay-regression"

"$cc" -std=gnu99 -O2 -g -Wall -Wextra \
  -Wno-unused-parameter -Wno-absolute-value -Wno-empty-body \
  -Wno-sign-compare -Wno-unused-but-set-variable \
  -ffunction-sections -fdata-sections \
  $sanitizer_flags \
  $sdl_cflags \
  -I"$repo_dir/nebu/include" \
  -I"$repo_dir/src/include" \
  -I"$repo_dir/lua/include" \
  "$repo_dir/tests/local_multiplayer_regression.c" \
  "$repo_dir/src/game/engine.c" \
  "$repo_dir/src/game/event.c" \
  "$repo_dir/src/game/computer.c" \
  "$repo_dir/src/game/computer_utilities.c" \
  "$repo_dir/src/game/globals.c" \
  "$repo_dir/src/video/player_visual.c" \
  "$repo_dir/src/video/video.c" \
  "$repo_dir/src/video/display_layout.c" \
  "$repo_dir/nebu/base/vector.c" \
  "$repo_dir/nebu/base/random.c" \
  "$repo_dir/nebu/base/util.c" \
  -Wl,--gc-sections \
  -lm -o "$build_dir/local-multiplayer-regression"

"$cc" -std=gnu99 -O2 -g -Wall -Wextra -Werror \
  -Wno-unused-parameter \
  $sanitizer_flags \
  $sdl_cflags \
  -I"$repo_dir/nebu/include" \
  -I"$repo_dir/src/include" \
  "$repo_dir/tests/camera_regression.c" \
  "$repo_dir/src/game/camera.c" \
  "$repo_dir/src/game/globals.c" \
  -lm -o "$build_dir/camera-regression"

"$cc" -std=gnu99 -O2 -g -Wall -Wextra \
  $sanitizer_flags \
  $sdl_cflags \
  -I"$repo_dir/nebu/include" \
  -I"$repo_dir/src/include" \
  "$repo_dir/tests/display_layout_test.c" \
  "$repo_dir/src/game/globals.c" \
  "$repo_dir/src/video/display_layout.c" \
  -lm -o "$build_dir/display-layout-test"

"$cc" -std=gnu99 -O2 -g -Wall -Wextra \
  $sanitizer_flags \
  $sdl_cflags \
  -I"$repo_dir/nebu/include" \
  -I"$repo_dir/src/include" \
  "$repo_dir/tests/hud_layout_test.c" \
  "$repo_dir/src/game/globals.c" \
  "$repo_dir/src/video/display_layout.c" \
  "$repo_dir/src/video/hud_layout.c" \
  -lm -o "$build_dir/hud-layout-test"

"$cc" -std=gnu99 -O2 -g -Wall -Wextra -Werror \
  -Wno-unused-parameter \
  -ffunction-sections -fdata-sections \
  $sanitizer_flags \
  $sdl_cflags \
  -I"$repo_dir/nebu/include" \
  -I"$repo_dir/src/include" \
  -I"$repo_dir/lua/include" \
  "$repo_dir/tests/input_binding_regression.c" \
  "$repo_dir/src/input/input.c" \
  -Wl,--gc-sections \
  -lm -o "$build_dir/input-binding-regression"

if [ "$mode" = asan ]; then
  ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 \
  UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
    "$build_dir/classic-gameplay-regression"
  ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 \
  UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
    "$build_dir/local-multiplayer-regression"
  ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 \
  UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
    "$build_dir/camera-regression"
  ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 \
  UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
    "$build_dir/display-layout-test"
  ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 \
  UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
    "$build_dir/hud-layout-test"
  ASAN_OPTIONS=detect_leaks=0:halt_on_error=1 \
  UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
    "$build_dir/input-binding-regression"
else
  "$build_dir/classic-gameplay-regression"
  "$build_dir/local-multiplayer-regression"
  "$build_dir/camera-regression"
  "$build_dir/display-layout-test"
  "$build_dir/hud-layout-test"
  "$build_dir/input-binding-regression"
fi

"$repo_dir/tests/run_sdl_backend_regression.sh" "$mode"
"$repo_dir/tests/run_settings_persistence_regression.sh" "$mode"
