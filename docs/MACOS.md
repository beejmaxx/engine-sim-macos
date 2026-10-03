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

The rendering path uses a font atlas and reusable vertex buffers. The dashboard
uses one draw call; driving uses three (scenery, static indexed car mesh, HUD). The display clock normally limits it to the connected display's
refresh rate. `U` removes that limit at the cost of more CPU/GPU work. Rendering
pauses when hidden or minimized; sound continues.

Text uses the native Mac monospaced font at 11.5, 13, 20, 30, and 64 points, with
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
stop to avoid stalling. Full brake requests an arcade 2.6 g before drag; braking cuts the
accelerator, inhibits new upshifts, and uses higher downshift thresholds to
retain engine braking. The GUI only sends pedal/mode commands.

`A` toggles automatic Drive on the dashboard; `G` does so in the driving view.
Hold `R`/`W` for acceleration and `S` for brakes. In driving view, Space also
brakes and `X` starts/stops ignition; dashboard Space retains its ignition action.
Keyboard/mouse pedal holds are released on focus loss. The throttle slider
remains a persistent pedal setting. Manual clutch/gear control and the dyno
exit automatic mode. Drive works with the scripted forward gears; there is no
reverse gear or separately simulated torque converter.

## Driving game

`V` switches between the game and engine dashboard. `--road` selects the game
on launch. A/D or left/right steer, R/W/up accelerate, S/down/Space brake, X
operates ignition, C recovers, and
Backspace starts a new run. Pedals and steering release on focus loss. The game
reserves arrow keys for driving even when a dashboard slider previously had focus.
WASD steering cannot change Drive or the dyno; `G` toggles Drive in the game.
Held aliases release independently, so releasing D while right-arrow is held
does not centre the steering. Dashboard A/D retain their gearbox/dyno actions.

`engine-sim-driving` is a device-independent library with a closed Catmull-Rom
forest circuit in metres and an arcade chassis. It uses speed-sensitive steering,
assisted grip, body roll/pitch, and forgiving barrier projection. A wall impact
aligns the car along the barrier rather than throwing it sideways. The engine's
vehicle distance and speed supply longitudinal movement. The track is approximately 4.2 km long and 15 m wide,
with barriers at 12.5 m from its centre. Its broader bends and runoff give
drivers more room for high-speed corrections. Off-road resistance and impacts
add bounded opposing force to the existing
vehicle drag constraint, so slowing down remains part of the engine/drivetrain
simulation. This is approximate game handling, not a calibrated tire/suspension model.

Keyboard input uses a cubic ramp: small taps make precise corrections, while a
held key reaches full turn strength in about 167 ms. Releasing centres the input
in 50 ms, and the turn rate settles rapidly without residual sideslip. At 100 mph,
full lock gives about a 57 m turning radius, deliberately beyond realistic tyre
grip. The car's travel follows its heading; it does not keep sliding toward a wall
after input is released. Grass retains 85% of steering authority, blended from
individual tyre contact patches. The engine still owns forward speed, distance,
gearbox load and sound. Integration remains bounded at 240 Hz on the game worker.
The HUD's steering marker displays normalized input, independent of wheel lock.
Road resistance scales with the off-road fraction. The HUD's corner-speed advice
uses the new turning envelope and looks 250 metres ahead, with braking/reaction
distance. Guidance never applies pedals or steering for normal play.

Eight ordered forward checkpoints validate each lap. Crossing the finish backwards
or circling near the start cannot award laps. Three laps finish the time trial.
Recovery brakes to a stop before teleporting to the last checkpoint and adds a
three-second penalty. A new run preserves the session's best lap. Timers use the
engine clock; time keeps advancing during GUI/render stalls and while hidden.

`DrivingGameWorker` runs separately at 120 Hz. It samples coherent atomic vehicle
telemetry and sends a latest-value road-resistance value with a 200 ms expiry.
The engine worker never takes a game/render lock or waits for a game update.
The audio device callback is unchanged. A stopped game worker lets the external
load expire; a stopped renderer cannot freeze steering or collision detection.
Game snapshots are interpolated for presentation, about one game tick behind;
frames never advance physics. The game worker has bounded catch-up work.

