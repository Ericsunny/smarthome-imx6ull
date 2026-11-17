#!/bin/sh
# Run a prebuilt binary. No module loading, network changes or factory UI kills.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BIN=${MONITOR_BIN:-"$ROOT/build-linux/smarthome"}
if [ ! -x "$BIN" ]; then
    echo "Build app/smarthome.pro or CMake first; set MONITOR_BIN to the binary." >&2
    exit 1
fi
exec "$BIN" "$@"
