# Ferrari 458 Italia body

This work is based on [Ferrari 458 Italia](https://sketchfab.com/3d-models/ferrari-458-italia-57bf6cc56931426e87494f554df1dab6) by [vicent091036](https://sketchfab.com/vicent091036), licensed under [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/).

The converted mesh and textures retain that license, separate from the MIT code.
[Full license](CC-BY-4.0.txt) and [original author notice](SOURCE-LICENSE.txt) are included.

Source: [pinned asset mirror](https://github.com/mrdoob/three.js/blob/576b084aff43ec5bb79911befb1d51be178cb7ed/examples/models/gltf/ferrari.glb). The Ferrari F136 V8 preset selects this open-roof 458 body. The original author titles the model “Ferrari 458 Italia”.

Our offline conversion bakes the node transforms, changes from the glTF coordinate
system to the game’s projection, scales the car to approximate metre dimensions,
grounds the tyres, and positions the front axle at the chassis pivot. It preserves
normals, material groups and rolling/steering wheel pivots. Materials are adjusted
for paint, glass, metal and rubber; brake lights use the live brake control.
Supplied base-colour textures are resized to 512 × 512 pixels; other texture maps
and source animations are omitted. The native host provides mipmapping, lighting,
wheel animation and body motion. The model is uploaded before audio starts and
uses one indexed GPU draw while driving. See [mesh statistics](mesh-info.json).

The original author notice was preserved from [this pinned mirror](https://github.com/qwer13270/haunted_house/blob/b3de17809923335d00dc727a3f5be7814873e706/static/ferrari/license.txt). The optimized GLB is the version credited in [the official three.js car example](https://threejs.org/examples/webgl_materials_car.html).

Reproduce with NumPy, Pillow, DracoPy 2.1.0 and meshoptimizer 0.2.30a0 installed
only in the offline conversion environment:

```sh
python3 tools/import_road_cars.py build/car-sources/ferrari/ferrari.glb \
  --profile ferrari --output assets/vehicles/ferrari_458
```

Use the glTF/GLB, referenced binary and base-colour textures from the pinned source.
The app packages the converted files and requires no account or runtime downloads.
The body and drivetrain are for the game, not a manufacturer-calibrated model.
