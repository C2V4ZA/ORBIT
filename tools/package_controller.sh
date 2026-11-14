#!/usr/bin/env bash
# Builds the separate "Orbit Controller-Test" package (title ORB000003).
set -euo pipefail
cd "$(dirname "$0")/.."
export PS3DEV="${PS3DEV:-$(cd ../build-tools/ps3dev && pwd)}"
export PSL1GHT="$PS3DEV"
export PATH="$PS3DEV/bin:$PS3DEV/ppu/bin:$PS3DEV/spu/bin:$PATH"
make -j4 controller
rm -rf build/controller-pkg
mkdir -p build/controller-pkg/USRDIR/licenses
ppu-strip build/ds-probe.elf -o build/ds-probe-stripped.elf
sprxlinker build/ds-probe-stripped.elf
make_self_npdrm build/ds-probe-stripped.elf build/controller-pkg/USRDIR/EBOOT.BIN UP0001-ORB000003_00-0000000000000000
sfo --title "Orbit Controller-Test" --appid ORB000003 -f assets/controller-sfo.xml build/controller-pkg/PARAM.SFO
cp build/icon.png build/controller-pkg/ICON0.PNG
cp assets/OrbitSans.ttf build/controller-pkg/USRDIR/OrbitSans.ttf
cp LICENSE build/controller-pkg/USRDIR/LICENSE.txt
cp THIRD-PARTY.md build/controller-pkg/USRDIR/THIRD-PARTY.txt
cp vendor/*LICENSE* build/controller-pkg/USRDIR/licenses/
pkg --contentid UP0001-ORB000003_00-0000000000000000 build/controller-pkg build/Orbit-Controller-Test-0.13.pkg
package_finalize build/Orbit-Controller-Test-0.13.pkg
sha256sum build/Orbit-Controller-Test-0.13.pkg
