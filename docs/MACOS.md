# Native Mac host: architecture and validation

## Thread ownership

`src/sound_mac.mm` owns AppKit controls and engine selection. A background loader
compiles each `.mr` entrypoint and creates a `SoundSession`. Switching engines
stops the old output before it is destroyed.

`AudioEngineRunner` owns the live simulation and synthesizer. It produces PCM
in 5 ms blocks, with a roughly 30 ms reserve. Controls travel through a bounded
command queue. Timed starter and rev actions end on the worker, even if AppKit
stalls. `SdlAudioOutput` delivers PCM to SDL3/CoreAudio. The device callback does
no allocation, disk I/O, logging, or blocking synchronization.

The Metal renderer runs on its own display-link-driven thread. It uses immutable
engine geometry and bounded queues of physics/audio snapshots. If a visual
queue fills, the producer drops visual snapshots; it never waits for rendering.
The renderer never dereferences the live engine's physics objects. Gauges and
pistons continue updating during an intentionally blocked AppKit event loop.

The rendering path uses a font atlas, reusable vertex buffers, and one draw call
per frame. The display clock normally limits it to the connected display's
refresh rate. `U` removes that limit at the cost of more CPU/GPU work. Rendering
pauses when hidden or minimized; sound continues.

Text uses the native Mac monospaced font at 11.5, 13, 20, and 30 points, with
Retina glyphs cached at initialization. It does not invoke AppKit text layout
or rasterize fonts during rendering. The Metal shader's deployment target
matches the app and bundled SDL's macOS 14 target.

## Automatic driving

`AutomaticTransmission` is a platform-independent controller advanced on
simulation time by the audio worker. It regulates clutch slip at launch,
selects shift points from throttle and each engine's redline/ignition limiter,
and uses a short torque cut plus clutch re-engagement during shifts. It reads
the script's actual gear ratios and vehicle parameters; RPM and speed come from
the physics simulation. Hysteresis prevents rapid gear hunting. Braking adds
force to the vehicle's existing drag constraint, and the clutch opens near a
stop to avoid stalling. The GUI only sends pedal/mode commands.

`A` toggles automatic Drive; hold `R` for acceleration and `S` for brakes.
Keyboard/mouse pedal holds are released on focus loss. The throttle slider
remains a persistent pedal setting. Manual clutch/gear control and the dyno
exit automatic mode. Drive works with the scripted forward gears; there is no
reverse gear or separately simulated torque converter.

## Driving view

`V` switches the center panel between the original cutaway and a chase camera.
`--road` selects the driving view on launch. The procedural coupe and scenery
live entirely on the render thread, in `src/sound_road_scene.h`. Vehicle
distance is copied into the existing bounded physics snapshot; the wheel angle
uses that distance and the script's tire radius. The road stops when the vehicle
does, even while the engine idles. Brake lamps use the worker's actual brake state.

The scene clips projected triangles to its panel and shares the dashboard's
single Metal batch. A depth attachment resolves car surfaces and fog softens
the distance. It adds no model files, physics thread, or audio synchronization.
The same generic coupe is used for all presets; this is a straight-road sound
visualization without steering or manufacturer-specific bodywork.

## Programmatic checks

The Python smoke harness uses SDL's dummy audio device by default. It is silent.
It saves PCM/WAV, callback timing, control assertions, and summary logs under
`build/audio-validation/`. A passing silent run does not prove that a physical
speaker is connected or audible.

To test the real CoreAudio output without creating a GUI window, first build
the app, close other simulator instances, and run:

```sh
mkdir -p build/audio-validation
build/macos-arm64-package/engine-sim-sound.app/Contents/MacOS/engine-sim-sound \
  --preset porsche_911_gt3 --self-test "$PWD/build/audio-validation/gt3-coreaudio"
```

