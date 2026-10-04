# Chevrolet Corvette (C7) body

This work is based on [Chevrolet Corvette (C7)](https://sketchfab.com/3d-models/chevrolet-corvette-c7-2b509d1bce104224b147c81757f6f43a) by [Martin Trafas](https://sketchfab.com/Bexxie), licensed under [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/).

The converted mesh and textures retain that license, separate from the MIT code.
[Full license](CC-BY-4.0.txt) and [original author notice](SOURCE-LICENSE.txt) are included.

Source: [pinned asset mirror](https://github.com/Domenicobrz/R3F-in-practice/tree/3b17e695df089a19fa31c3f43fb77226d5bc6330/car-show/public/models/car). The GM LS V8 preset selects this body. It is explicitly labeled an LS engine swap; the simulated LS is not the C7’s factory engine.

Our offline conversion bakes the node transforms, changes from the glTF coordinate
system to the game’s projection, scales the car to approximate metre dimensions,
grounds the tyres, and positions the front axle at the chassis pivot. It preserves
normals, material groups and rolling/steering wheel pivots. Materials are adjusted
for paint, glass, metal and rubber; brake lights use the live brake control.
Supplied base-colour textures are resized to 512 × 512 pixels; other texture maps
and source animations are omitted. The native host provides mipmapping, lighting,
wheel animation and body motion. The model is uploaded before audio starts and
uses one indexed GPU draw while driving. See [mesh statistics](mesh-info.json).

The dense source mesh is simplified offline to about 346,000 triangles, retaining material boundaries, normals, UVs and separate wheel/caliper assignments.

Reproduce with NumPy, Pillow, DracoPy 2.1.0 and meshoptimizer 0.2.30a0 installed
only in the offline conversion environment:

```sh
python3 tools/import_road_cars.py build/car-sources/corvette/scene.gltf \
  --profile corvette --output assets/vehicles/corvette_c7
```

Use the glTF/GLB, referenced binary and base-colour textures from the pinned source.
The app packages the converted files and requires no account or runtime downloads.
The body and drivetrain are for the game, not a manufacturer-calibrated model.
