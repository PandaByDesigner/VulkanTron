#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
tmp_root=$(CDPATH= cd -- "${TMPDIR:-/tmp}" && pwd)
build_root=$(mktemp -d "$tmp_root/gltron-sdl-build-matrix.XXXXXX")
make_cmd=${MAKE:-make}
jobs=${GLTRON_BUILD_JOBS:-2}

cleanup() {
  status=$?
  trap - EXIT HUP INT TERM
  case "$build_root" in
    "$tmp_root"/gltron-sdl-build-matrix.*)
      rm -rf -- "$build_root"
      ;;
    *)
      printf 'refusing to clean unexpected path: %s\n' "$build_root" >&2
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

case "$jobs" in
  ''|*[!0-9]*)
    fail "GLTRON_BUILD_JOBS must be a positive integer"
    ;;
  *[1-9]*)
    ;;
  *)
    fail "GLTRON_BUILD_JOBS must be a positive integer"
    ;;
esac

for tool in sdl-config sdl2-config readelf "$make_cmd"; do
  command -v "$tool" >/dev/null 2>&1 || fail "missing required tool: $tool"
done

sdl1_config=$(command -v sdl-config)
sdl2_config=$(command -v sdl2-config)
sdl1_cflags=$($sdl1_config --cflags)
sdl2_cflags=$($sdl2_config --cflags)

configure_build() {
  build_dir=$1
  sdl_config=$2
  shift 2

  mkdir "$build_dir"
  if ! (
    cd "$build_dir"
    SDL_CONFIG="$sdl_config" \
    SDL_VIDEODRIVER=dummy \
    SDL_AUDIODRIVER=dummy \
      "$repo_dir/configure" --disable-warn "$@"
  ) >"$build_dir/configure.log" 2>&1; then
    tail -n 80 "$build_dir/configure.log" >&2
    fail "configure failed in $build_dir"
  fi
}

build_all() {
  build_dir=$1
  shift

  # These command-line overrides make timestamp drift harmless: make cannot
  # regenerate checked-in legacy Autotools files in the source tree.
  if ! (
    cd "$build_dir"
    "$make_cmd" -j"$jobs" \
      AUTOCONF=: AUTOMAKE=: ACLOCAL=: AUTOHEADER=: \
      "$@"
  ) >"$build_dir/make.log" 2>&1; then
    tail -n 120 "$build_dir/make.log" >&2
    fail "full build failed in $build_dir"
  fi
}

write_needed() {
  binary=$1
  output=$2
  raw_output=$output.raw

  test -x "$binary" || fail "missing executable: $binary"
  readelf -d "$binary" >"$raw_output"
  sed -n 's/.*Shared library: \[\([^]]*\)\].*/\1/p' \
    "$raw_output" >"$output"
  test -s "$output" || fail "no DT_NEEDED entries found in $binary"
}

require_needed() {
  pattern=$1
  label=$2
  needed_file=$3

  grep -Eq "$pattern" "$needed_file" || {
    sed 's/^/  /' "$needed_file" >&2
    fail "$label is absent from DT_NEEDED"
  }
}

reject_needed() {
  pattern=$1
  label=$2
  needed_file=$3

  if grep -Eq "$pattern" "$needed_file"; then
    sed 's/^/  /' "$needed_file" >&2
    fail "$label unexpectedly appears in DT_NEEDED"
  fi
}

require_build_define() {
  define=$1
  build_dir=$2

  grep -F -- "-D$define=1" "$build_dir/make.log" >/dev/null ||
    fail "$define is absent from full-build compiler commands in $build_dir"
}

reject_build_define() {
  define=$1
  build_dir=$2

  if grep -F -- "-D$define=1" "$build_dir/make.log" >/dev/null; then
    fail "$define unexpectedly appears in full-build compiler commands in $build_dir"
  fi
}

sdl1_dir=$build_root/sdl1-classic
configure_build "$sdl1_dir" "$sdl1_config"
build_all "$sdl1_dir"
reject_build_define GLTRON_SDL2_AUDIO "$sdl1_dir"
reject_build_define GLTRON_NO_SOUND "$sdl1_dir"
sdl1_needed=$sdl1_dir/gltron.needed
write_needed "$sdl1_dir/gltron" "$sdl1_needed"
require_needed '^libSDL-1\.2\.so' 'SDL1' "$sdl1_needed"
reject_needed '^libSDL2-' 'SDL2' "$sdl1_needed"
require_needed '^libSDL_sound-' 'SDL_sound' "$sdl1_needed"
require_needed '^libmikmod' 'libmikmod' "$sdl1_needed"
require_needed '^libvorbisfile' 'libvorbisfile' "$sdl1_needed"
require_needed '^libvorbis\.so' 'libvorbis' "$sdl1_needed"
require_needed '^libogg' 'libogg' "$sdl1_needed"
printf 'PASS: classic SDL1 full build DT_NEEDED\n'
sed 's/^/  /' "$sdl1_needed"