This makes sound, exercises start/rev/stop/restart and engine switching, and
captures the software mixer output. The test intentionally mutes briefly twice.
It checks missing frames, clipping, unexpected silent blocks, and command-to-mixer
response. It cannot measure downstream hardware, wireless, or acoustic latency.

To measure rendering while audio plays:

```sh
./run-sound-gui.sh --preset porsche_911_gt3 \
  --benchmark "$PWD/build/audio-validation/gt3-metal" --seconds 15
```

The benchmark saves JSON, a PNG read back from the GPU, and timing evidence.
It counts GPU-completed frames, not independently measured display scanouts.
`--offscreen` selects hidden rendering; `--silent` selects dummy audio explicitly.
The native input suite uses synthetic AppKit events and Metal readback, not
computer-use automation. It covers seven engines, including both Porsches.

To check the car, actual braking, and sound together:

```sh
./run-sound-gui.sh --preset porsche_911_gt3 --drive --road \
  --benchmark "$PWD/build/audio-validation/gt3-road" --seconds 20
```

This accelerates, blocks AppKit while the car continues moving, then applies
the brakes and verifies that road travel stops. It captures idle, acceleration,
braking, and stopped PNGs directly from Metal, plus frame/audio counters in JSON.

For an audio-only acceleration/shift/braking test, including PCM and driving
telemetry capture:

```sh
build/macos-arm64-package/engine-sim-sound.app/Contents/MacOS/engine-sim-sound \
  --preset supra --drive-test "$PWD/build/audio-validation/supra-drive"
```

Add `--silent` for SDL's dummy device. The 41-second test accelerates through
multiple gears while checking actual RPM drops and throttle cuts, then brakes
to a stop without stalling and returns to neutral. It also deliberately blocks
the control thread, and checks for missing PCM, clipping, and unexpected silence.
The offline core integration test exercises a complete drive/stop cycle for
the Supra and GT3 without a real-time audio device.

Run performance checks on their own. Debug builds, concurrent compilation,
thermal throttling, and other active simulators can invalidate timing results.

## Observed M1 baseline

A 15-second, 2560 x 1600 V8 dashboard run with glow enabled averaged **59.87
completed FPS** on a 60 Hz display: **1.83 ms CPU / 2.74 ms GPU** per frame,
zero missing audio frames, and zero Metal errors. During a 1.2-second AppKit
stall, the render thread completed 72 frames while sound and physics continued.

A separate Ferrari F136 CoreAudio check observed **26–43 ms command-to-mixer
response**, zero missing frames, zero clipped samples, and no unexpected silent
blocks. These are local observations, not worst-case guarantees. The output
sample reserve is an intentional latency/reliability tradeoff.

After adding the Porsche/BMW presets and rebuilding the bundled SDL3, the GT3
passed the real CoreAudio start/rev/restart/switch test with **zero missing
frames, zero clipped samples**, and a maximum **40.44 ms command-to-mixer
response**. Its producer used about 30% of one CPU core; its slowest observed
5 ms work block took 2.03 ms. The hidden native input/render suite passed across
all seven tested engines, including full-throttle holds and focus loss.

With the chase camera and automatic gearbox, a 20-second GT3/CoreAudio run at
2560 x 1600 averaged **59.60 completed FPS**, **0.45 ms CPU / 3.46 ms GPU** per
frame, with **zero missing audio frames and zero Metal errors**. It rendered
72 frames through a 1.2-second AppKit stall. The vehicle travelled 294.99 m
before braking to a stop; subsequent frames kept that distance fixed.

Separate 41-second automatic-driving audio checks passed for the Supra on the
dummy device and GT3 on CoreAudio: five upshifts each, actual RPM drops and
throttle cuts, no missing/clipped/unexpected silent PCM, and no stall at rest.

See [ENGINES.md](ENGINES.md) for model limitations, including the radial-9 startup
issue at the host's default simulation frequency. Hosted CI validates builds,
core behavior, scripts, and dummy-device audio adapters; real-device timing and
native render tests are local checks.
