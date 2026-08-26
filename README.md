# Crystalbound

Crystalbound is a developing first-person C++17/OpenGL 3.3 cave-exploration
game. Each run builds a deterministic low-poly cave from a seed. The finished
game will ask the player to explore its branching passages, collect five
elemental crystals, and activate an ancient exit arch.

The repository is currently at construction Step 7. It generates and
mechanically validates complete chambers, curved tunnels, junctions, and at
least one wooden bridge before publishing the cave for exploration. The cave
now has its shared visual foundation: Phong lighting, a warm camera lantern,
deterministic procedural rock and wood, triplanar cave mapping, directional
bridge grain, and distance fog. Walking, sprinting, jumping, collision response,
bridge traversal, falling, and safe-chamber respawning remain unchanged.

## Run a generated cave

Build and launch a reproducible scene:

```powershell
cmake -S . -B build/step-07 -G "Visual Studio 17 2022" -A x64 -DCRYSTALBOUND_BUILD_TESTS=ON
cmake --build build/step-07 --config Debug --target crystalbound crystalbound_tests
.\build\step-07\Debug\crystalbound.exe --seed 42
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
.\build\step-07\Debug\crystalbound.exe --help
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

- OpenGL 3.3 Core renders indexed meshes through VAOs and VBOs. Readable GLSL
  330 shaders transform positions and normals into world space with an explicit
  model matrix and inverse-transpose normal matrix.
- Phong illumination combines low ambient light with diffuse and specular
  response, material shininess, emission capacity, point-light color and
  intensity, local attenuation, and a smooth range cutoff.
- An always-on warm point light follows the camera. It has no fuel or gameplay
  state, and its 10.5 m cutoff keeps nearby paths readable without illuminating
  the entire cave.
- The point-light policy is capped at eight slots: one reserved lantern, five
  future crystal slots, and at most two decorative lights. Candidate selection
  is deterministic: a relevant chamber crystal is reserved, then distance and
  stable object ID decide the remaining order.
- Rock is generated on the CPU as a periodic 128x128 linear `R8` texture. Four
  value-noise octaves use lattice sizes 8/16/32/64 and weights 8/4/2/1.
  World-space triplanar sampling blends the three projections from the absolute
  surface normal with a named sharpness, avoiding the cave mesh's UV stretching.
- Wood is generated as a periodic 128x128 `SRGB8` texture. Three octaves use
  sizes 8/16/32 and weights 4/2/1, four-times anisotropic integer coordinates,
  a 32-pixel integer triangle wave, and a fixed dark/light palette. Bridge UVs
  keep the grain aligned with the plank geometry instead of using triplanar rock.
- Both textures use only SplitMix64, signed 64-bit and 16.16 fixed-point math,
  explicit round-half-up interpolation, stable row-major output, and repeat plus
  linear filtering. No floating-point operation affects their bytes.
- Texture byte fingerprints for seed `42` and the locked material object IDs are
  `c960c475acbb3b70` (rock) and `746a0f29bf1661ed` (wood).
- Distance fog uses finite deterministic per-zone ranges and blends toward a
  dark cave color in the opaque shader. It uses ordinary depth-tested geometry,
  so it does not reveal surfaces through walls or affect collision/reachability.
- The default framebuffer is requested and verified as sRGB. Shader lighting is
  linear, wood is sampled from an sRGB texture, scalar rock noise remains linear,
  and `GL_FRAMEBUFFER_SRGB` performs final display encoding.
- Opaque and UI pass contracts explicitly establish framebuffer gamma, depth
  testing/writes, back-face culling, and blending rather than depending on
  leftover OpenGL state.
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
- RAII wrappers own programs, VAOs, buffers, and textures. Procedural bytes,
  shaders, static meshes, and GPU uploads are created once after the context is
  valid and destroyed before GLFW removes it; the frame loop only updates small
  camera, light, fog, transform, and material uniforms. Uniform locations are
  cached per shader program after their first validated lookup.

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
cmake -S . -B build/step-07 -G "Visual Studio 17 2022" -A x64 -DCRYSTALBOUND_BUILD_TESTS=ON
cmake --build build/step-07 --config Debug --target crystalbound crystalbound_tests
ctest --test-dir build/step-07 -C Debug --output-on-failure
cmake --install build/step-07 --config Debug --prefix build/install-07-debug
cmake --build build/step-07 --config Release --target crystalbound crystalbound_tests
ctest --test-dir build/step-07 -C Release --output-on-failure
cmake --install build/step-07 --config Release --prefix build/install-07
```

Run the dependency-free CPU harness directly to see every named case:

```powershell
.\build\step-07\Debug\crystalbound_tests.exe .\build\step-07\Debug\testdata
```

Run the installed executable from any working directory:

```powershell
.\build\install-07\crystalbound.exe --seed 42
```

Build-tree and installed executables resolve shaders and other runtime resources
relative to the executable, not the current working directory.

## Offline dependency policy

GLFW, GLAD, GLM, Dear ImGui, and tinyobjloader are stored under `third_party/`
at immutable revisions. CMake performs no dependency downloads. Provenance,
hashes, and licenses are recorded in `third_party/manifest.lock`,
[LICENSES.md](LICENSES.md), and [assets/models/LICENSE.md](assets/models/LICENSE.md).

## Automated checks

`crystalbound_tests` runs 120 deterministic CPU cases without a window, OpenGL
context, network, timing sleeps, or screenshot comparison. All 95 Step 6B cases
remain. The 25 Step 7 cases lock texture dimensions/formats/sampling, fixed-point
rounding and fade behavior, periodic noise, octave weights, anisotropy, palette
math, byte repeatability and goldens, material assignment, triplanar weights,
light reservations/selection/ties, finite validation, fog boundaries,
render-pass/gamma state, and the accepted/fallback seed contracts.

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

Step 7 provides a lit, mechanically validated cave, not the complete game loop.
Crystal meshes and elemental chamber dressing, collection, the exit arch,
timer, game-state UI, water or fog ribbons, particles, shadows, bloom,
post-processing, sound, and maps remain intentionally out of scope for Step 8
and later work.

Step 7 intentionally has no shadow mapping. A local point light can therefore
leak through a thin wall; the lantern's bounded range and attenuation limit the
artifact but cannot eliminate it.
