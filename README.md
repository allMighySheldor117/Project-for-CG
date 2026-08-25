# Real-Time Non-Photorealistic Renderer

An interactive C++17/OpenGL 3.3 Core project for comparing Phong illumination,
cel shading, and cel shading with silhouette outlines.

The repository is currently at Blueprint Step 3. It loads a real OBJ mesh,
normalizes it into a predictable viewing volume, uploads indexed geometry to
OpenGL, and visualizes mesh normals while retaining the reusable first-person
camera. The bundled default is the Suzanne model.

## Current controls

| Input | Action |
| --- | --- |
| `W` / `S` | Move forward / backward |
| `A` / `D` | Move left / right |
| `Space` / `Ctrl` | Move up / down |
| `Shift` | Temporarily increase movement speed |
| Mouse | Look around while captured |
| `Esc` | Release or recapture the mouse |

Close the application with the window close button. Releasing the mouse with
`Esc` is reserved for the user interface added in a later Blueprint step.

## Model input and loader policy

Run with no arguments to use bundled Suzanne, or pass one optional OBJ path:

~~~powershell
.\build\step-03\Debug\npr_renderer.exe C:\models\example.obj
~~~

If a custom path cannot be opened or validated, the application reports the
path and reason, then falls back to bundled Suzanne. A missing or invalid
bundled asset remains a fatal startup error.

The CPU loader deliberately has a narrow, deterministic contract:

- It triangulates faces and combines all OBJ shapes.
- It respects independent position and normal index streams, including hard
  edges represented by different normal indices at a shared position.
- Complete normals are validated and normalized. If every corner lacks a
  normal, area-weighted vertex normals are generated. Partial normal coverage
  is rejected as ambiguous.
- Degenerate triangles are skipped with a warning; a model with no valid
  triangle fails.
- The mesh is centered at its axis-aligned bounding-box center and uniformly
  scaled so its largest extent is 2 units. Zero-extent models fail.
- Texture coordinates, materials, smoothing groups, and MTL lookup are ignored
  in this step. The loader never repairs winding.

The current shader maps each unit normal from negative-one-to-one into an RGB
color. This makes seams, inverted normals, and generated-normal problems
visible before lighting is introduced.

## Requirements

- CMake 3.24 or newer
- Visual Studio 2022 with the Desktop development with C++ workload
- A 64-bit Windows 10 or Windows 11 environment

Linux is supported by the CMake structure. Its GLFW build uses X11 and requires
the corresponding system development packages to already be installed.

## Configure, build, and install on Windows

```powershell
cmake -S . -B build/step-03 -G "Visual Studio 17 2022" -A x64 -DNPR_BUILD_TESTS=OFF
cmake --build build/step-03 --config Debug
cmake --install build/step-03 --config Debug --prefix build/install-03
```

Run the build-tree executable:

```powershell
.\build\step-03\Debug\npr_renderer.exe
```

Run the installed executable:

```powershell
.\build\install-03\npr_renderer.exe
```

Both executables resolve `resources/assets` and `resources/shaders` relative to
their own executable location. They do not depend on the repository or current
working directory at runtime.

## Runtime foundation

- GLFW owns the OpenGL window and event processing.
- GLAD loads the OpenGL 3.3 Core API after the context becomes current.
- `Application` owns initialization, input routing, the frame loop, and ordered
  shutdown.
- `Camera` provides delta-time movement, mouse look, and clamped pitch.
- `npr_core` owns the CPU-only camera, mesh representation, OBJ parsing,
  validation, normal generation, and normalization logic.
- `GpuMesh` owns one VAO, VBO, and element buffer for indexed drawing.
  Each interleaved vertex is 24 bytes: position starts at byte 0, normal starts
  at byte 12, and the element buffer stores 32-bit unsigned indices.
- `ShaderProgram` loads separate shader files and reports complete compile or
  link diagnostics.
- GPU objects are destroyed while the OpenGL context is current and before the
  GLFW window is destroyed.

At startup, the program prints the OpenGL vendor, renderer, OpenGL version, and
GLSL version so context creation can be audited.

## Offline dependency policy

GLFW, GLAD, GLM, Dear ImGui, and tinyobjloader are stored under
`third_party/` at immutable revisions. CMake performs no dependency downloads,
and the imported files are recorded with SHA-256 values in
`third_party/manifest.lock`. See [LICENSES.md](LICENSES.md) for provenance and
license details.

Bundled Suzanne is a deterministic OBJ conversion of the CC0 Khronos glTF
sample at a pinned commit. Its source hashes, output hash, author metadata,
license links, and exact conversion policy are recorded in
[assets/models/LICENSE.md](assets/models/LICENSE.md).

## Current limitations

This step intentionally does not include Phong lighting, cel shading,
silhouette outlines, ImGui behavior, or automated tests. The OBJ fixtures under
[tests/testdata](tests/testdata) document the cases introduced now and become
executable tests in Blueprint Step 4.
