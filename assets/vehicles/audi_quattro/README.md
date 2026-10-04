# Audi Quattro Sport Stock

This work is based on [Audi Quattro Sport Stock](https://sketchfab.com/3d-models/audi-quattro-sport-stock-e81aa698ddba46838c94fe52be81d378) by Pitstop 3D - Euro (https://sketchfab.com/carfan100),
licensed under [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/).
The converted mesh and textures retain that license, separate from the MIT code.
The [author notice](SOURCE-LICENSE.txt), [source metadata](SOURCE-METADATA.json)
and [full license](CC-BY-4.0.txt) are included.

Source: [pinned public asset mirror](https://github.com/jemini-io/react-three-fiber-example/blob/cde4e39cf2673bf2ab5bc8e42a35f7e620a42f14/public/audi_quattro_sport_stock/scene.gltf).

Selection: Audi inline-five preset.

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
python3 tools/import_car_library.py build/car-sources/all-cars/audi/scene.gltf \
  --profile assets/vehicles/audi_quattro/import-profile.json \
  --output assets/vehicles/audi_quattro
```

Use the source file plus its referenced buffers/base-colour images from the
pinned mirror. The individual model's CC BY license applies to this derivative.
