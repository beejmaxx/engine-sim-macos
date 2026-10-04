# Engines and model provenance

Press **E**, click **ENGINE LIBRARY**, or use the macOS **Engines** menu. There
are 24 presets. `./run-sound-gui.sh --list-engines` prints their command-line IDs.
Select an engine, press **X** in the game or **Space** on the dashboard to start,
and hold **R** to open the throttle.

| Added preset | ID | Configuration |
| --- | --- | --- |
| Porsche 911 GT3 4.0 | `porsche_911_gt3` | 3,996 cc flat-six, 9,000 RPM limit, 5 kHz simulation |
| Porsche GT3 Sprint | `porsche_911_gt3_sprint` | Same GT3 engine/body, fictional lightweight arcade driveline, seven speeds |
| Porsche 911 Carrera 3.2 | `porsche_911_carrera_32` | 3,164 cc flat-six, 6,500 RPM model limit, 2.5 kHz simulation |
| BMW M52B28 | `bmw_m52b28` | Existing upstream 2.8 L straight-six script, now selectable |

The library also includes Toyota Supra 2JZ, GM LS V8, Ferrari F136 V8 and
412 T2 V12, Lexus LFA V10, Audi I5, Subaru EJ25 variants, Honda, Suzuki,
Harley-Davidson, Merlin V12, radial engines, and several V6 examples.

## Car bodies

In the game, press **E** and select one of these entries. Changing the engine
also selects its cached body; no model downloads or texture loading happen
while driving. The HUD identifies the active body.

| Menu entry | Preset ID | Body / asset credits |
| --- | --- | --- |
| Toyota Supra Mk4 / 2JZ | `supra` | [1998 Supra Mk4](../assets/vehicles/supra_mk4/README.md), pearl white |
| Ferrari 458 Italia / F136 V8 | `ferrari_f136_v8` | [Open-roof 458](../assets/vehicles/ferrari_458/README.md), red |
| Chevrolet Corvette C7 / LS V8 swap | `ls` | [Corvette C7](../assets/vehicles/corvette_c7/README.md), blue |
| Porsche 911 GT3 / Porsche GT3 Sprint | `porsche_911_gt3` / `porsche_911_gt3_sprint` | [911 GT3](../assets/vehicles/porsche_gt3/README.md) |
| Other engines | All other IDs | [Concept car](../assets/vehicles/concept/README.md) |

The Corvette is explicitly an LS V8 swap in the game; its stock C7 drivetrain
is not simulated. The 458 source is titled “Ferrari 458 Italia” by its author
and depicts an open-roof car. Bodies are approximately metre-scaled, with
separate wheels, materials and brake lamps. They do not change the existing
sound models, transmissions or arcade handling. These visual assets are
separately licensed under CC BY 4.0, with author notices in each asset directory.

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

### GT3 Sprint arcade tune

The Sprint is a fictional performance preset, not a Porsche production model.
It shares the complete engine and exhaust configuration in `porsche/gt3_engine.mr`
with the normal GT3. It uses a 760 kg virtual vehicle, 0.20 drag coefficient,
1.65 m² frontal area, 140 N rolling resistance, and seven ratios
(3.80 / 2.65 / 1.94 / 1.48 / 1.16 / 0.93 / 0.69). The final drive is 3.42.
The engine still drives the vehicle through the simulated clutch and gearbox,
so RPM, shifts, acceleration and sound remain linked. Existing arcade brakes,
steering and the reverse speed limit still apply. No additional synthesis work
or per-frame rendering work is needed.

Run `./run-fast.sh` to start it in Portside City with automatic Drive, or select
**Porsche GT3 Sprint** from the native Engines menu.

Measured in the deterministic drivetrain test at full throttle (simulation
results, not real-car claims):

| Measurement | Normal GT3 | GT3 Sprint |
| --- | ---: | ---: |
| 0–60 mph | 3.675 s | 2.705 s |
| 0–100 mph | 9.095 s | 5.610 s |
| Speed after 24 seconds | 146.975 mph | 202.240 mph |
| 100 mph to stopped | 1.700 s / 37.65 m | 1.700 s / 37.73 m |

The stopping figures use the intentionally strong arcade brakes. The 24-second
figure is the speed reached in that run, not an established maximum speed.

## Adding an engine

Place a script under `assets/engines/<group>/` with a `public node main` that
calls `set_engine`, `set_vehicle`, and `set_transmission`. Reconfigure CMake,
rebuild, and install: the catalog is generated at configure time. Helper scripts
without `main` are not shown. Numeric filename prefixes such as `01_` are
removed from the command-line ID.

The Mac host normally uses 2,500 simulation steps per second. Both GT3 presets and
Ferrari 412 T2 use 5,000. Per-engine host settings live in
`SoundSession::presets()` in `src/sound_session.cpp`; these override the script
frequency to keep the audio producer within its real-time budget.

Only contribute scripts/assets you have permission to redistribute. Preserve
their license and attribution. A public download page alone is not a license.

## Current limits

All catalog scripts are compile-tested. Runtime checks cover the Supra, LS,
both Ferraris, all three Porsche presets, and BMW; the rest are inherited examples and have
not all been tuned for this host's lower simulation frequencies. The radial-9
example currently stalls at the 2.5 kHz host setting. This simulator is for sound
and experimentation, not engineering measurements or tuning decisions.
