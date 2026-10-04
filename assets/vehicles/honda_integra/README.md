# Honda Integra (DB8) Type-R

This work is based on [Honda Integra (DB8) Type-R](https://sketchfab.com/3d-models/honda-integra-db8-type-r-06f0eba84e9745e9aec41a985ccac915) by Myedsu (https://sketchfab.com/myedsu),
licensed under [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/).
The converted mesh and textures retain that license, separate from the MIT code.
The [author notice](SOURCE-LICENSE.txt), [source metadata](SOURCE-METADATA.json)
and [full license](CC-BY-4.0.txt) are included.

Source: [pinned public asset mirror](https://github.com/josiahthr/FinalGame/blob/9c3fb66495210c8b9dccc3b5f28ba7633d59af53/honda_integra_db8_type-r/scene.gltf).

Selection: Honda B18C5 preset in the four-door DB8 body.

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
python3 tools/import_car_library.py build/car-sources/all-cars/honda/scene.gltf \
  --profile assets/vehicles/honda_integra/import-profile.json \
  --output assets/vehicles/honda_integra
```

Use the source file plus its referenced buffers/base-colour images from the
pinned mirror. The individual model's CC BY license applies to this derivative.
