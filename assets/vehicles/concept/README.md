# Concept-car mesh

`car.mesh` derives from **Car Concept**, © 2024 Darmstadt Graphics Group GmbH,
Eric Chadwick. Model and textures are licensed under **CC BY 4.0**.

- [Source and author credits](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/CarConcept)
- [Original GLB](https://raw.githubusercontent.com/KhronosGroup/glTF-Sample-Assets/main/Models/CarConcept/GLB/CarConcept.glb)
- [License text](CC-BY-4.0.txt), [original source notice](SOURCE-LICENSE.md)
- [Original Khronos marks notice](Khronos-marks.txt); logo textures are not imported.

The source credits UnityFan's public-domain concept car as its starting point.
This is a generic concept body, not a Porsche or a model of the selected engine's car.

Our conversion bakes node transforms into a compact indexed mesh, preserves
normals/material factors and wheel pivots, straightens front-wheel steering,
and separates rolling wheels from fixed brake pads. It changes glass/interior
material factors and omits textures, logos, animations, transmission and
iridescence extensions. The host supplies its own lighting and wheel animation.

To reproduce (NumPy is needed only for this optional development step):

```sh
python3 tools/import_concept_car.py CarConcept.glb assets/vehicles/concept
```

The checked-in binary is used directly; building or running the app requires no
asset download or Python packages. This asset retains CC BY 4.0 and is not
relicensed under the repository's MIT code license.
