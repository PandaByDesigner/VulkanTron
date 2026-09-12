#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp_root=$(CDPATH= cd -- "${TMPDIR:-/tmp}" && pwd)
build_dir=$(mktemp -d "$tmp_root/gltron-audio-decoder-parity.XXXXXX")
test_cc=${CC:-cc}

cleanup() {
  status=$?
  trap - EXIT HUP INT TERM
  case "$build_dir" in
    "$tmp_root"/gltron-audio-decoder-parity.*)
      rm -rf -- "$build_dir"
      ;;
    *)
      printf 'refusing to clean unexpected path: %s\n' "$build_dir" >&2
      status=1
      ;;
  esac
  exit "$status"
}
trap cleanup EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

fail() {
  printf 'FAIL: %s\n' "$*" >&2
  exit 1
}

field() {
  line=$1
  name=$2
  printf '%s\n' "$line" | tr ' ' '\n' | sed -n "s/^$name=//p"
}

require_equal() {
  label=$1
  expected=$2
  actual=$3
  [ "$actual" = "$expected" ] ||
    fail "$label: expected $expected, got $actual"
}

for tool in sdl-config sdl2-config pkg-config "$test_cc"; do
  command -v "$tool" >/dev/null 2>&1 || fail "missing required tool: $tool"
done

classic=$build_dir/audio-decoder-classic
native=$build_dir/audio-decoder-sdl2
native3=$build_dir/audio-decoder-sdl3

# Keep the incompatible SDL1/SDL_sound and SDL2 paths in separate processes.
"$test_cc" -std=c99 -O2 -g -Wall -Wextra -Werror \
  -DGLTRON_AUDIO_CLASSIC \
  $(sdl-config --cflags) \
  "$repo_dir/tests/audio_decoder_parity.c" \
  $(sdl-config --libs) -lSDL_sound \
  -o "$classic"

"$test_cc" -std=c99 -O2 -g -Wall -Wextra -Werror \
  $(sdl2-config --cflags) \
  "$repo_dir/tests/audio_decoder_parity.c" \
  $(sdl2-config --libs) -lmikmod \
  -o "$native"

"$test_cc" -std=c99 -O2 -g -Wall -Wextra -Werror \
  -DGLTRON_SDL3_AUDIO $(pkg-config --cflags sdl3) \
  "$repo_dir/tests/audio_decoder_parity.c" \
  $(pkg-config --libs sdl3) -lmikmod \
  -o "$native3"

check_common_format() {
  label=$1
  output=$2

  require_equal "$label format" 32784 "$(field "$output" format)"
  require_equal "$label channels" 2 "$(field "$output" channels)"
  require_equal "$label rate" 22050 "$(field "$output" rate)"
  require_equal "$label EOF" 1 "$(field "$output" eof)"
}

check_wav() {
  asset=$1
  expected_bytes=$2
  expected_hash=$3
  path=$repo_dir/data/$asset
  classic_output=$(SDL_AUDIODRIVER=dummy "$classic" wav "$path")
  native_output=$(SDL_AUDIODRIVER=dummy "$native" wav "$path")

  native3_output=$(SDL_AUDIO_DRIVER=dummy "$native3" wav "$path")
  require_equal "$asset SDL3 backend parity" "$classic_output" "$native3_output"
  check_common_format "classic $asset" "$classic_output"
  check_common_format "SDL2 $asset" "$native_output"
  require_equal "$asset backend parity" "$classic_output" "$native_output"
  require_equal "$asset PCM byte count" "$expected_bytes" \
    "$(field "$native_output" total_bytes)"
  require_equal "$asset PCM hash" "$expected_hash" \
    "$(field "$native_output" prefix_fnv1a)"

  printf 'PASS: %s exact SDL1/SDL2/SDL3 PCM (%s bytes, %s)\n' \
    "$asset" "$expected_bytes" "$expected_hash"
}

check_wav game_crash.wav 277928 5510bbe128637a7a
check_wav game_engine.wav 231052 f8f9e0744d68dc8c
check_wav game_recognizer.wav 394848 3d30a780f00049ac

track=$repo_dir/music/song_revenge_of_cats.it
classic_track=$(SDL_AUDIODRIVER=dummy "$classic" it "$track")
native_track=$(SDL_AUDIODRIVER=dummy "$native" it "$track")

native3_track=$(SDL_AUDIO_DRIVER=dummy "$native3" it "$track")
require_equal "SDL2/SDL3 tracker parity" "$native_track" "$native3_track"

check_common_format "classic tracker" "$classic_track"
check_common_format "SDL2 tracker" "$native_track"
require_equal "classic tracker prefix size" 4194304 \
  "$(field "$classic_track" prefix_bytes)"
require_equal "SDL2 tracker prefix size" 4194304 \
  "$(field "$native_track" prefix_bytes)"
require_equal "tracker reference hash" cdc7967bc48612e9 \
  "$(field "$classic_track" prefix_fnv1a)"
require_equal "tracker prefix parity" \
  "$(field "$classic_track" prefix_fnv1a)" \
  "$(field "$native_track" prefix_fnv1a)"

classic_bytes=$(field "$classic_track" total_bytes)
native_bytes=$(field "$native_track" total_bytes)
case "$classic_bytes:$native_bytes" in
  *[!0-9:]*|:*|*:)
    fail "invalid tracker byte counts: $classic_bytes / $native_bytes"
    ;;
esac
if [ "$classic_bytes" -ge "$native_bytes" ]; then
  duration_delta=$((classic_bytes - native_bytes))
else
  duration_delta=$((native_bytes - classic_bytes))
fi

# Module EOF is only observable at a decode-block boundary. SDL_sound itself
# varies by up to one 8192-byte block when its decode buffer size changes, so
# exact PCM is required for the stable prefix while duration gets that bound.
[ "$duration_delta" -le 8192 ] ||
  fail "tracker EOF differs by $duration_delta bytes (limit 8192)"

printf 'PASS: tracker first 4 MiB exact (%s); EOF delta %s bytes ' \
  "$(field "$native_track" prefix_fnv1a)" "$duration_delta"
printf '(classic %s, SDL2 %s)\n' "$classic_bytes" "$native_bytes"
printf 'PASS: SDL2/SDL3-native decoder/effect parity\n'
