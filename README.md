# GSRecomp webOS probe

A test of whether [GSRecomp](https://github.com/Shmargus/GSRecomp) (Golden Sun
recompiled into native code) can run on an LG webOS TV (32-bit ARM, webOS 6,
Mali-G51), before deciding on a real port.

> **Private builds only.** GSRecomp translates the game's code from your own
> ROM. The IPK this probe makes contains that translated code, so it is for
> your own TV and must never be shared or listed in a repository.

## What it does

1. GSRecomp's own translator (`gba_recompile`) and player builder
   (`gsr_builder --generate-only`), built for the PC, turn the ROM into C++
   (The Broken Seal: 579 files, 538 MB, about 80 s).
2. GSRecomp's CMake project is built for the TV with clang 18 and the
   webosbrew SDK (`cmake/WebOSClangToolchain.cmake`): the engine program
   `GoldenSunRecomp` and the game library `libGoldenSunGame.so`.
3. `scripts/package.sh` adds a small launcher (`src/launcher.cpp`) and
   SDL-webOS and makes the IPK.

The launcher finds the ROM (`<app>/data/rom.gba`, a `GoldenSun` folder on a
USB drive, or a "Golden Sun" ROM in a RetroArch `roms/` folder), writes remote
bindings on the first run (OK = A, Back = B, Blue = Start, Yellow = Select,
CH+/CH- = L/R, Red = the engine's menu), raises the stack limit and starts the
engine with its cost probe on, so frame timing reaches `data/stdout.log`.

On the first start it first runs the engine's headless timing pass twice,
natively and fully interpreted (1800 frames, 30 s of game time, uncapped; the
screen stays black meanwhile) and writes the frame rates to
`data/benchmark.txt`, with the last frame of each as `bench-*.png`. Delete
`benchmark.txt` to measure again.

## Results so far (qemu-arm on the build PC, not the TV)

| Run | Frames | Time | Rate |
|---|---|---|---|
| native, headless | 1800 | 90 s | 19.9 fps |
| interpreted, headless | 1800 | 102 s | 17.6 fps |

The ARM build boots, runs and draws correctly (it reaches the naming screen).
Native and interpreted are close, so in the intro the GBA's CPU is not the
cost; drawing the picture and the engine's per-frame work are.

## Changes to GSRecomp

- `patches/0001-webos-probe.patch` (against GSRecomp `main`):
  - one `[[clang::musttail]]` in the engine names a callee whose signature
    differs from the caller's, which clang rejects (GSRecomp is only built
    with GCC 15);
  - `GSR_NO_GL_WINDOW=1` starts without an OpenGL window, to run the ARM
    build headless under qemu with SDL's dummy driver.
- `cmake/WebOSClangToolchain.cmake` hides only the C++ runtime from the
  engine's exports: the game library calls about fifty engine functions.
- `compat/GL/gl.h` and `compat/gl_desktop_stubs.c`: webOS has no desktop OpenGL; SDL's `SDL_opengl.h`
  stands in for the types. The GL-only extras (screen filters, the wider-view
  field renderer) find no desktop GL functions on the TV's OpenGL ES context
  and stay off; the game draws through SDL's renderer.

## Known limits of the probe

- **Tail calls.** GSRecomp relies on GCC 15's guaranteed tail calls; clang
  only guarantees them between identical signatures, so the build uses the
  engine's fallback (plain calls the optimiser turns into jumps at -O2) and a
  256 MB stack.
- **Code found during play** is compiled on the fly on a PC. The TV has no
  compiler, so such code runs in the engine's interpreter.
- Original 240x160 view only.

## Building

```sh
export WEBOS_SDK=<arm-webos-linux-gnueabi_sdk-buildroot> WEBOS_SDL_PREFIX=<SDL-webOS install>
# 1. translator and builder for the PC (see the session notes), then:
gsr_builder --rom <rom.gba> --data "The Broken Seal/config/usa" --engine <eng> \
    --toolchain <tc> --recompiler gba_recompile --work <work> --out <out> --generate-only --keep-work
# 2. the TV build (GSRecomp checkout with the patch applied)
cmake -S "<GSRecomp>/The Broken Seal" -B build-arm -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE=cmake/WebOSClangToolchain.cmake -DCMAKE_BUILD_TYPE=Release \
    -DGSR_BUILD_LOCAL_RUNNER=ON -DGSR_SPLIT_GAME_CODE=ON -DGSR_GAME_CODE_OPT=-O2 \
    -DGSR_GAME_CODE_ROOT=<work>/gen -DGSR_GENERATED_DIR=<work>/gen/main \
    -DGSR_OVERLAY_ROOT=<work>/gen -DGSR_STAMPS_DIR=<work>/gen/stamps \
    -DGSR_ENABLE_LTO=OFF -DGSR_DEBUG_SYMBOLS=OFF -DGBARECOMP_ROOT=<GSRecomp>/engine \
    "-DCMAKE_CXX_STANDARD_LIBRARIES=<build-arm>/gl_desktop_stubs.o -lGLESv2 -lEGL -lpthread"
# (gl_desktop_stubs.o: compat/gl_desktop_stubs.c compiled with the same clang target flags, -fPIC)
cmake --build build-arm --target GoldenSunRecomp   # about 10 min on 2 cores
# 3. the IPK
scripts/package.sh build-arm dist
```

## Licence

GSRecomp's engine is PolyForm Noncommercial 1.0.0 (`LICENSE-ENGINE.txt`);
this probe's own files are MIT.
