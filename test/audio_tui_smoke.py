#!/usr/bin/env python3
"""Exercise the actual TUI and audio host in a PTY, with silent SDL output.

Run explicitly; the timing cases must not compete with parallel builds/tests.
Only Python's standard library is needed. Evidence stays in the build tree.
"""
import argparse
import fcntl
import json
import os
from pathlib import Path
import pty
import re
import select
import signal
import struct
import termios
import time


def fields(line):
    return dict(re.findall(r"(\w+)=([^\s]+)", line))


def run_case(binary, assets, out, case):
    prefix = out / ("tui-" + case)
    log = prefix.with_suffix(".log")
    args = [str(binary), "--tui", "--script", str(assets / "audio_supra.mr"),
            "--simulation-hz", "2500", "--demo-start-throttle", "2",
            "--demo-throttle", "2", "--volume", "50", "--log", str(log)]
    if case in ("stall", "backpressure"):
        args += ["--demo", "--seconds", "30", "--verify", str(prefix)]
    if case == "stall":
        args += ["--ui-stall-ms", "1000"]

    master, slave = pty.openpty()
    terminal_state_path = prefix.with_suffix(".terminal-state.json")
    terminal_state_path.unlink(missing_ok=True)
    fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", 26, 88, 0, 0))
    pid = os.fork()
    if pid == 0:
        os.close(master)
        os.setsid()
        fcntl.ioctl(slave, termios.TIOCSCTTY, 0)
        for descriptor in (0, 1, 2):
            os.dup2(slave, descriptor)
        if slave > 2:
            os.close(slave)
        expected_terminal = termios.tcgetattr(0)
        expected_flags = fcntl.fcntl(0, fcntl.F_GETFL)
        # Retain the controlling terminal until restoration has been checked.
        # macOS revokes the slave when its session leader exits.
        application = os.fork()
        if application == 0:
            os.execvpe(args[0], args, dict(os.environ, SDL_AUDIO_DRIVER="dummy", TERM="xterm-256color"))
        signal.signal(signal.SIGINT, signal.SIG_IGN)
        _, application_status = os.waitpid(application, 0)
        after_terminal = termios.tcgetattr(0)
        expected_modes = expected_terminal.copy()
        after_modes = after_terminal.copy()
        # PENDIN is kernel pending-input state, not a terminal mode; macOS
        # sets it when canonical input is restored. All other fields must match.
        expected_modes[3] &= ~getattr(termios, "PENDIN", 0)
        after_modes[3] &= ~getattr(termios, "PENDIN", 0)
        restored = {
            "termios": after_modes == expected_modes,
            # O_NONBLOCK is the file-status flag the TUI changes. Do not
            # compare undocumented kernel-maintained bits returned by F_GETFL.
            "flags": (fcntl.fcntl(0, fcntl.F_GETFL) & os.O_NONBLOCK) == (expected_flags & os.O_NONBLOCK),
            "before_termios": repr(expected_terminal), "after_termios": repr(after_terminal),
            "before_flags": expected_flags, "after_flags": fcntl.fcntl(0, fcntl.F_GETFL),
        }
        terminal_state_path.write_text(json.dumps(restored) + "\n")
        exit_code = os.waitstatus_to_exitcode(application_status)
        os._exit(exit_code if exit_code >= 0 else 128 - exit_code)

    os.set_blocking(master, False)
    terminal = bytearray()
    started = None
    deadline = time.monotonic() + 90
    actions = []
    if case == "controls":
        # A fragmented cursor-key sequence must still be recognized.
        actions = [(3.3, b"\x1b"), (3.4, b"["), (3.5, b"C"), (3.7, b"wwwwwww"),
                   (5.5, b"0"), (6.2, b"b"), (7.5, b"-"), (8.2, b"m"),
                   (9.4, b"m"), (10.5, b" "), (12.0, b" "), (16.0, b"q")]
    elif case == "sigint":
        actions = [(4.0, b"\x03")]
    resized = restored_size = False
    live_during_backpressure = False
    status = None
    try:
        while time.monotonic() < deadline:
            elapsed = time.monotonic() - started if started else 0
            while actions and started and elapsed >= actions[0][0]:
                os.write(master, actions.pop(0)[1])
            if case == "controls" and started:
                if elapsed >= 4.2 and not resized:
                    fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", 8, 42, 0, 0))
                    resized = True
                if elapsed >= 5.2 and not restored_size:
                    fcntl.ioctl(slave, termios.TIOCSWINSZ, struct.pack("HHHH", 26, 88, 0, 0))
                    restored_size = True

            paused_reader = case == "backpressure" and started and 3 <= elapsed < 15
            if paused_reader:
                if elapsed > 13 and log.exists():
                    ticks = [fields(line) for line in log.read_text().splitlines() if line.startswith("t=")]
                    live_during_backpressure = any(float(tick["t"]) > 12 for tick in ticks)
                time.sleep(0.02)
            elif select.select([master], [], [], 0.02)[0]:
                try:
                    chunk = os.read(master, 65536)
                    terminal.extend(chunk)
                    if started is None and b"\x1b[?1049h" in terminal:
                        started = time.monotonic()
                except BlockingIOError:
                    pass

            finished, status = os.waitpid(pid, os.WNOHANG)
            if finished:
                pid = None
                while select.select([master], [], [], 0)[0]:
                    try:
                        chunk = os.read(master, 65536)
                        if not chunk:
                            break
                        terminal.extend(chunk)
                    except BlockingIOError:
                        break
                break
        else:
            raise AssertionError("TUI did not exit within the deadline")

        prefix.with_suffix(".terminal.log").write_bytes(terminal)
        assert os.waitstatus_to_exitcode(status) == 0, terminal[-4000:].decode(errors="replace")
        restored = json.loads(terminal_state_path.read_text())
        assert restored["termios"], "Terminal mode was not restored"
        assert restored["flags"], "Nonblocking flag was not restored"
        assert b"\x1b[?1049l" in terminal and b"\x1b[?25h" in terminal, "Screen/cursor was not restored"
        assert b"ENGINE SOUND" in terminal and b"2JZ [I6]" in terminal
        lines = log.read_text().splitlines()
        summary = fields(next(line for line in lines if line.startswith("SUMMARY")))
        ticks = [fields(line) for line in lines if line.startswith("t=")]
        assert summary["result"] == "PASS", summary
        assert int(summary["total_missing_frames"]) == 0, summary
        assert int(summary["write_errors"]) == 0, summary
        assert int(summary["ui_frames"]) > 0, summary
        assert any(float(tick["rpm"]) > 500 for tick in ticks), "Engine never started"
        if case == "controls":
            assert any(float(tick["throttle_pct"]) == 2 for tick in ticks), "Arrow/W input did not reach producer"
            assert any(float(tick["volume_pct"]) == 45 for tick in ticks), "Volume did not reach producer"
            assert any(float(tick["volume_pct"]) == 0 for tick in ticks), "Mute did not reach producer"
            assert any(tick["ignition"] == "0" for tick in ticks), "Stop did not reach producer"
            assert float(ticks[-1]["rpm"]) > 500 and ticks[-1]["ignition"] == "1", "Restart failed"
            assert b"Gentle rev in progress" in terminal, "Timed rev was not acknowledged"
            assert b"Enlarge terminal" in terminal, "Resize did not produce compact layout"
        if case in ("stall", "backpressure"):
            assert int(summary["near_full_scale_samples"]) == 0, summary
            assert float(summary["silence_max_ms"]) == 0, summary
        if case == "backpressure":
            assert int(summary["ui_skipped_frames"]) > 0, "Test did not fill the terminal buffer"
            assert live_during_backpressure, "Main thread stopped while terminal was full"
        result = dict(case=case, terminal_restored=True, summary=summary)
        prefix.with_suffix(".json").write_text(json.dumps(result, indent=2) + "\n")
        print(json.dumps(result), flush=True)
    finally:
        if pid is not None:
            os.killpg(pid, signal.SIGKILL)
            os.waitpid(pid, 0)
        os.close(master)
        os.close(slave)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=Path("build/macos-arm64-package/engine-sim-audio"))
    parser.add_argument("--assets", type=Path, default=Path("assets"))
    parser.add_argument("--out-dir", type=Path, default=Path("build/audio-validation"))
    parser.add_argument("--case", choices=["all", "controls", "stall", "backpressure", "sigint"], default="all")
    options = parser.parse_args()
    options.out_dir.mkdir(parents=True, exist_ok=True)
    for name in (["controls", "stall", "backpressure", "sigint"] if options.case == "all" else [options.case]):
        run_case(options.binary.resolve(), options.assets.resolve(), options.out_dir.resolve(), name)
