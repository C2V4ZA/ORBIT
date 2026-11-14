#!/usr/bin/env bash
# Shows the 0.9.2 relay failing the tunnel load test and the fixed relay passing it.
cd "$(dirname "$0")/.."
for v in alt neu; do
    f=relay/orbit_relay.py
    [ "$v" = alt ] && f=../../outputs/Orbit-PS3-0.9.2/PC-Internet/orbit_relay.py
    python3 "$f" --port 31495 --state "/tmp/o-$v.json" --quiet > "/tmp/r-$v.log" 2>&1 &
    p=$!
    sleep 1
    echo "== $v:"
    python3 tests/relay_load.py 31495 2>&1 | tail -1
    kill "$p"
    sleep 1
done
