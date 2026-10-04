#!/bin/sh
set -eu
engine_sim_root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
exec "$engine_sim_root/run-sound-gui.sh" --preset porsche_911_gt3_sprint --play --drive --city "$@"
