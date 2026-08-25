# Crystalbound

Crystalbound is a developing first-person C++17/OpenGL 3.3 cave-exploration
game. Each run builds a deterministic low-poly cave from a seed. The finished
game will ask the player to explore its branching passages, collect five
elemental crystals, and activate an ancient exit arch.

The repository is currently at construction Step 5B. It generates complete
chambers, curved tunnels, junctions, and at least one wooden bridge, validates
the scene before it becomes active, uploads the accepted static meshes once,
and renders them with a simple normal/albedo debug shader. The current camera is
still a free-flight development camera; grounded movement and gameplay arrive
in later steps.

## Run a generated cave

Build and launch a reproducible scene:

```powershell
cmake -S . -B build/step-05b -G "Visual Studio 17 2022" -A x64 -DCRYSTALBOUND_BUILD_TESTS=ON
cmake --build build/step-05b --config Debug --target crystalbound crystalbound_tests
.\build\step-05b\Debug\crystalbound.exe --seed 42
```

Omit `--seed` to choose a requested seed from operating-system entropy. Entropy
is used only for that initial choice; the cave itself comes from the specified
SplitMix64 implementation and stable domain substreams.

Generation is atomic. A candidate must pass topology, spline, geometry,
separation, collider, and budget checks before it can be displayed. Rejected
candidates are reported and retried deterministically, with a revalidated
known-good fallback after eight normal attempts. The startup diagnostic prints
the requested, attempted, and effective seeds, every attempt outcome, fallback
state, and canonical topology and scene fingerprints.

Seed `42` currently accepts a normal candidate and produces this Step 5B scene
fingerprint:

```text
Scene fingerprint: 9fb15c446b74730d
```

Show command-line help without opening a window:

```powershell
.\build\step-05b\Debug\crystalbound.exe --help
```

## Current scene

The generated cave provides:

- one Start, five elemental chambers, one Exit, and sometimes one Neutral
  chamber;
- a connected route graph with a guaranteed loop and optional alternate path;
- integer millimetre source contracts and deterministic per-object variation;
- faceted chamber floors and inward-facing shells with explicit portal gaps;
- curved Catmull-Rom tunnel sweeps with stable parallel-transport frames;
- junction geometry joining every chamber portal to its route;
- one deterministic bridge route with a walkable wooden deck and rails;
- separate chamber-floor, chamber-boundary, tunnel, bridge-deck, bridge-rail,
  and fall-region collider records; and
- canonical scene fingerprints derived from integer contracts rather than raw
  floating-point mesh bytes.

The scene stays inside locked limits of 250,000 static vertices and 200 opaque
draw calls. The fixed-seed debug suite also verifies complete portal/route seams,
valid indices and normals, non-degenerate triangles, inward cave winding,
outward prop winding, traversal clearances, and spatial separation.

## Current controls

| Input | Action |
| --- | --- |
| `W` / `S` | Move forward / backward |
| `A` / `D` | Move left / right |
| `Space` / `Ctrl` | Move up / down |
| `Shift` | Temporarily increase movement speed |
| Mouse | Look around while captured |
| `Esc` | Release or recapture the mouse |

Close the application with the window close button. The camera starts in the
Start chamber facing one of its portals.

## Rendering and geometry pipeline

- OpenGL 3.3 Core renders indexed meshes through VAOs and VBOs, with depth
  testing and back-face culling enabled.
- The debug shader combines per-piece placeholder albedo with encoded surface
  normals so winding, seams, and surface orientation remain easy to inspect.
- CPU scene construction is independent of GLFW and OpenGL, so generation and
  validation run in the test harness without a window or graphics context.
- Centripetal Catmull-Rom sampling enforces maximum spacing, chord error,
  overshoot, grade, and curvature constraints.
- The checked mesh builder emits positions, unit normals, UV-ready coordinates,
  and 32-bit indices for both inward-facing cave surfaces and outward-facing
  props.
- `Application` receives one complete `CaveGenerationResult`; rejected or
  partial candidates never reach the renderer.
- RAII wrappers destroy GPU resources while the OpenGL context is still current.

The earlier OBJ loader and bundled Suzanne asset remain available as tested
development infrastructure, but the runtime now displays the generated cave.

## Requirements

- CMake 3.24 or newer
- Visual Studio 2022 with the Desktop development with C++ workload
- 64-bit Windows 10 or Windows 11

Linux is supported through Ninja and X11 when the documented system development
packages are already installed.

## Build, test, and install

```powershell
cmake -S . -B build/step-05b -G "Visual Studio 17 2022" -A x64 -DCRYSTALBOUND_BUILD_TESTS=ON
cmake --build build/step-05b --config Debug --target crystalbound crystalbound_tests
ctest --test-dir build/step-05b -C Debug --output-on-failure
cmake --build build/step-05b --config Release --target crystalbound crystalbound_tests
ctest --test-dir build/step-05b -C Release --output-on-failure
cmake --install build/step-05b --config Release --prefix build/install-05b
```

Run the dependency-free CPU harness directly to see every named case:

```powershell
.\build\step-05b\Debug\crystalbound_tests.exe .\build\step-05b\Debug\testdata
```

Run the installed executable from any working directory:

```powershell
.\build\install-05b\crystalbound.exe --seed 42
```

Build-tree and installed executables resolve shaders and other runtime resources
relative to the executable, not the current working directory.

## Offline dependency policy

GLFW, GLAD, GLM, Dear ImGui, and tinyobjloader are stored under `third_party/`
at immutable revisions. CMake performs no dependency downloads. Provenance,
hashes, and licenses are recorded in `third_party/manifest.lock`,
[LICENSES.md](LICENSES.md), and [assets/models/LICENSE.md](assets/models/LICENSE.md).

## Automated checks

`crystalbound_tests` runs 48 deterministic CPU cases without a window or OpenGL
context. They cover the earlier mesh, camera, topology, spline, and frame
contracts plus complete scene generation, repeatability and variation, the
accepted-scene golden fingerprint, bridge and collider guarantees, portal seams,
geometry budgets, validation failures, checked fallback behavior, and atomic
failure when even fallback geometry is invalid.

GitHub Actions builds, tests, and installs on Windows/Visual Studio 2022 and
Linux/Ninja. CI also rejects tracked `plans/` content and first-party CMake
downloads. GPU rendering remains a manual check.

## Current limitations

Step 5B is a geometry and debug-rendering milestone, not a playable build.
Grounded collision response, jumping, crystal collection, elemental art,
Phong lighting, procedural materials, fog, the exit arch, timer, and game UI
are intentionally not implemented yet.
