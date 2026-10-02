#!/bin/sh
set -eu
engine_sim_root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
engine_sim_audio="$engine_sim_root/dist/engine-sim.app/Contents/MacOS/engine-sim-audio"
if [ ! -x "$engine_sim_audio" ]; then
    engine_sim_audio="$engine_sim_root/build/macos-arm64-package/engine-sim-audio"
fi
if [ ! -x "$engine_sim_audio" ]; then
    echo "Build first: cmake --build --preset macos-arm64-package --target engine-sim-audio" >&2
    exit 1
fi
exec "$engine_sim_audio" "$@"
