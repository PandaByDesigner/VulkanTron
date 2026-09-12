#!/bin/sh
# Build real executables, run CTest, and validate a relocatable install/package.
# GUI/play verification remains a separate check in a graphical session.
set -eu

case "${1:-SDL3}" in
  SDL1|sdl1) backends=SDL1 ;;
  SDL2|sdl2) backends=SDL2 ;;
  SDL3|sdl3) backends=SDL3 ;;
  all) backends='SDL1 SDL2 SDL3' ;;
  *) printf 'usage: %s [SDL3|SDL2|SDL1|all]\n' "$0" >&2; exit 2 ;;
esac
[ "$#" -le 1 ] || { printf 'too many arguments\n' >&2; exit 2; }
repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
test_root=$(mktemp -d "${TMPDIR:-/tmp}/gltron-cmake-smoke.XXXXXX")
jobs=${CMAKE_BUILD_PARALLEL_LEVEL:-4}

cleanup() {
  status=$?
  trap - EXIT HUP INT TERM
  if [ "$status" -ne 0 ] || [ "${GLTRON_KEEP_BUILD:-0}" = 1 ]; then
    printf 'Build evidence retained in %s\n' "$test_root"
  else
    rm -rf -- "$test_root"
  fi
  exit "$status"
}
trap cleanup EXIT
trap 'exit 129' HUP
trap 'exit 130' INT
trap 'exit 143' TERM

run_logged() {
  log=$1
  shift
  if "$@" >"$log" 2>&1; then return 0; fi
  cat "$log" >&2
  return 1
}

for backend in $backends; do
  for audio in ON OFF; do
    build_dir=$test_root/$backend-$audio
    mkdir -p "$build_dir"
    run_logged "$build_dir/configure.log" cmake -S "$repo_dir" -B "$build_dir" \
      -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr \
      -DGLTRON_SDL_BACKEND="$backend" -DGLTRON_ENABLE_AUDIO="$audio" \
      -DBUILD_TESTING=ON
    run_logged "$build_dir/build.log" cmake --build "$build_dir" --parallel "$jobs"
    run_logged "$build_dir/ctest.log" ctest --test-dir "$build_dir" --output-on-failure
    [ -x "$build_dir/bin/gltron" ]
    run_logged "$build_dir/version.log" "$build_dir/bin/gltron" --version
    run_logged "$build_dir/help.log" "$build_dir/bin/gltron" --help
    if command -v ldd >/dev/null 2>&1; then
      ldd "$build_dir/bin/gltron" >"$build_dir/linked-libraries.txt"
      if [ "$backend" != SDL1 ] && \
         rg -q 'libSDL-1\.2|libSDL_sound' "$build_dir/linked-libraries.txt"; then
        printf 'FAIL: legacy SDL audio leaked into %s\n' "$backend" >&2; exit 1
      fi
      if [ "$backend" = SDL3 ] && rg -q 'libSDL2' "$build_dir/linked-libraries.txt"; then
        printf 'FAIL: SDL2 linked into native SDL3 executable\n' >&2; exit 1
      fi
      if [ "$audio" = OFF ] && \
         rg -q 'libmikmod|libSDL_sound' "$build_dir/linked-libraries.txt"; then
        printf 'FAIL: decoder linked into no-audio executable\n' >&2; exit 1
      fi
    fi
    stage=$build_dir/stage
    run_logged "$build_dir/install.log" cmake --install "$build_dir" --prefix "$stage"
    [ -x "$stage/bin/gltron" ]
    run_logged "$build_dir/installed-version.log" "$stage/bin/gltron" --version
    for asset in scripts/main.lua scripts/config.lua art/default/artpack.lua \
                 art/default/gltron.png data/lightcycle-high.obj data/game_crash.wav \
                 music/song_revenge_of_cats.it; do
      cmp "$repo_dir/$asset" "$stage/share/gltron/$asset"
    done
    [ -s "$stage/share/gltron/COPYING" ]
    [ -s "$stage/share/gltron/build-info.txt" ]
    if command -v desktop-file-validate >/dev/null 2>&1; then
      desktop-file-validate "$stage/share/applications/gltron.desktop"
    fi
    run_logged "$build_dir/package.log" cpack --config "$build_dir/CPackConfig.cmake" -B "$build_dir/packages"
    archive=$(printf '%s\n' "$build_dir"/packages/*.tar.gz)
    [ -f "$archive" ] && [ -s "$archive.sha256" ]
    (cd "$build_dir/packages" && sha256sum --check "$(basename "$archive").sha256")
    mkdir "$build_dir/unpacked"
    tar -xzf "$archive" -C "$build_dir/unpacked"
    packaged_bin=$(printf '%s\n' "$build_dir"/unpacked/*/bin/gltron)
    [ -x "$packaged_bin" ]
    run_logged "$build_dir/packaged-version.log" "$packaged_bin" --version
    printf 'PASS: %s audio=%s executable, CTest, install, and package\n' "$backend" "$audio"
  done
done

# A source archive must contain the Lua entry points and be independently
# buildable. Merely linking --version cannot detect omitted runtime assets.
source_packages=$test_root/source-packages
run_logged "$test_root/source-package.log" cpack \
  --config "$build_dir/CPackSourceConfig.cmake" -B "$source_packages"
source_archive=$(printf '%s\n' "$source_packages"/*-source.tar.gz)
[ -f "$source_archive" ]
(cd "$source_packages" && sha256sum --check "$(basename "$source_archive").sha256")
mkdir "$test_root/source-unpacked"
tar -xzf "$source_archive" -C "$test_root/source-unpacked"
source_dir=$(printf '%s\n' "$test_root"/source-unpacked/*)
[ -f "$source_dir/CMakeLists.txt" ]
for script in "$repo_dir"/scripts/*.lua; do
  cmp "$script" "$source_dir/scripts/$(basename "$script")"
done
cmp "$repo_dir/art/default/artpack.lua" "$source_dir/art/default/artpack.lua"
run_logged "$test_root/source-configure.log" cmake -S "$source_dir" \
  -B "$test_root/source-build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DGLTRON_SDL_BACKEND="$backend" -DGLTRON_ENABLE_AUDIO=OFF -DBUILD_TESTING=ON
run_logged "$test_root/source-build.log" cmake --build "$test_root/source-build" --parallel "$jobs"
run_logged "$test_root/source-ctest.log" ctest --test-dir "$test_root/source-build" --output-on-failure
run_logged "$test_root/source-version.log" "$test_root/source-build/bin/gltron" --version
printf 'PASS: source archive Lua assets, independent %s build, and CTest\n' "$backend"
