# Crystalbound

Crystalbound is a developing first-person C++17/OpenGL 3.3 cave-exploration
game. The finished game will generate a cave in which the player finds five
elemental crystals and activates an ancient exit arch.

The repository is currently at construction Step 4A. The reusable graphics
foundation opens an OpenGL window, provides a first-person development camera,
loads and validates OBJ meshes, and visualizes normals. The bundled Suzanne
model remains a temporary development smoke scene while cave generation is
built in later steps.

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
.\build\step-04a\Debug\crystalbound.exe C:\models\example.obj
~~~

Show the currently implemented command-line interface without opening a window:

```powershell
.\build\step-04a\Debug\crystalbound.exe --help
```

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
cmake -S . -B build/step-04a -G "Visual Studio 17 2022" -A x64 -DCRYSTALBOUND_BUILD_TESTS=ON
cmake --build build/step-04a --config Debug --target crystalbound crystalbound_tests
ctest --test-dir build/step-04a -C Debug --output-on-failure
cmake --install build/step-04a --config Debug --prefix build/install-04a
```

Run the build-tree executable:

```powershell
.\build\step-04a\Debug\crystalbound.exe
```

Run the installed executable:

```powershell
.\build\install-04a\crystalbound.exe
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
- `crystalbound_core` owns the CPU-only camera, mesh representation, OBJ parsing,
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

## Automated checks

`crystalbound_tests` exercises the CPU mesh and OBJ-loading policies without
creating a window or OpenGL context. GitHub Actions builds and runs these tests
on Windows and Linux. GPU rendering remains a manual check because CI runners
do not provide a representative interactive graphics environment.

## Current limitations

This is still the technical foundation, not the complete game. It does not yet
include seeded cave generation, grounded collision, cave lighting, procedural
materials, elemental crystals, the exit arch, or the final game UI.
