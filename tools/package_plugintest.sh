#!/usr/bin/env bash
# Builds the separate "Orbit Plugin-Test" package (title ORB000004) with the plugin test modules inside.
set -euo pipefail
cd "$(dirname "$0")/.."
export PS3DEV="${PS3DEV:-$(cd ../build-tools/ps3dev && pwd)}"
export PSL1GHT="$PS3DEV"
export PATH="$PS3DEV/bin:$PS3DEV/ppu/bin:$PS3DEV/spu/bin:$PATH"
PL=../orbit-plugin
make -j4 plugintest
rm -rf build/plugintest-pkg
mkdir -p build/plugintest-pkg/USRDIR/licenses
ppu-strip build/prx-tester.elf -o build/prx-tester-stripped.elf
sprxlinker build/prx-tester-stripped.elf
make_self_npdrm build/prx-tester-stripped.elf build/plugintest-pkg/USRDIR/EBOOT.BIN UP0001-ORB000004_00-0000000000000000
sfo --title "Orbit Plugin-Test" --appid ORB000004 -f assets/plugintest-sfo.xml build/plugintest-pkg/PARAM.SFO
U=build/plugintest-pkg/USRDIR
cp $PL/build/orbit_i.sprx $U/orbit_i.sprx
cp $PL/build/orbit_k.sprx $PL/build/orbit_j.sprx $U/
cp build/icon.png build/plugintest-pkg/ICON0.PNG
cp assets/OrbitSans.ttf $U/OrbitSans.ttf
cp LICENSE $U/LICENSE.txt
cp THIRD-PARTY.md $U/THIRD-PARTY.txt
cp vendor/*LICENSE* $U/licenses/
pkg --contentid UP0001-ORB000004_00-0000000000000000 build/plugintest-pkg build/Orbit-Plugin-Test-4.1.pkg
package_finalize build/Orbit-Plugin-Test-4.1.pkg
ls -la $U
sha256sum build/Orbit-Plugin-Test-4.1.pkg
