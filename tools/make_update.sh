#!/bin/sh
# Prepare a signed update manifest for a built PKG. Runs locally only and uploads nothing:
# publishing the PKG and orbit-update.json (e.g. as GitHub release assets) is a separate,
# manual step that needs the maintainer's decision.
#
#   tools/make_update.sh <pkg> <version> <download-url> <notes-de> <notes-en> [keydir]
#
# The secret key stays in keydir (default: ~/.orbit-release-key, outside the repository).
set -e
[ $# -ge 5 ] || { echo "usage: $0 <pkg> <version> <url> <notes-de> <notes-en> [keydir]"; exit 2; }
PKG=$1 VERSION=$2 URL=$3 DE=$4 EN=$5
KEYDIR=${6:-${ORBIT_KEYDIR:-/mnt/c/Users/$USER/.orbit-release-key}}
cd "$(dirname "$0")/.."
make -s build/orbit-sign
set -- $(./build/orbit-sign hash "$PKG")
SIZE=$1 HASH=$2
OUT=$(dirname "$PKG")
python3 - "$VERSION" "$URL" "$SIZE" "$HASH" "$DE" "$EN" > "$OUT/orbit-update.payload.json" <<'PY'
import json, sys, datetime
v, url, size, h, de, en = sys.argv[1:]
print(json.dumps({"app": "orbit-ps3", "version": v, "url": url, "size": int(size), "blake2b": h,
                  "date": datetime.date.today().isoformat(), "notes": {"de": de, "en": en}},
                 ensure_ascii=False, separators=(",", ":")))
PY
./build/orbit-sign sign "$KEYDIR" "$OUT/orbit-update.payload.json" "$OUT/orbit-update.json"
rm -f "$OUT/orbit-update.payload.json"
echo "manifest: $OUT/orbit-update.json (not published)"
