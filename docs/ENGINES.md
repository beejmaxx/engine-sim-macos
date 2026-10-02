# Engines and model provenance

Press **E**, click **ENGINE LIBRARY**, or use the macOS **Engines** menu. There
are 23 presets. `./run-sound-gui.sh --list-engines` prints their command-line IDs.
Select an engine, press **Space** to start, and hold **R** to open the throttle.

| Added preset | ID | Configuration |
| --- | --- | --- |
| Porsche 911 GT3 4.0 | `porsche_911_gt3` | 3,996 cc flat-six, 9,000 RPM limit, 5 kHz simulation |
| Porsche 911 Carrera 3.2 | `porsche_911_carrera_32` | 3,164 cc flat-six, 6,500 RPM model limit, 2.5 kHz simulation |
| BMW M52B28 | `bmw_m52b28` | Existing upstream 2.8 L straight-six script, now selectable |

The library also includes Toyota Supra 2JZ, GM LS V8, Ferrari F136 V8 and
412 T2 V12, Lexus LFA V10, Audi I5, Subaru EJ25 variants, Honda, Suzuki,
Harley-Davidson, Merlin V12, radial engines, and several V6 examples.

## Porsche approximations

These are new MIT-licensed examples derived from Ange Yaghi's bundled
`atg-video-2/01_subaru_ej25_eh.mr`. They are not downloaded community mods,
recordings of real cars, or factory-calibrated performance models.

The common `porsche/flat_six.mr` defines two opposed three-cylinder banks,
six crank journals, and an even-fire 1-6-2-4-3-5 sequence. Each bank has its own
exhaust system. The examples differ in displacement, compression, cam profile,
flywheel inertia, exhaust length, and the bundled exhaust impulse response.

The GT3 uses 102 mm bore and 81.5 mm stroke from
[Porsche's 992 GT3 technical data](https://newsroom.porsche.com/dam/jcr:15fa49de-7fc3-4770-a09d-1b8bf451ddbd/PAG-992-911GT3-PDK-EN.pdf).
Its 9,000 RPM limit follows
[Porsche's powertrain description](https://newsroom.porsche.com/en/press-kits/911-GT3/Powertrain-and-performance.html).
The Carrera uses 95 mm bore, 74.4 mm stroke, 10.3:1 compression, and a 1,210 kg
vehicle mass from [Porsche's description of a 1984 Carrera 3.2](https://newsroom.porsche.com/en/2019/history/porsche-klassik-911-carrera-company-car-assistant-porsche-ceoexclusive-label-tilman-brodbeck-16209.html).

Other inputs are approximations: head flow, cam timing/lift, intake geometry,
ignition advance, piston/rod/flywheel mass, exhaust geometry, driveline ratios,
and the GT3's compression setting. In particular, the GT3 uses a shared intake
instead of simulating its individual throttle bodies. The Carrera does not
synthesize a separate cooling-fan sound. No horsepower, torque, or acoustic
match to a real car is claimed.

## Adding an engine

Place a script under `assets/engines/<group>/` with a `public node main` that
calls `set_engine`, `set_vehicle`, and `set_transmission`. Reconfigure CMake,
rebuild, and install: the catalog is generated at configure time. Helper scripts
without `main` are not shown. Numeric filename prefixes such as `01_` are
removed from the command-line ID.

The Mac host normally uses 2,500 simulation steps per second. The GT3 and
Ferrari 412 T2 use 5,000. Per-engine host settings live in
`SoundSession::presets()` in `src/sound_session.cpp`; these override the script
frequency to keep the audio producer within its real-time budget.

Only contribute scripts/assets you have permission to redistribute. Preserve
their license and attribution. A public download page alone is not a license.

## Current limits

All catalog scripts are compile-tested. Runtime checks cover the Supra, LS,
both Ferraris, both Porsches, and BMW; the rest are inherited examples and have
not all been tuned for this host's lower simulation frequencies. The radial-9
example currently stalls at the 2.5 kHz host setting. This simulator is for sound
and experimentation, not engineering measurements or tuning decisions.
