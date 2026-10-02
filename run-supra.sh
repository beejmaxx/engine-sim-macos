#!/bin/bash
set -euo pipefail
engine_sim_root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
engine_sim_log_dir="$engine_sim_root/build/audio-validation"
mkdir -p "$engine_sim_log_dir"
engine_sim_log="$engine_sim_log_dir/supra-playback-$(date +%Y%m%d-%H%M%S)-$$.log"
echo "Toyota Supra 2JZ straight-six: start, gentle rev at 6 seconds, then idle."
echo "Saving playback diagnostics to $engine_sim_log"
"$engine_sim_root/run-audio.sh" \
    --script "$engine_sim_root/assets/audio_supra.mr" \
    --simulation-hz 2500 --demo --demo-start-throttle 2 --demo-throttle 2 \
    --volume 50 "$@" 2>&1 | tee "$engine_sim_log"
