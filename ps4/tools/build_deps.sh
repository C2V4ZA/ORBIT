#!/bin/bash
# Builds zlib, FreeType and mbedTLS as static PS4 libraries into ~/ps4dev/portlibs.
# Sources: official release tarballs (sha256 checked), mbedTLS from ../build-tools.
set -eo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
. "$HERE/oo-env.sh"
DL="$HOME/oo-dl/src"; OUT="$HOME/ps4dev/portlibs"; B="$HOME/ps4dev/build-deps"
mkdir -p "$OUT/lib" "$OUT/include" "$B"
CC=clang; AR=llvm-ar-18
CF="$OO_CFLAGS -O2 -ffunction-sections -fdata-sections"
check() { echo "$2  $1" | sha256sum -c --quiet; }
check "$DL/zlib-1.3.1.tar.gz" 9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23
check "$DL/freetype-2.13.3.tar.xz" 0550350666d427c74daeb85d5ac7bb353acba5f76956395995311a9c6f063289
# zlib
rm -rf "$B/zlib" && mkdir "$B/zlib" && tar xzf "$DL/zlib-1.3.1.tar.gz" -C "$B/zlib" --strip-components=1
( cd "$B/zlib" && for f in adler32 crc32 deflate infback inffast inflate inftrees trees zutil compress uncompr gzclose gzlib gzread gzwrite; do
    $CC $CF -DHAVE_UNISTD_H -c $f.c -o $f.o; done && $AR rcs "$OUT/lib/libz.a" *.o && cp zlib.h zconf.h "$OUT/include/" )
# FreeType (only the modules a TTF font needs)
rm -rf "$B/ft" && mkdir "$B/ft" && tar xJf "$DL/freetype-2.13.3.tar.xz" -C "$B/ft" --strip-components=1
( cd "$B/ft" && cp "$HERE/ftmodule.h" include/freetype/config/ftmodule.h && FT="-Iinclude -DFT2_BUILD_LIBRARY -DFT_CONFIG_OPTION_USE_ZLIB -DFT_CONFIG_OPTION_SYSTEM_ZLIB -I$OUT/include"
  for f in src/base/ftsystem.c src/base/ftinit.c src/base/ftdebug.c src/base/ftbase.c src/base/ftbbox.c src/base/ftglyph.c \
           src/base/ftbitmap.c src/base/ftmm.c src/truetype/truetype.c src/sfnt/sfnt.c src/smooth/smooth.c \
           src/autofit/autofit.c src/psnames/psnames.c src/gzip/ftgzip.c src/cff/cff.c src/psaux/psaux.c src/pshinter/pshinter.c; do
    $CC $CF $FT -c $f -o $(basename $f .c).o; done && $AR rcs "$OUT/lib/libfreetype.a" *.o
  rm -rf "$OUT/include/freetype2" && mkdir -p "$OUT/include/freetype2" && cp -r include/* "$OUT/include/freetype2/" )
# mbedTLS 2.28 (same version as the PS3 build)
rm -rf "$B/mbed" && cp -r "$HERE/../../../build-tools/mbedtls-2.28.10" "$B/mbed"
( cd "$B/mbed/library" && rm -f *.o && for f in *.c; do $CC $CF -DMBEDTLS_NO_PLATFORM_ENTROPY -I../include -c $f -o ${f%.c}.o; done &&
  $AR rcs "$OUT/lib/libmbedcrypto.a" $(ls *.o | grep -v "^ssl\|^x509\|^debug\|^net_sockets\|^pkcs11") &&
  $AR rcs "$OUT/lib/libmbedx509.a" x509*.o pkcs11.o && $AR rcs "$OUT/lib/libmbedtls.a" ssl*.o debug.o net_sockets.o &&
  rm -rf "$OUT/include/mbedtls" && cp -r ../include/mbedtls "$OUT/include/" )
ls -la "$OUT/lib"
