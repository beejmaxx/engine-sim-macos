# Engine Sim for macOS

A native Apple Silicon engine-sound playground: C++ simulation and audio,
AppKit controls, and a Metal dashboard with moving pistons, cams, gauges,
live audio plots, and a playable driving game with a chase camera.

An independent community fork of [Ange Yaghi's Engine Simulator](https://github.com/ange-yaghi/engine-sim),
built on [Carles Onielfa's Open Engine Simulator](https://github.com/carlesonielfa/open-engine-sim).
The original simulation, sounds, and visual design are their work. This fork
focuses on responsive sound and a native Mac interface.

![The Porsche 911 GT3 driving view](docs/images/driving-gt3.png)

[See the piston cutaway view](docs/images/porsche-gt3.png).

## What works

- 23 selectable engines, including Porsche flat-sixes, BMW M52B28, Supra 2JZ,
  GM LS, Ferrari V8/V12, and Lexus LFA V10.
- Hold-to-rev keyboard/mouse input, a persistent throttle slider, live exhaust
  and noise controls, mute, ignition, and a layered engine cutaway.
- Automatic Drive mode uses the real vehicle load and gearbox, handles clutch
  launch and up/downshifts, and briefly cuts throttle during a shift. Braking
  cuts the accelerator, prevents new upshifts, and downshifts for engine braking.
- Clear native Mac monospaced text, rasterized once into a Retina font atlas.
- A closed forest circuit with steering, tire grip, off-road resistance and
  barrier collisions. A three-lap time trial has ordered checkpoints, lap/best
  timers, a finish state and a minimap. Recover to the last checkpoint or start
  a new run without reloading the engine.
- A credited Porsche 911 GT3 body for the GT3 preset, reflective paint,
  moving front wheels, body lean,
  a responsive chase camera and a large MPH/RPM/gear HUD. The engine simulation drives
  actual forward travel; road and wheel motion use metres.
  The sun stays in the world, moving across the view as the car turns.
- Progressive keyboard steering with front/rear tyre forces and chassis yaw
  inertia. Quick taps make small corrections at speed; sustained presses build
  a turn, with prompt release and countersteering. Grip and resistance change
  gradually as individual tyres cross the shoulder. Corner-speed and braking
  cues help judge when to slow down.
- Audio production runs independently of the UI. The renderer consumes bounded
  snapshots; a slow or hidden window does not have to delay sound production.
- Native Metal rendering, display-synchronized by default, with optional glow
  and an uncapped mode. A local M1/60 Hz run sustained about 60 completed frames
  per second with no missing audio frames. This is a measurement, not a guarantee
  for every engine or machine.
- A separate terminal/audio-only host and programmatic audio/GUI tests.

The new Porsche presets are **approximate sound models**, not recordings or
factory-calibrated simulations. See [engines and provenance](docs/ENGINES.md).
Other engine presets currently retain the credited concept body, labelled in
the driving view; matching bodies are not bundled for the entire engine library.
This is an experimental sound simulator, not an engineering or tuning tool.

## Build on an Apple Silicon Mac

Requires macOS 14+, full Xcode with its Metal compiler, Homebrew, and CMake 3.25+.
The current native app has been tested on an M1 running macOS 26; older supported
macOS versions still need real-machine testing. Command Line Tools alone may
not provide the Metal compiler.

```sh
brew install cmake ninja bison flex
export PATH="$(brew --prefix bison)/bin:$(brew --prefix flex)/bin:$PATH"

# Check that full Xcode and the Metal compiler are selected.
xcodebuild -version
xcrun -sdk macosx metal --version

git clone --recurse-submodules https://github.com/beejmaxx/engine-sim-macos.git
cd engine-sim-macos
cmake --preset macos-arm64-package
cmake --build --preset macos-arm64-package --parallel 4
cmake --install build/macos-arm64-package --prefix "$PWD/dist"
./run-sound-gui.sh --preset porsche_911_gt3 --play --drive --road
```

If Xcode 26 reports a missing Metal Toolchain, install that component with
`xcodebuild -downloadComponent MetalToolchain`.
[Apple's component instructions](https://developer.apple.com/documentation/Xcode/downloading-and-installing-additional-xcode-components)
cover selection and installation. If submodules are missing, run
`git submodule update --init --recursive`.

The installed app is `dist/engine-sim-sound.app`. It includes assets and SDL3,
so it can be moved out of the checkout. Locally built bundles are ad-hoc signed,
not Developer ID signed or notarized. `run-sound-gui.sh` prefers the installed
app: rerun the install command after rebuilding.

The package preset builds a pinned SDL3 from source with the same macOS target
as the app, avoiding a dependency on a newer Homebrew binary's minimum OS.

## Controls

| Input | Action |
| --- | --- |
| **E**, ENGINE LIBRARY, or macOS Engines menu | Choose an engine |
| **Space** | Start/stop ignition |
| **A** or **AUTO DRIVE** | Automatic Drive / neutral |
| **Hold R**, **W**, **Up**, or the **GAS** button | Accelerate while held |
| **Hold S**, **Down**, or **BRAKE** | Apply the brakes (takes priority over throttle) |
| **Left / Right** in the game | Steer; releasing returns the wheels toward centre |
| **C** or **RECOVER** | Stop and return to the last checkpoint; adds a 3-second penalty |
| **Backspace** or **NEW RUN** | Stop and restart the three-lap time trial |
| **V** or the view button | Switch between the driving scene and engine dashboard |
| Drag the throttle track | Set a persistent throttle position |
| **I** | Return to idle |
| **B** | Short automatic rev |
| **M** | Mute/unmute |
| Drag VOL / CONV / +HF / ~LF / ~HF | Volume, exhaust convolution, high-frequency gain, noise |
| **D** | Toggle dyno |
| **[ / ]** | Change cutaway layer |
| **F / U** | Toggle glow / uncapped rendering |
| **1 / 2** | Quick-select Supra / LS |
| **Tab**, arrows, **Return** | Focus, adjust, activate controls |

Hold-to-rev releases when the window loses focus. The engine starts stopped
unless `--play` is passed. To list preset IDs:

```sh
./run-sound-gui.sh --list-engines
./run-sound-gui.sh --preset porsche_911_carrera_32 --play
./run-sound-gui.sh --preset bmw_m52b28 --play
```

To hear a run through the gears, press **Space**, **A**, then hold **R**. Drive
handles the launch clutch and shifts automatically; **S** brakes. The gear panel
shows `D1`, `D2`, etc., and highlights shifts. Press **A** again for neutral and
free revving. Manual gear/clutch changes and the dyno leave automatic mode.
This is automatic control of the existing simulated clutch/gearbox, not a
separate torque-converter model.

Press **V** to enter the game, or launch with `--road`. Drive the forest circuit,
pass all eight checkpoints in order, and complete three laps. Brake before tight
corners: grip is limited, and leaving the asphalt increases resistance. Recovery
adds a three-second penalty and returns you to the last passed checkpoint. The
GT3 preset uses the Porsche body; other presets use the concept car. Handling
is an approximate game model. Car and scenery credits are in
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

For the terminal interface without graphics, run `./run-sound.sh`. For the
line-oriented audio host, run `./run-audio.sh --help`.

## Tests and performance checks

```sh
make PLATFORM=macos-arm64 portable-test
cmake --preset macos-arm64-package -DBUILD_TESTING=ON
cmake --build --preset macos-arm64-package --parallel 4
ctest --test-dir build/macos-arm64-package --output-on-failure

# Silent tests: capture generated PCM and check controls, gaps, clipping, timing.
python3 test/sound_gui_smoke.py --presets porsche_911_gt3 porsche_911_carrera_32 bmw_m52b28

# Hidden native input + Metal render tests; requires a logged-in Mac desktop.
python3 test/sound_gui_smoke.py --native-only
```

Run audio timing tests without a concurrent build or another simulator instance.
The checks measure software output and command-to-mixer latency, not speaker
or Bluetooth latency. See [validation and architecture](docs/MACOS.md) for real
CoreAudio checks, rendering benchmarks, and current limitations.

The inherited SDL desktop, Windows/Linux presets, and web host remain in the
source. This fork's new dashboard is macOS-only. The preserved
[upstream README](docs/UPSTREAM.md) describes the older hosts and their different
controls; it is historical documentation, not a promise of release support here.

## Contributing and license

[Contributions](CONTRIBUTING.md) are welcome. Keep the simulation independent
of platform devices, and keep blocking work out of real-time audio code.

The upstream [MIT license](LICENSE) and copyright notice are preserved.
See [third-party notices](THIRD_PARTY_NOTICES.md) for dependency and asset
licenses, and credits to Ange Yaghi, Carles Onielfa, and their contributors.
