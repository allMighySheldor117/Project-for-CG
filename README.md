# Real-Time Non-Photorealistic Renderer

An interactive C++17/OpenGL 3.3 Core project for comparing Phong illumination,
cel shading, and cel shading with silhouette outlines.

The repository is currently at Blueprint Step 2. It opens a verified OpenGL
3.3 Core window, renders a colored triangle from file-based GLSL shaders, and
provides a reusable first-person camera. The triangle is a runtime smoke test;
the model pipeline and NPR rendering modes are introduced in later steps.

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

## Requirements

- CMake 3.24 or newer
- Visual Studio 2022 with the Desktop development with C++ workload
- A 64-bit Windows 10 or Windows 11 environment

Linux is supported by the CMake structure. Its GLFW build uses X11 and requires
the corresponding system development packages to already be installed.

## Configure, build, and install on Windows

```powershell
cmake -S . -B build/step-02 -G "Visual Studio 17 2022" -A x64 -DNPR_BUILD_TESTS=OFF
cmake --build build/step-02 --config Debug
cmake --install build/step-02 --config Debug --prefix build/install-02
```

Run the build-tree executable:

```powershell
.\build\step-02\Debug\npr_renderer.exe
```

Run the installed executable:

```powershell
.\build\install-02\npr_renderer.exe
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

## Current limitations

This step intentionally does not include OBJ loading, Suzanne, Phong lighting,
cel shading, silhouette outlines, ImGui behavior, or automated GPU tests.
