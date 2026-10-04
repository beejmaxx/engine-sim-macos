# 1998 Toyota Supra body

This work is based on [1998 Toyota Supra](https://sketchfab.com/3d-models/1998-toyota-supra-b9ee69e17af947c0bce1c54d34195187) by [BHP3D](https://sketchfab.com/BHP3D), licensed under [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/).

The converted mesh and textures retain that license, separate from the MIT code.
[Full license](CC-BY-4.0.txt) and [original author notice](SOURCE-LICENSE.txt) are included.

Source: [pinned asset mirror](https://github.com/anvnh/auto_showroom/tree/0d8036c5a10640d3ac0faf9f92f134becba6bf1c/client/public/3d/supra). The Supra/2JZ preset selects this body.

Our offline conversion bakes the node transforms, changes from the glTF coordinate
system to the game’s projection, scales the car to approximate metre dimensions,
grounds the tyres, and positions the front axle at the chassis pivot. It preserves
normals, material groups and rolling/steering wheel pivots. Materials are adjusted
for paint, glass, metal and rubber; brake lights use the live brake control.
Supplied base-colour textures are resized to 512 × 512 pixels; other texture maps
and source animations are omitted. The native host provides mipmapping, lighting,
wheel animation and body motion. The model is uploaded before audio starts and
uses one indexed GPU draw while driving. See [mesh statistics](mesh-info.json).

Reproduce with NumPy, Pillow, DracoPy 2.1.0 and meshoptimizer 0.2.30a0 installed
only in the offline conversion environment:

```sh
python3 tools/import_road_cars.py build/car-sources/supra/scene.gltf \
  --profile supra --output assets/vehicles/supra_mk4
```

Use the glTF/GLB, referenced binary and base-colour textures from the pinned source.
The app packages the converted files and requires no account or runtime downloads.
The body and drivetrain are for the game, not a manufacturer-calibrated model.
