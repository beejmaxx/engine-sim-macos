#!/bin/sh
set -eu
engine_sim_root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
engine_sim_log_dir="$engine_sim_root/build/audio-validation"
mkdir -p "$engine_sim_log_dir"
engine_sim_log="$engine_sim_log_dir/sound-playback-$(date +%Y%m%d-%H%M%S)-$$.log"
echo "Loading Toyota Supra 2JZ sound controls..."
echo "Playback log: $engine_sim_log"
exec "$engine_sim_root/run-audio.sh" --tui \
    --script "$engine_sim_root/assets/audio_supra.mr" --simulation-hz 2500 \
    --demo-start-throttle 2 --demo-throttle 2 --volume 50 \
    --log "$engine_sim_log" "$@"
