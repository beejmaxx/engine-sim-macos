#!/usr/bin/env python3
"""Validate the GUI's audio session without computer-use automation.

Default: silent, headless audio/control tests for Supra and LS.
--native-only: hidden native input checks + Metal PNGs, requires a desktop login.
Run timing checks on their own, without a concurrent build or audio player.
"""
import argparse
import os
from pathlib import Path
import subprocess
import sys


def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=root / "build/macos-arm64-package/engine-sim-sound.app/Contents/MacOS/engine-sim-sound")
    parser.add_argument("--output", type=Path, default=root / "build/audio-validation")
    parser.add_argument("--native-only", action="store_true")
    parser.add_argument("--presets", nargs="+", default=["supra", "ls"], help="Engine IDs for headless sound tests")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    environment = dict(os.environ, SDL_AUDIO_DRIVER="dummy")
    if args.native_only:
        directory = args.output / "gui-ui"
        directory.mkdir(exist_ok=True)
        commands = [("gui-ui-test", ["--ui-test", str(directory.resolve()), "--log", str(directory.resolve() / "playback.log")])]
    else:
        commands = [("gui-" + preset + "-run", ["--self-test", str((args.output / ("gui-" + preset)).resolve()),
                     "--preset", preset, "--ui-stall-ms", "1000"]) for preset in args.presets]
    for name, options in commands:
        log = args.output / (name + ".log")
        print("Running", name, "(silent)", flush=True)
        with log.open("w") as output:
            result = subprocess.run([str(args.binary.resolve()), *options], cwd=args.output.resolve(),
                                    env=environment, stdout=output, stderr=subprocess.STDOUT,
                                    timeout=210 if args.native_only else 120)
        complete = not args.native_only or "UI_RESULT=PASS" in log.read_text()
        if result.returncode or not complete:
            print("FAIL:", name, "exit", result.returncode, "— see", log)
            if args.native_only:
                print("The hidden native test must finish all checks and print UI_RESULT=PASS.")
            return 1
    print("PASS:", len(commands), "test runs. Evidence:", args.output)
    return 0


if __name__ == "__main__":
    sys.exit(main())
