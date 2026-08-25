# Suzanne asset provenance

`suzanne.obj` is a deterministic format conversion of the Suzanne sample in
KhronosGroup/glTF-Sample-Assets.

- Upstream repository: https://github.com/KhronosGroup/glTF-Sample-Assets
- Upstream model: `Models/Suzanne`
- Pinned upstream commit: `81e8b567643b5166e6ff40024e4ff71ad4b18676`
- Copyright owner in upstream metadata: © 2017 UX3D
- Artist in upstream metadata: Norbert Nopper
- License: CC0 1.0 Universal
- Upstream license notice:
  https://github.com/KhronosGroup/glTF-Sample-Assets/blob/81e8b567643b5166e6ff40024e4ff71ad4b18676/Models/Suzanne/LICENSE.md
- CC0 legal code: https://creativecommons.org/publicdomain/zero/1.0/legalcode

The upstream license notice dedicates the model-associated text, image, and
binary files to the public domain under CC0 1.0. Attribution is included here
for traceability even though CC0 does not require it.

## Reproducibility record

- Retrieved and converted: 2026-08-25
- Source `Suzanne.gltf` SHA-256:
  `7e8ae013010aff530162ef2795cec74c2646019e224af17bcfb691664f0f0aec`
- Source `Suzanne.bin` SHA-256:
  `b85c2727aa41318e00673d8892f5879d46fb6e476e280f28ee1febd07602b6b8`
- Output `suzanne.obj` SHA-256:
  `6f7dd2d67e655582fc477a2331242c80b8a54a9d5c0875ae0cdb7c6323131c0b`

The conversion retained the glTF position, normal, triangle-index, and winding
data; converted indices to OBJ's one-based `position//normal` form; and omitted
tangents, texture coordinates, material bindings, and textures. The resulting
OBJ contains 11,808 positions, 11,808 normals, and 3,936 triangles.