sdl1_noaudio_dir=$build_root/sdl1-no-audio
configure_build "$sdl1_noaudio_dir" "$sdl1_config" --disable-sound
sdl1_matrix_cflags="-O2 -g -DSEPARATOR=47 -DGLTRON_BUILD_MATRIX_CFLAGS=1 -I$repo_dir/lua/src -I$repo_dir/lua/include $sdl1_cflags"
sdl1_matrix_cxxflags="-O2 -g -DGLTRON_BUILD_MATRIX_CXXFLAGS=1 -I$repo_dir/lua/src -I$repo_dir/lua/include $sdl1_cflags"
build_all "$sdl1_noaudio_dir" \
  "CFLAGS=$sdl1_matrix_cflags" \
  "CXXFLAGS=$sdl1_matrix_cxxflags"
reject_build_define GLTRON_SDL2_AUDIO "$sdl1_noaudio_dir"
require_build_define GLTRON_NO_SOUND "$sdl1_noaudio_dir"
sdl1_noaudio_needed=$sdl1_noaudio_dir/gltron.needed
write_needed "$sdl1_noaudio_dir/gltron" "$sdl1_noaudio_needed"
require_needed '^libSDL-1\.2\.so' 'SDL1' "$sdl1_noaudio_needed"
reject_needed '^libSDL2-' 'SDL2' "$sdl1_noaudio_needed"
reject_needed '^libSDL_sound-' 'SDL_sound' "$sdl1_noaudio_needed"
reject_needed '^libmikmod' 'libmikmod' "$sdl1_noaudio_needed"
reject_needed '^libvorbis' 'libvorbis' "$sdl1_noaudio_needed"
reject_needed '^libogg' 'libogg' "$sdl1_noaudio_needed"
printf 'PASS: SDL1 no-audio full build DT_NEEDED (overridden CFLAGS/CXXFLAGS)\n'
sed 's/^/  /' "$sdl1_noaudio_needed"

matrix_cflags="-O2 -g -DSEPARATOR=47 -DGLTRON_BUILD_MATRIX_CFLAGS=1 -I$repo_dir/lua/src -I$repo_dir/lua/include $sdl2_cflags"
matrix_cxxflags="-O2 -g -DGLTRON_BUILD_MATRIX_CXXFLAGS=1 -I$repo_dir/lua/src -I$repo_dir/lua/include $sdl2_cflags"

sdl2_audio_dir=$build_root/sdl2-audio
configure_build "$sdl2_audio_dir" "$sdl2_config"
build_all "$sdl2_audio_dir" \
  "CFLAGS=$matrix_cflags" \
  "CXXFLAGS=$matrix_cxxflags"
require_build_define GLTRON_SDL2_AUDIO "$sdl2_audio_dir"
reject_build_define GLTRON_NO_SOUND "$sdl2_audio_dir"
sdl2_audio_needed=$sdl2_audio_dir/gltron.needed
write_needed "$sdl2_audio_dir/gltron" "$sdl2_audio_needed"
require_needed '^libSDL2-' 'SDL2' "$sdl2_audio_needed"
require_needed '^libmikmod' 'libmikmod' "$sdl2_audio_needed"
reject_needed '^libSDL-1\.2\.so' 'SDL1' "$sdl2_audio_needed"
reject_needed '^libSDL_sound-' 'SDL_sound' "$sdl2_audio_needed"
reject_needed '^libSDL2_mixer' 'SDL2_mixer' "$sdl2_audio_needed"
reject_needed '^libsmpeg' 'smpeg' "$sdl2_audio_needed"
reject_needed '^libvorbis' 'libvorbis' "$sdl2_audio_needed"
reject_needed '^libogg' 'libogg' "$sdl2_audio_needed"
reject_needed '^libopenmpt' 'libopenmpt' "$sdl2_audio_needed"
reject_needed '^libxmp' 'libxmp' "$sdl2_audio_needed"
reject_needed '^libsndfile' 'libsndfile' "$sdl2_audio_needed"
printf 'PASS: SDL2 full-audio build DT_NEEDED (SDL2 + libmikmod only)\n'
sed 's/^/  /' "$sdl2_audio_needed"

sdl2_dir=$build_root/sdl2-no-audio
configure_build "$sdl2_dir" "$sdl2_config" --disable-sound
build_all "$sdl2_dir" \
  "CFLAGS=$matrix_cflags" \
  "CXXFLAGS=$matrix_cxxflags"
reject_build_define GLTRON_SDL2_AUDIO "$sdl2_dir"
require_build_define GLTRON_NO_SOUND "$sdl2_dir"
sdl2_needed=$sdl2_dir/gltron.needed
write_needed "$sdl2_dir/gltron" "$sdl2_needed"
require_needed '^libSDL2-' 'SDL2' "$sdl2_needed"
reject_needed '^libSDL-1\.2\.so' 'SDL1' "$sdl2_needed"
reject_needed '^libSDL_sound-' 'SDL_sound' "$sdl2_needed"
reject_needed '^libmikmod' 'libmikmod' "$sdl2_needed"
reject_needed '^libvorbis' 'libvorbis' "$sdl2_needed"
reject_needed '^libogg' 'libogg' "$sdl2_needed"
printf 'PASS: SDL2 no-audio full build DT_NEEDED (overridden CFLAGS/CXXFLAGS)\n'
sed 's/^/  /' "$sdl2_needed"

printf 'PASS: SDL build/link matrix\n'
