#!/usr/bin/env bash
#
# Build LibSensel as a native macOS dylib, including Apple Silicon (arm64).
#
# The core Sensel library is MIT-licensed portable C that talks to the Morph
# over a USB CDC serial port (POSIX termios). It has no architecture-specific
# code, so it compiles natively for arm64 even though Sensel never shipped a
# prebuilt Apple Silicon binary.
#
# Produces a UNIVERSAL dylib (arm64 + x86_64) at build/mac/libSensel.dylib.
#
# Notes:
#   * This builds the "no pressure" (standalone) variant -- full multitouch
#     contact data: position, total force (grams), area, ellipse, peak, etc.
#     This is everything most Morph apps need and has NO closed-source deps.
#   * The full raw force *image* (105x185 pressure map) needs Sensel's
#     closed-source libSenselDecompress, which ships x86_64-only. To use it,
#     run your app under Rosetta against an x86_64 build. The Python/C# Mac
#     wrappers do not load the decompress lib, so contacts work natively.
#
set -euo pipefail
cd "$(dirname "$0")"

ARCHS=(arm64 x86_64)
CFLAGS=(-c -std=c99 -Wall -fPIC -fvisibility=hidden -Isrc/ -DSENSEL_EXPORTS -O2)
SRCS=(src/sensel.c src/sensel_register.c src/sensel_serial_linux.c)  # _linux.c is POSIX; works on macOS

# Add 'pressure' as the first argument to link against libSenselDecompress (x86_64 only).
if [[ "${1:-}" == "pressure" ]]; then
  CFLAGS+=(-DSENSEL_PRESSURE)
  LDFLAGS=(-lsenseldecompress -L/usr/local/lib)
  ARCHS=(x86_64)   # decompress lib is x86_64 only
  echo "Building WITH pressure (x86_64 only, needs libSenselDecompress)."
else
  LDFLAGS=()
fi

rm -rf build/mac
for ARCH in "${ARCHS[@]}"; do
  OUT="build/mac/$ARCH"
  mkdir -p "$OUT"
  for src in "${SRCS[@]}"; do
    clang "${CFLAGS[@]}" -arch "$ARCH" -o "$OUT/$(basename "${src%.c}").o" "$src"
  done
  clang -dynamiclib -arch "$ARCH" \
    -install_name /usr/local/lib/libSensel.dylib \
    -current_version 0.8.2 -compatibility_version 0.8.0 \
    -o "$OUT/libSensel.dylib" "$OUT"/*.o ${LDFLAGS[@]+"${LDFLAGS[@]}"}
done

if [[ ${#ARCHS[@]} -gt 1 ]]; then
  lipo -create "${ARCHS[@]/#/build/mac/}" --output build/mac/libSensel.dylib 2>/dev/null || \
  lipo -create $(printf 'build/mac/%s/libSensel.dylib ' "${ARCHS[@]}") -output build/mac/libSensel.dylib
else
  cp "build/mac/${ARCHS[0]}/libSensel.dylib" build/mac/libSensel.dylib
fi

echo "Built: build/mac/libSensel.dylib"
file build/mac/libSensel.dylib
echo
echo "Install with:  sudo cp build/mac/libSensel.dylib /usr/local/lib/libSensel.dylib"
echo "(That is the path the Python and C# Mac wrappers load.)"
