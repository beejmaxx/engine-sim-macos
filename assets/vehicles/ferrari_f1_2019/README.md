# Ferrari F1 2019

This work is based on [Ferrari F1 2019](https://sketchfab.com/3d-models/ferrari-f1-2019-1b050bffe4e749b586b4782ee7ff4fd0) by valvetin (https://sketchfab.com/valvetin),
licensed under [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/).
The converted mesh and textures retain that license, separate from the MIT code.
The [author notice](SOURCE-LICENSE.txt), [source metadata](SOURCE-METADATA.json)
and [full license](CC-BY-4.0.txt) are included.

Source: [pinned public asset mirror](https://github.com/ritesh-kumar289/cars-landing-page/blob/ce7d55c3ff627e9cade6a136a5b2f880585d1f3b/public/models/ferrari_f1_2019/scene.gltf).

Selection: Fictional 412 T2 V12 swap in a 2019 Ferrari F1 body, not a historically matching 1995 car. F1 has no brake lamps.

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
python3 tools/import_car_library.py build/car-sources/all-cars/f1/scene.gltf \
  --profile assets/vehicles/ferrari_f1_2019/import-profile.json \
  --output assets/vehicles/ferrari_f1_2019
```

Use the source file plus its referenced buffers/base-colour images from the
pinned mirror. The individual model's CC BY license applies to this derivative.
