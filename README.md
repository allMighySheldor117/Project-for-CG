# Crystalbound

Crystalbound is a developing first-person C++17/OpenGL 3.3 cave-exploration
game. The finished game will generate a cave in which the player finds five
elemental crystals and activates an ancient exit arch.

The repository is currently at construction Step 5A. It generates and validates
a deterministic abstract cave topology and now provides tested CPU contracts
for curved route sampling, stable frames, UV-capable procedural meshes, bounds,
and geometry budgets. The bundled Suzanne model remains a temporary normal
visualization; assembling and rendering the generated cave is Step 5B.

## Seeded topology

Run with a strict unsigned decimal seed:

```powershell
.\build\step-05a\Debug\crystalbound.exe --seed 123456789
```

Run without `--seed` to choose a requested seed from operating-system entropy.
Entropy selects only that initial seed; all topology content then comes from the
specified SplitMix64 implementation and stable domain substreams.

The startup diagnostic reports the requested, attempt, and effective seeds,
generator version, fallback state, and a canonical FNV-1a-64 topology
fingerprint. Seed `123456789` has generator-version-1 fingerprint
`2f307424c0dcd1f4`.

```text
Topology generation
  Requested seed: 123456789
  Attempt seed: 123456789
  Effective seed: 123456789
  Generator version: 1
  Fingerprint: 2f307424c0dcd1f4
  Fallback: no
```

Show command-line help without opening a window:

```powershell
.\build\step-05a\Debug\crystalbound.exe --help
```

The current topology contract provides:

- one Start, five elemental chambers, one Exit, and an optional Neutral chamber;
- exactly one Fire, Water, Earth, Air, and Aether identity;
- canonical stable node IDs and normalized undirected edges;
- full connectedness and a guaranteed cycle with at least three chambers;
- integer millimetre/millidegree anchors and abstract route descriptors;
- bounded deterministic retries and a revalidated known-good fallback;
- canonical little-endian fingerprinting independent of raw floating-point mesh data.

No complete cave scene is generated or rendered by this step.

## Spline and mesh contracts

Step 5A establishes the reusable CPU boundary that Step 5B will use to build
the cave:

- integer millimetre control points remain the deterministic source of truth;
- routes use centripetal Catmull-Rom sampling with a maximum 0.50 m spacing,
  bounded chord error, overshoot, grade, and frame-turn validation;
- parallel-transport frames avoid tunnel-ring flips and use a stable fallback
  axis for near-vertical initialization;
- a checked mesh builder emits positions, unit normals, UVs, and 32-bit indices;
- ring sweeps support inward-facing cave surfaces and outward-facing props with
  consistent winding, duplicated UV seams, and no zero-area triangles;
- bounds and the locked 250,000-static-vertex budget are available to later
  scene validation; and
- canonical little-endian integer-contract fingerprints stay independent of
  platform floating-point mesh bytes.

For effective seed 123456789, the fixed Step 5A route fixture has geometry
contract fingerprint 1a7e6bdc2941a77a. This is a contract/test fixture, not yet
a player-visible tunnel.

## Current controls

| Input | Action |
| --- | --- |
| `W` / `S` | Move forward / backward |
| `A` / `D` | Move left / right |
| `Space` / `Ctrl` | Move up / down |
| `Shift` | Temporarily increase movement speed |
| Mouse | Look around while captured |
| `Esc` | Release or recapture the mouse |

Close the application with the window close button. Grounded movement replaces
the temporary free-flight development camera in a later step.

## Temporary model pipeline

The runtime loads only bundled Suzanne as its current smoke scene. The OBJ
loader remains a tested development component:

- It triangulates faces and combines all OBJ shapes.
- It respects independent position and normal indices, including hard edges.
- Complete normals are normalized; completely missing normals are generated.
- Partial normal coverage is rejected.
- Degenerate triangles are skipped with diagnostics; an entirely invalid model fails.
- Valid meshes are centered and uniformly normalized to a largest extent of 2 units.

The current shader converts normals into RGB so model and GPU-pipeline problems
remain visible before cave lighting is implemented.

## Requirements

- CMake 3.24 or newer
- Visual Studio 2022 with the Desktop development with C++ workload
- A 64-bit Windows 10 or Windows 11 environment

Linux is supported through Ninja and X11 when the documented system development
packages are already installed.

## Configure, build, test, and install

```powershell
cmake -S . -B build/step-05a -G "Visual Studio 17 2022" -A x64 -DCRYSTALBOUND_BUILD_TESTS=ON
cmake --build build/step-05a --config Debug --target crystalbound crystalbound_tests
ctest --test-dir build/step-05a -C Debug --output-on-failure
cmake --install build/step-05a --config Debug --prefix build/install-05a
```

Run the dependency-free CPU harness directly to see every named case:

```powershell
.\build\step-05a\Debug\crystalbound_tests.exe .\build\step-05a\Debug\testdata
```

Run the installed executable:

```powershell
.\build\install-05a\crystalbound.exe --seed 123456789
```

Build-tree and installed executables resolve assets and shaders relative to the
executable, not the current working directory.

## Runtime and CPU foundation

- `crystalbound_core` owns deterministic random generation, CLI seed parsing,
  topology contracts, validation, fingerprints, retry/fallback behavior, the
  movement envelope, spline/parallel-frame contracts, procedural mesh
  validation, camera logic, mesh data, and OBJ loading.
- `Application` receives one complete validated `GenerationResult`; partial
  attempts never become active.
- GLFW owns the window and events; GLAD loads OpenGL 3.3 Core.
- `GpuMesh` and `ShaderProgram` own GPU resources through ordered RAII shutdown.
- GPU objects are destroyed while the OpenGL context remains current.

## Offline dependency policy

GLFW, GLAD, GLM, Dear ImGui, and tinyobjloader are stored under `third_party/`
at immutable revisions. CMake performs no dependency downloads. Provenance,
hashes, and licenses are recorded in `third_party/manifest.lock`,
[LICENSES.md](LICENSES.md), and [assets/models/LICENSE.md](assets/models/LICENSE.md).

## Automated checks

`crystalbound_tests` runs 38 deterministic CPU cases without a window or OpenGL
context. They cover the prior mesh/camera and topology behavior plus spline
endpoints and spacing, curved-route repeatability, parallel-transport frames,
invalid route rejection, UV seams, winding and normal orientation, mesh
budgets, bounds separation, and the integer geometry-contract golden.

GitHub Actions builds, tests, and installs on Windows/Visual Studio 2022 and
Linux/Ninja. CI also rejects tracked `plans/` content and first-party CMake
downloads. GPU rendering remains a manual check.

## Current limitations

The Step 5A builders are not yet connected to the runtime scene: the abstract
graph is not assembled into chambers, tunnels, bridges, or colliders until Step
5B. Crystal collection, grounded movement, lighting, procedural materials, fog,
the exit arch, timer, and game UI are also not implemented yet.
