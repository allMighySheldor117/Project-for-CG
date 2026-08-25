# Third-Party Licenses

The project vendors the following exact upstream revisions so that CMake can
configure and build without downloading project dependencies. Each dependency's
license text is retained beside its imported source.

## GLFW

- Release: 3.5.1
- Commit: `d9d6f0f1f967807ffade6598ea9a631ebaf37a56`
- Source: https://github.com/glfw/glfw
- License: zlib/libpng
- Local license: `third_party/glfw/LICENSE.md`
- Imported layout: root CMake file, `CMake/`, `include/`, `src/`, README,
  and license

## GLM

- Release: 1.0.3
- Commit: `8d1fd52e5ab5590e2c81768ace50c72bae28f2ed`
- Source: https://github.com/g-truc/glm
- License: Happy Bunny License or MIT
- Local license: `third_party/glm/copying.txt`
- Imported layout: root CMake file, `cmake/`, `glm/`, README, manual, and
  license

## Dear ImGui

- Release: v1.92.9b
- Commit: `f1cc2ae15e53a861a874c3034aae6798fde194ab`
- Source: https://github.com/ocornut/imgui
- License: MIT
- Local license: `third_party/imgui/LICENSE.txt`
- Imported layout: core headers/sources and the GLFW/OpenGL3 backend
  headers/sources only

## GLAD

- Generator release: v2.0.8
- Generator commit: `73db193f853e2ee079bf3ca8a64aa2eaf6459043`
- Source: https://github.com/Dav1dde/glad
- Generator license: MIT
- Generated-code SPDX expression:
  `(WTFPL OR CC0-1.0) AND Apache-2.0`
- Local generator license: `third_party/glad/LICENSE.generator.txt`
- Generation command:
  `python -m glad --api='gl:core=3.3' --extensions='' --out-path <output> --reproducible c --loader`
- Imported layout: generated `include/glad/gl.h`,
  `include/KHR/khrplatform.h`, and `src/gl.c`

## tinyobjloader

- Release: v1.0.6
- Commit: `e60d33385e2e4f7fa891513150f2532b5bbcb093`
- Source: https://github.com/tinyobjloader/tinyobjloader
- License: MIT
- Local license: `third_party/tinyobjloader/LICENSE`
- Imported layout: `tiny_obj_loader.h`, README, and license

The complete imported-file list and SHA-256 values are stored in
`third_party/manifest.lock`.

## Suzanne model

- Upstream commit: `81e8b567643b5166e6ff40024e4ff71ad4b18676`
- Source: https://github.com/KhronosGroup/glTF-Sample-Assets/tree/main/Models/Suzanne
- Copyright owner in upstream metadata: © 2017 UX3D
- Artist in upstream metadata: Norbert Nopper
- License: CC0 1.0 Universal
- Local provenance and conversion record: `assets/models/LICENSE.md`
