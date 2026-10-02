# Contributing to Engine Sim for macOS

This independent Mac fork builds on
[AngeTheGreat's Engine Simulator](https://github.com/ange-yaghi/engine-sim) and
[Carles Onielfa's Open Engine Simulator](https://github.com/carlesonielfa/open-engine-sim).
Please keep upstream attribution and the MIT license intact.

## Before you start

1. Read [README.md](README.md) and [AGENTS.md](AGENTS.md).
2. Clone recursively: `git clone --recurse-submodules <fork-url>`.
3. Use the CMake presets or the Makefile wrappers; do not depend on a local
   IDE-only configuration.
4. Open an issue or start a discussion before a large simulation, scripting,
   renderer, or public-API change.

## Development workflow

Build and test the portable core:

```sh
make PLATFORM=macos-arm64 portable-test
```

Build and install the native Mac dashboard:

```sh
cmake --preset macos-arm64-package -DBUILD_TESTING=ON
cmake --build --preset macos-arm64-package --parallel 4
ctest --test-dir build/macos-arm64-package --output-on-failure
cmake --install build/macos-arm64-package --prefix "$PWD/dist"
```

See [docs/MACOS.md](docs/MACOS.md) for programmatic audio and native input/render
tests. The inherited portable core also has `linux-x86_64` and `windows-x86_64`
CMake presets. The new AppKit/Metal dashboard is macOS-only.

## Contribution guidelines

- Keep core simulation, synthesis, scripting, rendering, and SDL host code
  separated. Core and scripting public headers must not expose SDL types.
- Keep Piranha runtime assets under `assets/es`; do not introduce a source-tree
  runtime dependency.
- Preserve real-time audio safety: no allocation, disk I/O, logging, or
  blocking synchronization in the audio render path.
- Include tests for portable behavior changes. Verify desktop/audio changes
  with the SDL dummy-audio test and a native visual/audio smoke check where
  practical.
- Update README/build documentation when requirements, controls, package
  layout, or asset workflows change.
- Keep pull requests focused, explain user-visible behavior, and avoid mixing
  formatting-only churn with functional changes.

## Assets and generated files

`art/assets.blend` is the authored mesh source. When it changes, regenerate
`assets/authored_meshes.obj` with:

```sh
make export-meshes
```

Commit the intentional export alongside its source change. Do not commit build
directories, local Blender backups, `.DS_Store`, or generated logs.
