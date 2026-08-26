# Crystalbound

Crystalbound is a developing first-person C++17/OpenGL 3.3 cave-exploration
game. Each run builds a deterministic low-poly cave from a seed. The finished
game will ask the player to explore its branching passages, collect five
elemental crystals, and activate an ancient exit arch.

The repository is currently at construction Step 6B. It generates complete
chambers, curved tunnels, junctions, and at least one wooden bridge, validates
that the grounded player can mechanically reach every required chamber, and
only then publishes the cave for exploration. Walking, sprinting, jumping,
collision response, bridge traversal, falling, and safe-chamber respawning
remain available; the simple normal/albedo shader remains in place while visual
atmosphere and game objectives are developed later.

## Run a generated cave

Build and launch a reproducible scene:

```powershell
cmake -S . -B build/step-06b -G "Visual Studio 17 2022" -A x64 -DCRYSTALBOUND_BUILD_TESTS=ON
cmake --build build/step-06b --config Debug --target crystalbound crystalbound_tests
.\build\step-06b\Debug\crystalbound.exe --seed 42
```

Omit `--seed` to choose a requested seed from operating-system entropy. Entropy
is used only for that initial choice; the cave itself comes from the specified
SplitMix64 implementation and stable domain substreams.

Generation is atomic. A candidate must pass topology, spline, geometry,
separation, collider, budget, and mechanical-reachability checks before it can
be displayed. Rejected candidates are reported and retried deterministically,
with a fully revalidated known-good fallback after exactly eight normal
attempts. The startup diagnostic prints the requested, attempted, and effective
seeds, every attempt outcome, fallback state, canonical topology and scene
fingerprints, and the accepted mechanical report summary.

Seed `42` accepts a normal candidate and keeps the existing Step 5B scene
fingerprint:

```text
Scene fingerprint: 9fb15c446b74730d
```

Show command-line help without opening a window:

```powershell
.\build\step-06b\Debug\crystalbound.exe --help
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
| `Shift` | Sprint while held |
| `Space` | Jump while grounded |
| Mouse | Look around while captured |
| `Esc` | Release or recapture the mouse |

Close the application with the window close button. The player starts grounded
in the Start chamber facing one of its portals. Looking up or down never changes
the ground movement direction, and there is no public vertical free-flight
mode.

## Rendering and geometry pipeline

- OpenGL 3.3 Core renders indexed meshes through VAOs and VBOs, with depth
  testing and back-face culling enabled.
- The debug shader combines per-piece placeholder albedo with encoded surface
  normals so winding, seams, and surface orientation remain easy to inspect.
- CPU scene construction is independent of GLFW and OpenGL, so generation and
  validation run in the test harness without a window or graphics context.
- A CPU-only collision world is derived from the same canonical chamber and
  route contracts as the rendered scene. Analytic chamber regions and sampled
  tunnel or bridge corridors remain separate from rendering triangles.
- A stable directed traversal graph is derived from that collision world.
  Topology alone never makes a route traversable: the validator checks each
  direction independently using exact integer clearance, slope, step, gap,
  landing, and seam measurements.
- Start must mechanically reach all five elemental chambers, Exit, and Neutral
  whenever it exists. Every guaranteed-loop route and wooden bridge must be
  usable in both directions, preserving two alternatives around the loop.
- Each chamber respawn is checked for finite capsule clearance, grounded support,
  fall-region and kill-plane safety, stable repeated queries, and its intended
  stable chamber identity.
- The controller advances at a fixed 120 Hz, clamps long frame deltas, limits
  catch-up work, and uses conservative movement substeps for deterministic wall
  collision and sliding.
- The physical capsule owns player position and vertical velocity. The camera
  follows at the locked eye height while retaining independent mouse yaw and
  pitch.
- Entering a chamber updates a safe checkpoint. Fall regions and the cave kill
  plane return the player to that checkpoint with unsafe velocity cleared.
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
cmake -S . -B build/step-06b -G "Visual Studio 17 2022" -A x64 -DCRYSTALBOUND_BUILD_TESTS=ON
cmake --build build/step-06b --config Debug --target crystalbound crystalbound_tests
ctest --test-dir build/step-06b -C Debug --output-on-failure
cmake --install build/step-06b --config Debug --prefix build/install-06b-debug
cmake --build build/step-06b --config Release --target crystalbound crystalbound_tests
ctest --test-dir build/step-06b -C Release --output-on-failure
cmake --install build/step-06b --config Release --prefix build/install-06b
```

Run the dependency-free CPU harness directly to see every named case:

```powershell
.\build\step-06b\Debug\crystalbound_tests.exe .\build\step-06b\Debug\testdata
```

Run the installed executable from any working directory:

```powershell
.\build\install-06b\crystalbound.exe --seed 42
```

Build-tree and installed executables resolve shaders and other runtime resources
relative to the executable, not the current working directory.

## Offline dependency policy

GLFW, GLAD, GLM, Dear ImGui, and tinyobjloader are stored under `third_party/`
at immutable revisions. CMake performs no dependency downloads. Provenance,
hashes, and licenses are recorded in `third_party/manifest.lock`,
[LICENSES.md](LICENSES.md), and [assets/models/LICENSE.md](assets/models/LICENSE.md).

## Automated checks

`crystalbound_tests` runs 95 deterministic CPU cases without a window or OpenGL
context. The original 63 cases remain. The 32 Step 6B cases add successful
reports, stable directed graphs, required and Neutral reachability, loop
alternatives, bidirectional routes and bridges, seam failures, safe respawns,
exact movement-envelope boundaries, deterministic typed diagnostics, mechanical
retry and fallback behavior, atomic invalid-fallback failure, repeated complete
acceptance, and a bounded five-seed corpus.

Seed `42` is the normal-acceptance example. Seed `123456789` is the checked
fallback example: its eight normal candidates are rejected by existing geometry
contracts before the fallback independently passes topology, geometry,
collision, and mechanical validation. The CI corpus is intentionally bounded to
`1`, `2`, `3`, `42`, and `123456789`; exhaustive procedural soak testing
belongs to a later construction step.

GitHub Actions builds, tests, and installs on Windows/Visual Studio 2022 and
Linux/Ninja. CI also rejects tracked `plans/` content and first-party CMake
downloads. GPU rendering remains a manual check.

## Current limitations

Step 6B provides mechanically validated cave navigation, not the complete game
loop. Crystal collection, elemental art, Phong lighting, procedural materials,
fog, the exit arch, timer, game-state screens, sound, and maps remain
intentionally out of scope.
