# Porsche 911 GT3 body

This work is based on [Porsche 911 GT3](https://sketchfab.com/3d-models/porsche-911-gt3-78d5c47ab2554c2592b7e499179a0792)
by [ChevroletSS](https://sketchfab.com/ChevroletSS), licensed under
[CC BY 4.0](https://creativecommons.org/licenses/by/4.0/).

The model is selected by the `porsche_911_gt3` engine preset. Other engines
currently retain the separately credited concept body. Neither body is a
manufacturer-endorsed or calibrated vehicle simulation.

The source glTF and author notice were obtained from the public
[Porsche-Timeline mirror](https://github.com/YoanMln/Porsche-Timeline/tree/d030dc224b6ca87ca836fc8f34eb050ba132fc38/assets/threeJS/991gt3rs)
at commit `d030dc224b6ca87ca836fc8f34eb050ba132fc38`.
The mirror's directory name is `991gt3rs`; the embedded source metadata and
license identify the work as **Porsche 911 GT3**.

[Full license](CC-BY-4.0.txt) and [original author notice](SOURCE-LICENSE.txt)
are included. The mesh retains this license, separate from the MIT code.

Our offline conversion bakes glTF transforms, scales the body to 4.55 metres,
grounds the tyres, and centers the chassis at its centre of gravity. It retains
the authored normals, body, interior, rear wing, lights, and separate wheels.
It removes blurred wheel duplicates and hidden damage glass, changes material
factors for paint, glass, rubber and lights, and assigns rolling wheel pivots
and brake-light materials. The host provides lighting, suspension, steering,
wheel animation and brake-light illumination. Texture/decal maps are omitted.
The result contains 168,224 vertices and 234,580 triangles; both bodies are
uploaded before engine audio starts and share one GPU draw per frame.

To reproduce (NumPy is needed only for this optional development step):

```sh
python3 tools/import_porsche_gt3.py scene.gltf assets/vehicles/porsche_gt3
```

Use `scene.gltf` and `scene.bin` from the pinned mirror directory. The packaged
app includes the converted mesh, so building and running require no download,
asset service, account, or Python dependency.
