#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
export PS3DEV="${PS3DEV:-$(cd ../build-tools/ps3dev && pwd)}"
export PSL1GHT="$PS3DEV"
export PATH="$PS3DEV/bin:$PS3DEV/ppu/bin:$PS3DEV/spu/bin:$PATH"
VERSION="$(sed -n 's/^#define ORBIT_VERSION "\(.*\)"/\1/p' include/orbit.h)"
PKG="build/Orbit-PS3-$VERSION.pkg"
make -j4 ps3 preview
python3 tools/png.py
python3 tools/contact_sheet.py
mkdir -p build/pkg/USRDIR
ppu-strip build/orbit.elf -o build/orbit-stripped.elf
sprxlinker build/orbit-stripped.elf
make_self_npdrm build/orbit-stripped.elf build/pkg/USRDIR/EBOOT.BIN UP0001-ORB000001_00-0000000000000000
sfo --title "Orbit Chat" --appid ORB000001 -f assets/sfo.xml build/pkg/PARAM.SFO
cp build/icon.png build/pkg/ICON0.PNG
cp assets/OrbitSans.ttf build/pkg/USRDIR/OrbitSans.ttf
# 1.2: interface language files (checked line by line when ORBIT starts)
mkdir -p build/pkg/USRDIR/lang
cp lang/*.lang build/pkg/USRDIR/lang/
cp README-DE.md build/pkg/USRDIR/README-DE.txt
cp THIRD-PARTY.md build/pkg/USRDIR/THIRD-PARTY.txt
cp LICENSE build/pkg/USRDIR/LICENSE.txt
# 1.1: background-notification plugin (installed from the settings, see notify_core.h)
bash ../orbit-plugin/build_notify.sh > build/notify-plugin.log
cp ../orbit-plugin/build/notify/orbit_notify.sprx build/pkg/USRDIR/orbit_notify.sprx
# 1.2: experimental game-presence plugin, a separate module (installed only on request, presence.h)
bash ../orbit-plugin/build_presence.sh > build/presence-plugin.log
cp ../orbit-plugin/build/presence/orbit_presence.sprx build/pkg/USRDIR/orbit_presence.sprx
# Optional: address of your Orbit relay ("Vermittler"), e.g. 203.0.113.5 - baked into the package
if [ -s relay.txt ]; then cp relay.txt build/pkg/USRDIR/relay.txt; fi
mkdir -p build/pkg/USRDIR/licenses
cp vendor/*LICENSE* build/pkg/USRDIR/licenses/
pkg --contentid UP0001-ORB000001_00-0000000000000000 build/pkg $PKG
pkg --list $PKG > build/package-contents.txt
package_finalize $PKG
python3 tools/verify_package.py
sha256sum $PKG
