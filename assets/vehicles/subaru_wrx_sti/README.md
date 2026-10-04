# 2010 Subaru Impreza WRX STi

This work is based on [2010 Subaru Impreza WRX STi](https://sketchfab.com/3d-models/2010-subaru-impreza-wrx-sti-b61292f5b9d2416990aa5bd502555f3a) by Galaxy Car Showroom (https://sketchfab.com/adrianaflak09),
licensed under [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/).
The converted mesh and textures retain that license, separate from the MIT code.
The [author notice](SOURCE-LICENSE.txt), [source metadata](SOURCE-METADATA.json)
and [full license](CC-BY-4.0.txt) are included.

Source: [pinned public asset mirror](https://github.com/YuFengjie97/nova-nuxt/blob/07e75a8705cc663733b16f7687f50f79f874af8f/public/model/2010_subaru_impreza_wrx_sti/scene.gltf).

Selection: All three Subaru EJ25 exhaust variants share this hatchback body.

Our offline conversion bakes glTF transforms, changes projection handedness,
scales the vehicle to approximate metre dimensions, grounds the tyres and
anchors its front axle. It assigns rolling/steering pivots, preserves UVs,
adjusts paint/glass/metal/rubber and brake-light materials, and resizes used
base-colour textures to 512 × 512. Other source maps and animations are omitted.
Some dense primitives are simplified; showroom/shadow geometry and duplicate
clear coats are excluded where specified in [the import profile](import-profile.json).
See [converted mesh statistics](mesh-info.json).

The native host caches the GPU buffers and textures before audio starts.
Only the selected car is drawn, with one indexed draw. No downloads, Python,
glTF parsing or image conversion occur while driving. These bodies do not
change engine synthesis, gearbox ratios or arcade handling.

Reproduce with NumPy, Pillow, DracoPy and meshoptimizer in an offline environment:

```sh
python3 tools/import_car_library.py build/car-sources/all-cars/subaru2010/scene.gltf \
  --profile assets/vehicles/subaru_wrx_sti/import-profile.json \
  --output assets/vehicles/subaru_wrx_sti
```

Use the source file plus its referenced buffers/base-colour images from the
pinned mirror. The individual model's CC BY license applies to this derivative.
