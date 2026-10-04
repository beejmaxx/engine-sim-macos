# Attribution and third-party notices

This is an independent community fork, maintained at
[beejmaxx/engine-sim-macos](https://github.com/beejmaxx/engine-sim-macos).

- **Engine Simulator**, by Ange Yaghi (AngeTheGreat) and contributors:
  [ange-yaghi/engine-sim](https://github.com/ange-yaghi/engine-sim).
  The engine simulation, scripting integration, engine examples, audio assets,
  and original visual design come from this project. Its MIT copyright and
  permission notice are preserved in [LICENSE](LICENSE).
- **Open Engine Simulator**, by Carles Onielfa and contributors:
  [carlesonielfa/open-engine-sim](https://github.com/carlesonielfa/open-engine-sim).
  This fork builds on its portable CMake/core architecture, SDL host, engine
  catalog, asset pipeline, and cross-platform work. Git history is preserved.
- **New Mac host code and Porsche examples:** these additions are provided
  under the repository's MIT license. The Porsche examples derive from the
  upstream Subaru example; see [docs/ENGINES.md](docs/ENGINES.md).
- **Car Concept**, © 2024 Darmstadt Graphics Group GmbH, Eric Chadwick, from
  [Khronos glTF Sample Assets](https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/CarConcept),
  is used under **CC BY 4.0**. The converted mesh retains that license. See
  [source, modifications and license](assets/vehicles/concept/README.md).
- **Porsche 911 GT3**, by [ChevroletSS](https://sketchfab.com/ChevroletSS), is
  used under **CC BY 4.0**. This work is based on
  [the original Porsche 911 GT3 model](https://sketchfab.com/3d-models/porsche-911-gt3-78d5c47ab2554c2592b7e499179a0792).
  The converted mesh retains that license; see
  [source, modifications and license](assets/vehicles/porsche_gt3/README.md).
- **1998 Toyota Supra**, by [BHP3D](https://sketchfab.com/BHP3D), is used under
  **CC BY 4.0**. This work is based on
  [the original model](https://sketchfab.com/3d-models/1998-toyota-supra-b9ee69e17af947c0bce1c54d34195187).
  The converted mesh and textures retain that license; see
  [source, modifications and license](assets/vehicles/supra_mk4/README.md).
- **Ferrari 458 Italia**, by [vicent091036](https://sketchfab.com/vicent091036),
  is used under **CC BY 4.0**. This work is based on
  [the original model](https://sketchfab.com/3d-models/ferrari-458-italia-57bf6cc56931426e87494f554df1dab6),
  distributed in optimized form by the three.js car example. The converted
  mesh retains that license; see
  [source, modifications and license](assets/vehicles/ferrari_458/README.md).
- **Chevrolet Corvette (C7)**, by [Martin Trafas / Bexxie](https://sketchfab.com/Bexxie),
  is used under **CC BY 4.0**. This work is based on
  [the original model](https://sketchfab.com/3d-models/chevrolet-corvette-c7-2b509d1bce104224b147c81757f6f43a).
  The converted mesh and textures retain that license; see
  [source, modifications and license](assets/vehicles/corvette_c7/README.md).
- **Pine Tree 01 preview render**, by Rico Cilliers and Rob Tuytel, from
  [Poly Haven](https://polyhaven.com/a/pine_tree_01), is used under **CC0 1.0**.
  See [scenery source and license](assets/scenery/README.md).
- **SDL3**, by Sam Lantinga and contributors, uses the zlib license, reproduced
  in [third_party/licenses/SDL3.txt](third_party/licenses/SDL3.txt). Packaged Mac
  apps include an unmodified SDL3 dynamic library and this notice.
- **Silkscreen**, by Jason Kottke: the unmodified font and its separate
  redistribution notice remain together in `assets/fonts/`. See
  [Kottke Silkscreen License.txt](assets/fonts/Kottke%20Silkscreen%20License.txt).
- **stb_truetype**, by Sean Barrett and contributors, retains its MIT/public-domain
  notices in [third_party/stb/stb/stb_truetype.h](third_party/stb/stb/stb_truetype.h).
- **simple-2d-constraint-solver** and **csv-io**, by Ange Yaghi, are pinned
  submodules with their own MIT license files. **Piranha**, also by Ange Yaghi,
  is retained as the upstream pinned submodule; this repository does not
  relicense that separate project. Their source locations are in
  [.gitmodules](.gitmodules).
- **GoogleTest**, used only for development tests, retains its BSD license in
  its separately fetched source distribution.

Manufacturer names identify the modeled engines and bodies. This project is not
affiliated with or endorsed by Porsche, BMW, Toyota, Ferrari, Chevrolet, or other
manufacturers.
