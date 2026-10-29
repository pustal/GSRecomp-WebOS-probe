#!/usr/bin/env bash
# package.sh <arm build dir> <out dir>
#
# Stages the probe IPK from a finished ARM build of GSRecomp's The Broken Seal
# (GoldenSunRecomp + libGoldenSunGame.so) and runs ares-package. The IPK holds
# game code translated from YOUR ROM: it is for your own TV only, never share it.
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build="${1:?usage: package.sh <arm build dir> <out dir>}"
out="${2:?usage: package.sh <arm build dir> <out dir>}"
: "${WEBOS_SDK:?Set WEBOS_SDK}"
: "${WEBOS_SDL_PREFIX:?Set WEBOS_SDL_PREFIX}"
strip="${WEBOS_SDK}/bin/arm-webos-linux-gnueabi-strip"
stage="${out}/stage"
rm -rf "${stage}"
mkdir -p "${stage}/bin"

# The launcher (C++17, the webOS toolchain's runtime linked in).
clang++ --target=arm-webos-linux-gnueabi --sysroot="${WEBOS_SDK}/arm-webos-linux-gnueabi/sysroot" \
    --gcc-toolchain="${WEBOS_SDK}" -march=armv7-a -mfloat-abi=softfp -mfpu=vfpv3 -std=c++17 -O2 \
    -Wall -Wextra -fuse-ld=lld -static-libstdc++ -static-libgcc \
    "${here}/src/launcher.cpp" -o "${stage}/bin/gsr-launcher" -lrt

cp "${build}/GoldenSunRecomp" "${build}/libGoldenSunGame.so" "${stage}/bin/"
# The engine finds libGoldenSunGame.so and SDL beside itself ($ORIGIN).
cp -L "${WEBOS_SDL_PREFIX}/lib/libSDL2-2.0.so.0" "${stage}/bin/"
"${strip}" --strip-unneeded "${stage}/bin/"*
cp "${here}/webos/appinfo.json" "${here}/webos/icon.png" "${here}/webos/largeIcon.png" "${stage}/"
mkdir -p "${stage}/licenses"
cp "${here}/LICENSE-ENGINE.txt" "${stage}/licenses/" 2>/dev/null || true
ares-package --no-minify "${stage}" -o "${out}"
ls -la "${out}"/*.ipk
