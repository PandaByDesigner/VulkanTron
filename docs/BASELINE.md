# GLTron stabilization baseline

This repository began with the complete upstream GLTron 0.70 source release.
The pristine import is preserved by the `upstream-0.70` tag.

## Provenance

- Archive: `gltron-0.70-source.tar.gz`
- Upstream release: GLTron 0.70
- SHA-256: `e0c8ebb41a18a1f8d7302a9c2cb466f5b1dd63e9a9966c769075e6b6bdad8bb0`
- Imported files: 341 regular files

The imported tree was compared byte-for-byte with a fresh extraction of the
archive before the baseline commit was created.

## Classic compatibility changes

The first maintained layer carries the compatibility fixes used by the Arch
package that successfully built and ran on the target machine:

- Preserve distribution-provided compiler and optimization flags.
- Match scripting function definitions to their const-correct declarations.
- Correct modern integer and OpenGL texture identifier types.
- Correct the zero-vector comparison in trail distance calculation.
- Make the program entry point compatible with the subsystem interface.
- Include `<stdint.h>` for modern C library integer types in bundled Lua 4.

The compatibility layer deliberately retains upstream's generated Autotools
files. Do not run `autoreconf` yet: the 2003 inputs require a separate build
system modernization, and regeneration would discard the `configure` flag fix.

## Reproduce the classic build on Arch Linux

Install the build dependencies:

```sh
sudo pacman -S --needed base-devel glu libglvnd libpng sdl12-compat sdl_sound
```

Build outside the source tree:

```sh
mkdir -p _build/classic
cd _build/classic
CFLAGS='-O2 -g' CXXFLAGS='-O2 -g' ../../configure --enable-warn=off --enable-localdata
make
```

`--enable-localdata` makes asset paths relative to the executable's directory.
GLTron also changes into that directory during startup, so copy the ignored
build artifact to the source root before launching it:

```sh
cd ../..
cp _build/classic/gltron ./gltron
./gltron -i
```

Running the out-of-tree binary in place will not find `scripts/`, `data/`, or
the other local asset directories even if the shell started in the source root.
