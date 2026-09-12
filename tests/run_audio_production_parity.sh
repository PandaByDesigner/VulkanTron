#!/bin/sh
set -eu

test_cxx=${CXX:-c++}
[ "$#" -le 1 ] || {
  printf 'usage: %s [plain|asan]\n' "$0" >&2
  exit 2
}
mode=${1:-plain}

case "$mode" in
  plain)
    compile_flags='-O2 -g'
    ;;
  asan)
    compile_flags='-O1 -g3 -fno-omit-frame-pointer -fno-optimize-sibling-calls -fsanitize=address,undefined -fno-sanitize-recover=all'
    ;;
  *)
    printf 'usage: %s [plain|asan]\n' "$0" >&2
    exit 2
    ;;
esac

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp_root=$(CDPATH= cd -- "${TMPDIR:-/tmp}" && pwd)
build_dir=$(mktemp -d "$tmp_root/gltron-audio-production-parity.XXXXXX")

cleanup() {
  status=$?
  trap - EXIT HUP INT TERM
  case "$build_dir" in
    "$tmp_root"/gltron-audio-production-parity.*)
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

for tool in sdl-config sdl2-config pkg-config "$test_cxx"; do
  command -v "$tool" >/dev/null 2>&1 || fail "missing required tool: $tool"
done

sources="
$repo_dir/tests/audio_production_parity.cpp
$repo_dir/nebu/audio/SoundSystem.cpp
$repo_dir/nebu/audio/Source.cpp
$repo_dir/nebu/audio/SourceCopy.cpp
$repo_dir/nebu/audio/SourceMusic.cpp
$repo_dir/nebu/audio/SourceSample.cpp
$repo_dir/nebu/audio/Source3D.cpp
$repo_dir/nebu/audio/SourceEngine.cpp
"
classic=$build_dir/audio-production-classic
native=$build_dir/audio-production-sdl2
native3=$build_dir/audio-production-sdl3

# These must remain distinct processes: SDL_sound is linked to SDL1 while the
# replacement production objects are compiled directly against SDL2.
"$test_cxx" -std=c++11 $compile_flags -Wall -Wextra -Werror \
  -I"$repo_dir/nebu/include" \
  $(sdl-config --cflags) \
  $sources \
  $(sdl-config --libs) -lSDL_sound -pthread \
  -o "$classic"

"$test_cxx" -std=c++11 $compile_flags -Wall -Wextra -Werror \
  -DGLTRON_SDL2_AUDIO \
  -I"$repo_dir/nebu/include" \
  $(sdl2-config --cflags) \
  $sources \
  $(sdl2-config --libs) -lmikmod -pthread \
  -o "$native"


"$test_cxx" -std=c++11 $compile_flags -Wall -Wextra -Werror \
  -DGLTRON_SDL3_AUDIO \
  -I"$repo_dir/nebu/include" \
  $(pkg-config --cflags sdl3) \
  $sources \
  $(pkg-config --libs sdl3) -lmikmod -pthread \
  -o "$native3"

run_probe() {
  executable=$1
  if [ "$mode" = asan ]; then
    ASAN_OPTIONS=abort_on_error=1:halt_on_error=1:detect_leaks=0 \
    UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
    SDL_AUDIODRIVER=dummy SDL_AUDIO_DRIVER=dummy "$executable" \
      "$repo_dir/data/game_crash.wav" \
      "$repo_dir/data/game_engine.wav" \
      "$repo_dir/data/game_recognizer.wav" \
      "$repo_dir/music/song_revenge_of_cats.it"
  else
    SDL_AUDIODRIVER=dummy SDL_AUDIO_DRIVER=dummy "$executable" \
      "$repo_dir/data/game_crash.wav" \
      "$repo_dir/data/game_engine.wav" \
      "$repo_dir/data/game_recognizer.wav" \
      "$repo_dir/music/song_revenge_of_cats.it"
  fi
}

classic_output=$(run_probe "$classic")
native_output=$(run_probe "$native")
native3_output=$(run_probe "$native3")

require_equal "production SDL1/SDL3 object output" \
  "$classic_output" "$native3_output"