`src/sound_road_scene.h` owns static metre-scaled track/scenery geometry and uses
the device-free `DrivingCamera`. A single heading response controls both the chase
offset and aim, while translation follows the interpolated game pose directly.
Speed cannot stretch the camera distance, and camera filters no longer compound
steering lag. It clips/culls scenery on the
render thread. Textures, curbs, barriers and checkpoint markers remain in world
coordinates. The sun is projected from a fixed world direction, with a 0.53-degree
angular diameter, and disappears outside the camera view. The same direction
lights scenery and the car. Straight travel correctly leaves this distant sun
in place; steering rotates it across the sky.
`src/sound_car_metal.h` uploads the credited Porsche 911 GT3 mesh
(168,224 vertices, 234,580 triangles) and concept body before audio starts.
The `porsche_911_gt3` preset selects the GT3; other presets select the concept
body and display that fact. Switching only selects cached GPU buffers. Metal
steers/rolls the wheels with each body's tyre radii, and transforms
and lights the body. Mipmapped image planes supply foliage; a depth buffer resolves
the scene. The dashboard uses one draw call; the game uses three (scenery, car, HUD).
PNG encoding runs on a utility queue after GPU readback. See
[asset credits](../THIRD_PARTY_NOTICES.md) for sources/licenses.

The game has one circuit, GT3 and concept bodies, forward driving, and a time
trial mode. It does not yet have opponents, reverse, a handbrake or matching
bodies for every engine. The game/test pilot is only enabled by validation flags,
never normal play.

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

For a complete programmatic circuit/impact/recovery and audio check:

```sh
./run-sound-gui.sh --preset porsche_911_gt3 \
  --game-test "$PWD/build/audio-validation/gt3-game" \
  --log "$PWD/build/audio-validation/gt3-game-playback.log"
```

This uses a test-only driver issuing discrete left/released/right inputs at
10 Hz through the normal keyboard ramp, alongside timed pedal commands. The
renderer benchmark retains its analogue pilot. The game test completes a
lap, deliberately drives into a barrier, recovers and drives again. It separately
stalls AppKit and the renderer for 1.2 seconds each, checks that game physics and
PCM continue, and writes JSON, logs and Metal PNGs. A test-only software-mixer
observer checks missing, silent, nonfinite and clipped output without changing
PCM. Add `--offscreen --silent` for hidden dummy audio. Portable game tests cover
three-lap finish detection, a keyboard circuit with acceleration/braking,
high-speed tap/release/countersteer response, partial-shoulder grip, barrier
containment, recovery, invalid inputs, no-motion data and resistance forces.
Native tests cover steering/pedal
holds, releases, focus changes and engine switching.

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

## Arcade handling validation

The arcade update replaces the tyre-force lateral chassis with assisted turning.
In the deterministic 100 mph input check, full lock reaches a roughly 57 m turn
radius; release settles below 0.1 g in 88 ms and opposite 0.7 g countersteering
in 167 ms. A 100 ms tap moves the car about 0.29 m sideways over 1.5 seconds.
These are intentional game responses, not real-car performance estimates.
A test driver using left/released/right keys at 10 Hz completed the 4.27 km course
at up to 112 mph with no collisions or shoulder contact. This verifies control
behaviour but does not substitute for player feedback about feel.

The actual simulated Porsche drivetrain stops from just below 100 mph in
37.7 m / 1.70 s and from just below 60 mph in 13.8 m / 1.03 s. The Supra also
passes the stronger stopping bounds. Both retain a running engine and return
to first gear; brakes still cut throttle and prevent new upshifts. Space is a
brake in driving view; X operates ignition. Dashboard Space retains ignition.

All 73 portable checks and 80 packaged checks passed, as did the native input
suite across seven engines. Direct Metal captures confirm the resized minimap
and control labels. An onscreen CoreAudio run logged zero missing frames over
23 seconds, including forced UI and render stalls, but ended before the full-lap
PCM report; no completed full-lap audio result is claimed for this update.

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

With the earlier inset chase camera and automatic gearbox, a 20-second GT3/CoreAudio run at
2560 x 1600 averaged **59.60 completed FPS**, **0.45 ms CPU / 3.46 ms GPU** per
frame, with **zero missing audio frames and zero Metal errors**. It rendered
72 frames through a 1.2-second AppKit stall. The vehicle travelled 294.99 m
before braking to a stop; subsequent frames kept that distance fixed.

The full-window scene with the detailed car mesh and forest completed a newer
20-second GT3/CoreAudio run at 2560 x 1600: **59.65 completed FPS**, **0.29 ms CPU /
4.11 ms GPU** per frame, **zero missing audio frames and zero Metal errors**.
It completed 72 frames during the 1.2-second AppKit stall and correctly stopped
road travel after braking. A separate dummy-audio run averaged 60.00 FPS.
These software measurements do not establish downstream device/acoustic latency.

