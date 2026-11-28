#!/bin/bash
# Builds the ORBIT PS4 fPKG from build/eboot.bin (called by `make pkg`).
# Contents: eboot.bin, sce_sys/param.sfo, sce_sys/icon0.png, assets/ (font, languages, licenses).
# sce_module/libc.prx, sce_module/libSceFios2.prx and sce_sys/about/right.sprx are the
# OpenOrbis toolchain's own placeholder modules (built from src/modules in the toolchain, GPL-3.0;
# each is a one-function dummy). The PS4 loads these two module names from every app package,
# so without them the system reports the application as corrupted. No Sony files are bundled.
set -eo pipefail
cd "$(dirname "$0")/.."
. tools/oo-env.sh
T="$OO_PS4_TOOLCHAIN/bin/linux"
P=build/pkg
rm -rf "$P" && mkdir -p "$P/sce_sys/about" "$P/sce_module" "$P/assets/lang" "$P/assets/docs"
M="$OO_PS4_TOOLCHAIN/src/modules"
cp "$M/libc/libc.prx" "$P/sce_module/libc.prx"
cp "$M/libSceFios2/libSceFios2.prx" "$P/sce_module/libSceFios2.prx"
cp "$M/right/right.sprx" "$P/sce_sys/about/right.sprx"
cp build/eboot.bin "$P/eboot.bin"
python3 tools/make_icon.py "$P/sce_sys/icon0.png"
cp ../assets/OrbitSans.ttf "$P/assets/OrbitSans.ttf"
# 1.3: GoldHEN plugin for notifications during games; the app copies it to /data/GoldHEN/plugins
# only when the user installs it in the settings
cp build/orbit_notify.prx "$P/assets/orbit_notify.prx"
cp ../lang/*.lang "$P/assets/lang/"
cp ../LICENSE "$P/assets/docs/LICENSE.txt"
cp ../THIRD-PARTY.md "$P/assets/docs/THIRD-PARTY.txt"
cp THIRD-PARTY-PS4.md "$P/assets/docs/THIRD-PARTY-PS4.txt"
SFO="$P/sce_sys/param.sfo"
"$T/PkgTool.Core" sfo_new "$SFO" > /dev/null
set_sfo() { "$T/PkgTool.Core" sfo_setentry "$SFO" "$1" --type "$2" --maxsize "$3" --value "$4" > /dev/null; }
set_sfo APP_TYPE Integer 4 1
set_sfo APP_VER Utf8 8 "$(printf '%05.2f' "$(echo "$VERSION" | awk -F. '{print $1"."$2$3}')")"
set_sfo ATTRIBUTE Integer 4 0
set_sfo CATEGORY Utf8 4 gd
set_sfo CONTENT_ID Utf8 48 "$CONTENT_ID"
set_sfo DOWNLOAD_DATA_SIZE Integer 4 0
set_sfo SYSTEM_VER Integer 4 0
set_sfo TITLE Utf8 128 "$TITLE"
set_sfo TITLE_ID Utf8 12 "$TITLE_ID"
set_sfo VERSION Utf8 8 "$(printf '%05.2f' "$(echo "$VERSION" | awk -F. '{print $1"."$2$3}')")"
( cd "$P" && FILES="eboot.bin sce_sys/param.sfo sce_sys/icon0.png sce_sys/about/right.sprx sce_module/libc.prx sce_module/libSceFios2.prx $(find assets -type f | sort | tr '\n' ' ')" &&
  "$T/create-gp4" -out orbit.gp4 --content-id="$CONTENT_ID" --files "$FILES" > /dev/null && python3 ../../tools/fix_gp4.py orbit.gp4 )
( cd "$P" && "$T/PkgTool.Core" pkg_build orbit.gp4 .. > ../pkg.log )
OUT="build/ORBIT-PS4-$VERSION-TEST.pkg"
mv "build/$CONTENT_ID.pkg" "$OUT"
"$T/PkgTool.Core" pkg_listentries "$OUT" > build/pkg-entries.txt
"$T/PkgTool.Core" sfo_listentries "$SFO" > build/pkg-sfo.txt
sha256sum "$OUT"
