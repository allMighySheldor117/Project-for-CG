# Real-Time Non-Photorealistic Renderer

An interactive C++17/OpenGL project for comparing Phong illumination, cel
shading, and cel shading with silhouette outlines.

The repository is currently at Blueprint Step 1: the offline build foundation.
It compiles and links all required libraries but intentionally does not open a
window or render a scene yet.

## Requirements

- CMake 3.24 or newer
- Visual Studio 2022 with the Desktop development with C++ workload
- A 64-bit Windows 10 or Windows 11 environment

Linux is supported by the CMake structure. Its GLFW build uses X11 and requires
the corresponding system development packages to already be installed.

## Configure and build on Windows

```powershell
cmake -S . -B build/step-01 -G "Visual Studio 17 2022" -A x64 -DNPR_BUILD_TESTS=OFF
cmake --build build/step-01 --config Debug
cmake --install build/step-01 --config Debug --prefix build/install-01
```

Run the build-tree executable:

```powershell
.\build\step-01\Debug\npr_renderer.exe
```

Run the installed executable:

```powershell
.\build\install-01\npr_renderer.exe
```

## Offline dependency policy

GLFW, GLAD, GLM, Dear ImGui, and tinyobjloader are stored under
`third_party/` at immutable revisions. CMake performs no dependency downloads,
and the imported files are recorded with SHA-256 values in
`third_party/manifest.lock`. See [LICENSES.md](LICENSES.md) for provenance and
license details.