require_equal "production SDL1/SDL2 object output" \
  "$classic_output" "$native_output"

require_equal "crash sample bytes" 277928 \
  "$(field "$native_output" crash_bytes)"
require_equal "crash sample hash" 5510bbe128637a7a \
  "$(field "$native_output" crash_fnv1a)"
require_equal "engine sample bytes" 231052 \
  "$(field "$native_output" engine_bytes)"
require_equal "engine sample hash" f8f9e0744d68dc8c \
  "$(field "$native_output" engine_fnv1a)"
require_equal "recognizer sample bytes" 394848 \
  "$(field "$native_output" recognizer_bytes)"
require_equal "recognizer sample hash" 3d30a780f00049ac \
  "$(field "$native_output" recognizer_fnv1a)"
require_equal "music prefix bytes" 4194304 \
  "$(field "$native_output" music_prefix_bytes)"
require_equal "music production prefix hash" cdc7967bc48612e9 \
  "$(field "$native_output" music_prefix_fnv1a)"
require_equal "music production prefix parity" \
  "$(field "$classic_output" music_prefix_fnv1a)" \
  "$(field "$native_output" music_prefix_fnv1a)"
require_equal "music reset prefix hash" e15d2b883819f9cb \
  "$(field "$native_output" reset_prefix_fnv1a)"
require_equal "finite-loop mixed length" 51208192 \
  "$(field "$native_output" loop_mixed_bytes)"
require_equal "SourceSample full-volume mix hash" 009755c4be87b8b3 \
  "$(field "$native_output" sample_full_fnv1a)"
require_equal "SourceSample half-volume mix hash" 8aa0d2f0f61a4ff2 \
  "$(field "$native_output" sample_half_fnv1a)"
require_equal "SourceCopy first-block mix hash" 75082e4d082e486d \
  "$(field "$native_output" copy_first_fnv1a)"
require_equal "SourceCopy overlap mix hash" b0e910e17f34df90 \
  "$(field "$native_output" copy_overlap_fnv1a)"
require_equal "one-shot boundary hash" bfa06d69a754e68b \
  "$(field "$native_output" one_shot_fnv1a)"
require_equal "finite-loop boundary hash" 8047ea38c1496720 \
  "$(field "$native_output" loop_boundary_fnv1a)"
require_equal "SourceCopy cursor independence" 1 \
  "$(field "$native_output" copy_independent)"
require_equal "one-shot restart" 1 \
  "$(field "$native_output" one_shot_reset)"
require_equal "sample loop boundary" 1 \
  "$(field "$native_output" sample_loop_boundary)"
require_equal "finite loop reset" 1 \
  "$(field "$native_output" loop_reset)"
require_equal "finite loop EOF stop" 1 \
  "$(field "$native_output" eof_stop)"
require_equal "audio wrapper lifecycle" 1 \
  "$(field "$native_output" wrappers)"
require_equal "spatial panning and doppler mix hash" e6f032ebaff3e61d \
  "$(field "$native_output" spatial_fnv1a)"
require_equal "player engine pitch and boost mix hash" e8f916760eb7a382 \
  "$(field "$native_output" engine_mix_fnv1a)"
require_equal "spatial mixer wrapped cursor" 193352 \
  "$(field "$native_output" spatial_cursor)"
require_equal "engine mixer wrapped cursor" 224864 \
  "$(field "$native_output" engine_cursor)"

printf 'PASS: production SourceSample PCM parity for all three effects\n'
printf 'PASS: production SourceSample full/half-volume and boundary parity\n'
printf 'PASS: production SourceCopy independent-cursor and overlap parity\n'
printf 'PASS: production spatial panning/doppler and engine boost/pitch parity\n'
printf 'PASS: production SourceMusic first 4 MiB parity (%s)\n' \
  "$(field "$native_output" music_prefix_fnv1a)"
printf 'PASS: production SourceMusic finite-loop reset (%s) and EOF stop\n' \
  "$(field "$native_output" reset_prefix_fnv1a)"
printf 'PASS: SDL1/SDL2/SDL3 production audio-object parity\n'
