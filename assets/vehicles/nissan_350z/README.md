# Nissan 350z

This work is based on [Nissan 350z](https://sketchfab.com/3d-models/nissan-350z-18c081f765854d249bb8dc580a1e9f7c) by David_Holiday (https://sketchfab.com/David_Holiday),
licensed under [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/).
The converted mesh and textures retain that license, separate from the MIT code.
The [author notice](SOURCE-LICENSE.txt), [source metadata](SOURCE-METADATA.json)
and [full license](CC-BY-4.0.txt) are included.

Source: [pinned public asset mirror](https://huggingface.co/datasets/allenai/objaverse/resolve/21e4e142159e2153706c23a3a02e55cec5591cea/glbs/000-148/18c081f765854d249bb8dc580a1e9f7c.glb).

Selection: All three generic V6 engine examples use this modified 350Z body; no factory VQ engine match is claimed.

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
python3 tools/import_car_library.py build/car-sources/all-cars/nissan350z/source.glb \
  --profile assets/vehicles/nissan_350z/import-profile.json \
  --output assets/vehicles/nissan_350z
```

Use the source file plus its referenced buffers/base-colour images from the
pinned mirror. The individual model's CC BY license applies to this derivative.

The source GLB is distributed by the [Allen Institute for AI Objaverse dataset](https://huggingface.co/datasets/allenai/objaverse), revision `21e4e142159e2153706c23a3a02e55cec5591cea`. Credit is retained to the original artist; the dataset is only the download mirror.