The playable forest circuit completed a 20-second GT3/CoreAudio onscreen run at
2560 x 1600 with **60.00 completed FPS**, **0.48 ms CPU / 4.30 ms GPU** per frame,
zero missing audio frames and zero Metal errors. It completed 72 frames during a
1.2-second AppKit stall. The native steering/pedal/focus suite passed on all seven
tested engines. A separate **104.8-second CoreAudio game check** completed a clean
lap, deliberate barrier impacts, recovery and another acceleration, with zero
missing audio frames, silent PCM blocks, clipped PCM samples or Metal errors.
Both 1.2-second forced UI/render stalls preserved game physics and sound.
The separate audio-only start/rev/restart/switch check passed with zero missing,
clipped or unexpected silent output and **35.60 ms maximum command-to-mixer
response**, with the original sample reserve unchanged.

After fixing steering/camera response and world-space sun projection, the GT3
completed another onscreen 20-second CoreAudio check at 2560 x 1600: **60.00 FPS**,
**0.51 ms CPU / 4.38 ms GPU**, zero missing frames/write/Metal errors, and 72
rendered frames during a 1.2-second UI stall. The separate **103.75-second real
audio game check** completed a clean lap, deliberate impact, recovery and
acceleration with zero missing, silent or clipped PCM. Both forced UI/render
stalls preserved driving physics and sound. Sun projection moved across the
view and disappeared on the circuit; GPU PNGs were inspected without computer
use. The 65 portable and 72 packaged tests passed, including steering response,
release, countersteering, frame-independent motion and camera/sun geometry.

With the Porsche GT3 body, progressive keyboard input and tyre-force chassis,
the final 20-second onscreen CoreAudio check at 2560 x 1600 sustained **60.00 FPS**,
**0.54 ms CPU / 2.63 ms GPU** per frame, with zero missing audio frames, write
errors or Metal errors. A **105.12-second CoreAudio game check** completed a clean
lap, deliberate collision, recovery and acceleration, with zero missing, silent
or clipped PCM. Forced UI/render stalls still preserved physics and audio.
The seven-engine native input suite and all **66 portable / 73 packaged tests**
passed. Models preload into GPU memory before audio starts; the callback and
audio sample reserve are unchanged.

With the firmer road tyre tune, the 100 mph keyboard release test settled
below 0.1 g in **304 ms**, down from **592 ms**; countersteering reached the
opposite 0.7 g in **400 ms**, down from **625 ms**. The short-tap checks still
limit a 100 ms press to less than one degree of heading change. The portable
keyboard driver completed the circuit above 80 mph without touching the shoulder.
All **71 portable / 78 packaged tests** passed. A **103.50-second CoreAudio
keyboard game check** completed a clean 96.76-second lap, deliberate impact,
recovery and acceleration with zero missing, silent or clipped PCM and no Metal
errors. Both forced UI/render stalls preserved physics and audio. The separate
20-second onscreen run at 2560 x 1600 sustained **60.00 FPS**, **0.63 ms CPU /
4.44 ms GPU**, with zero missing frames and 72 rendered frames during the UI
stall. The audio-only control check passed with **35.86 ms maximum
command-to-mixer response**, zero missing/clipped/unexpected silent output,
and the existing audio sample reserve.
The separate real-output audio/control check passed with zero missing, clipped
or unexpected silent samples and **34.0 ms maximum command-to-mixer response**.

Before the arcade handling update, recorded full-brake runs showed the GT3
stopping from approximately 60 mph in
**30 m / 2.3 s**, compared with about **38 m / 2.9 s** before; near 100 mph it
stopped in **80 m / 3.7 s**, compared with **101 m / 4.7 s**. Sampling began just
below each speed threshold; these are game measurements, not manufacturer data.
A shift-phase clutch regression caught by the real output test was fixed so
braking to a standstill retains engine idle. A 100 ms steering tap at 100 mph
produced about **0.22 m** lateral correction over 1.5 seconds, versus **1.08 m**
with the previous immediate full-steering input.

The smoke harness detects early exits as failures by requiring the completed
report and result marker:

```sh
python3 test/sound_gui_smoke.py --game-only --presets porsche_911_gt3
# Makes sound through the system output:
python3 test/sound_gui_smoke.py --game-only --real-audio --presets porsche_911_gt3
```

Separate 41-second automatic-driving audio checks passed for the Supra on the
dummy device and GT3 on CoreAudio: five upshifts each, actual RPM drops and
throttle cuts, no missing/clipped/unexpected silent PCM, and no stall at rest.

See [ENGINES.md](ENGINES.md) for model limitations, including the radial-9 startup
issue at the host's default simulation frequency. Hosted CI validates builds,
core behavior, scripts, and dummy-device audio adapters; real-device timing and
native render tests are local checks.
