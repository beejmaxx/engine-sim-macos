#!/bin/sh
set -eu
engine_sim_root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
engine_sim_sound="$engine_sim_root/dist/engine-sim-sound.app/Contents/MacOS/engine-sim-sound"
if [ ! -x "$engine_sim_sound" ]; then
    engine_sim_sound="$engine_sim_root/build/macos-arm64-package/engine-sim-sound.app/Contents/MacOS/engine-sim-sound"
fi
if [ ! -x "$engine_sim_sound" ]; then
    echo "Build first: cmake --build --preset macos-arm64-package --target engine-sim-sound" >&2
    exit 1
fi
engine_sim_logs="$engine_sim_root/build/audio-validation"
mkdir -p "$engine_sim_logs"
engine_sim_log="$engine_sim_logs/sound-gui-$(date +%Y%m%d-%H%M%S)-$$.log"
echo "Playback log: $engine_sim_log"
exec "$engine_sim_sound" --log "$engine_sim_log" "$@"
