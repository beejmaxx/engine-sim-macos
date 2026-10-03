#!/usr/bin/env python3
"""Validate the GUI's audio session without computer-use automation.

Default: silent, headless audio/control tests for Supra and LS.
--native-only: hidden native input checks + Metal PNGs, requires a desktop login.
--game-only: complete circuit/impact/recovery check with hidden Metal rendering.
--arcade-only: brief reverse/drift/audio check with hidden Metal rendering.
--real-audio: use the real system output (makes sound).
Run timing checks on their own, without a concurrent build or audio player.
"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys


def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=root / "build/macos-arm64-package/engine-sim-sound.app/Contents/MacOS/engine-sim-sound")
    parser.add_argument("--output", type=Path, default=root / "build/audio-validation")
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--native-only", action="store_true")
    mode.add_argument("--game-only", action="store_true")
    mode.add_argument("--arcade-only", action="store_true")
    parser.add_argument("--real-audio", action="store_true", help="Use system audio; tests will make sound")
    parser.add_argument("--presets", nargs="+", default=["supra", "ls"], help="Engine IDs for headless sound tests")
    args = parser.parse_args()
    if args.native_only and args.real_audio:
        parser.error("The native input suite uses dummy audio; use --arcade-only or --game-only for a real-audio game test")
    args.output.mkdir(parents=True, exist_ok=True)
    environment = dict(os.environ)
    if args.real_audio:
        environment.pop("SDL_AUDIO_DRIVER", None)
    else:
        environment["SDL_AUDIO_DRIVER"] = "dummy"
    if args.native_only:
        directory = args.output / "gui-ui"
        directory.mkdir(exist_ok=True)
        commands = [("gui-ui-test", ["--ui-test", str(directory.resolve()), "--log", str(directory.resolve() / "playback.log")])]
    elif args.game_only or args.arcade_only:
        prefix = (args.output / ("arcade" if args.arcade_only else "driving-game")).resolve()
        flag = "--arcade-test" if args.arcade_only else "--game-test"
        commands = [(prefix.name + "-test", [flag, str(prefix), "--offscreen",
                     "--preset", args.presets[0], "--log", str(prefix) + "-playback.log"])]
    else:
        commands = [("gui-" + preset + "-run", ["--self-test", str((args.output / ("gui-" + preset)).resolve()),
                     "--preset", preset, "--ui-stall-ms", "1000"]) for preset in args.presets]
    for name, options in commands:
        log = args.output / (name + ".log")
        print("Running", name, "(system audio)" if args.real_audio else "(silent)", flush=True)
        with log.open("w") as output:
            result = subprocess.run([str(args.binary.resolve()), *options], cwd=args.output.resolve(),
                                    env=environment, stdout=output, stderr=subprocess.STDOUT,
                                    timeout=210 if args.native_only or args.game_only else 120)
        complete = not args.native_only or "UI_RESULT=PASS" in log.read_text()
        if args.game_only or args.arcade_only:
            report = args.output / ("arcade.json" if args.arcade_only else "driving-game.json")
            marker = "ARCADE_RESULT=PASS" if args.arcade_only else "GAME_RESULT=PASS"
            complete = marker in log.read_text() and report.exists()
            if complete:
                complete = json.loads(report.read_text())["result"] == "PASS"
        if result.returncode or not complete:
            print("FAIL:", name, "exit", result.returncode, "— see", log)
            if args.native_only or args.game_only or args.arcade_only:
                print("The native/game test must finish all checks and print its PASS result.")
            return 1
    print("PASS:", len(commands), "test runs. Evidence:", args.output)
    return 0


if __name__ == "__main__":
    sys.exit(main())
