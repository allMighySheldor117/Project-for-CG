# Crystalbound

Crystalbound is a first-person C++17/OpenGL 3.3 cave-exploration game. Explore
seven authored chambers connected by a deterministic linear cave route,
collect the five glowing elemental crystals in any order, then activate the
ancient exit arch to stop the timer and escape.

The repository is currently at construction Step 10. The complete in-memory
game loop now sits behind a deterministic complete-cave corpus, repeatability
checks, Release profiling, and offline install verification. Start, Playing,
Paused, and Completed states; a monotonic run timer; same-seed restart;
new-cave generation; per-seed session best times; and the minimal Dear ImGui
interface remain unchanged. No map or save file is used.

## Run a generated cave

Build and launch a reproducible scene:

```powershell
cmake -S . -B build/step-10 -G "Visual Studio 17 2022" -A x64 -DCRYSTALBOUND_BUILD_TESTS=ON
cmake --build build/step-10 --config Debug --target crystalbound crystalbound_tests crystalbound_seed_corpus
.\build\step-10\Debug\crystalbound.exe --seed 42
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

Seed `42` accepts the generator-version 3 authored scene with this canonical
fingerprint:

```text
Scene fingerprint: 1f8517f2c8d6c15a
```

Show command-line help without opening a window:

```powershell
.\build\step-10\Debug\crystalbound.exe --help
```

## Current scene

The generated cave provides:

- one Start, five elemental chambers, and one Exit chamber;
- a fixed one-way progression order with six level tunnel connections and no
  bridges;
- integer millimetre source contracts and deterministic per-object variation;
- authored chamber meshes with fixed entrance sockets and deterministic
  placement;
- level tunnel meshes and junction geometry joining every chamber entrance
  without traversal gaps;
- one Fire, Water, Earth, Air, and Aether crystal, each with a different
  low-poly silhouette, color, glow rhythm, pedestal, and smaller future socket
  variant;
- a low-poly stone exit arch with five elemental sockets and a violet portal
  that activates only after all five crystals have been collected;
- any-order crystal collection within 2.2 m and a 12-degree camera cone, with
  deterministic CPU structural line-of-sight testing and stable target
  tie-breaks by angle, distance, then object ID;
- Fire lava rocks, glowing cracks, and sparks; Water pools and cool mist;
  Earth pillars and stalagmites; Air wood spires, wind ribbons, and motes; and
  Aether arch stones, an orbiting rock, and violet haze;
- separate chamber-floor, chamber-boundary, tunnel, authored-object, and
  fall-region collider records; and
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
| `E` | Collect a focused crystal, or activate the completed exit arch |
| `Esc` | Pause and release the mouse |

Begin Exploration starts the timer and captures the mouse. The player starts
grounded in the Start chamber facing one of its portals. Looking up or down
never changes the ground movement direction, and there is no public vertical
free-flight mode. Losing focus or minimizing the window pauses without counting
hidden time.

A short prompt appears only when an interaction satisfies the range, focus, and
structural line-of-sight rules. `E` is rising-edge only, so holding it never
repeats an action. The normal HUD shows only crystal progress and elapsed time;
seed details appear on Start, Pause, and Completion screens. Collection,
elapsed time, and best times are kept only for the current process.

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
  active crystal slots, and at most two decorative lights. Candidate selection
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
- Opaque, emissive, premultiplied-alpha, additive, and UI pass contracts
  explicitly establish framebuffer gamma, depth testing/writes, back-face
  culling, and blending rather than depending on leftover OpenGL state.
- Transparent water and fog are sorted back-to-front with stable-ID ties;
  sparks and motes use a separate additive pass. All effect geometry is purely
  visual and never enters the collision or reachability contracts.
- CPU scene construction is independent of GLFW and OpenGL, so generation and
  validation run in the test harness without a window or graphics context.
- A CPU-only collision world is derived from the same canonical chamber and
  route contracts as the rendered scene. Analytic chamber regions and sampled
  tunnel or bridge corridors remain separate from rendering triangles.
- Crystal interaction is also CPU-only. Dedicated, stably ordered wall and
  ceiling visibility triangles are derived from chamber geometry contracts and
  portal openings. Broad chamber bounds, OpenGL depth reads, cosmetic meshes,
  and draw order are never treated as line-of-sight truth.
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
cmake -S . -B build/step-10 -G "Visual Studio 17 2022" -A x64 -DCRYSTALBOUND_BUILD_TESTS=ON
cmake --build build/step-10 --config Debug --target crystalbound crystalbound_tests crystalbound_seed_corpus
ctest --test-dir build/step-10 -C Debug --output-on-failure
cmake --install build/step-10 --config Debug --prefix build/install-step10-debug
cmake --build build/step-10 --config Release --target crystalbound crystalbound_tests crystalbound_seed_corpus
ctest --test-dir build/step-10 -C Release --output-on-failure
cmake --install build/step-10 --config Release --prefix build/install-step10-release
```

Run the dependency-free CPU harness directly to see every named case:

```powershell
.\build\step-10\Debug\crystalbound_tests.exe .\tests\testdata
```

Run the installed executable from any working directory:

```powershell
.\build\install-step10-release\crystalbound.exe --seed 42
```

Build-tree and installed executables resolve shaders and other runtime resources
relative to the executable, not the current working directory.

## Offline dependency policy

GLFW, GLAD, GLM, Dear ImGui, and tinyobjloader are stored under `third_party/`
at immutable revisions. CMake performs no dependency downloads. Provenance,
hashes, and licenses are recorded in `third_party/manifest.lock`,
[LICENSES.md](LICENSES.md), and [assets/models/LICENSE.md](assets/models/LICENSE.md).

## Automated checks

`crystalbound_tests` runs 168 deterministic CPU cases without a window, OpenGL
context, network, timing sleeps, or screenshot comparison. All 147 tests through
Step 8B remain. The Step 7 cases lock texture dimensions/formats/sampling, fixed-point
rounding and fade behavior, periodic noise, octave weights, anisotropy, palette
math, byte repeatability and goldens, material assignment, triplanar weights,
light reservations/selection/ties, finite validation, fog boundaries,
render-pass/gamma state, and the accepted/fallback seed contracts. The 16 Step
8A cases lock elemental identities, procedural mesh validity, socket scale,
decoration substreams, fixed-time animation, crystal lights, transparent
ordering, budgets, render-pass policy, and isolation from structural collision
and fingerprint contracts. The 11 Step 8B cases exhaust all 120 collection
orders and cover one-time state, socket readiness, exact range and focus
boundaries, target tie-breaks, structural occlusion, invalid queries, E rising
edges, renderer/light visibility, respawn preservation, and unchanged
fingerprints, collision, reachability, and accepted/fallback determinism.
The 16 Step 9 cases cover all state transitions and invalid transitions,
monotonic pause-aware timing, session-best isolation, reset rules, injected
new-seed selection, UI field visibility, all 120 collection orders, exact arch
interaction boundaries, structural occlusion, and unchanged scene fingerprints.

Seed `42` is the normal-acceptance example. Seed `123456789` is the checked
fallback example: its eight normal candidates are rejected by existing geometry
contracts before the fallback independently passes topology, geometry,
collision, and mechanical validation.

The CPU-only `crystalbound_seed_corpus` executable validates each requested seed
twice through topology, geometry, mesh, budget, collision, reachability,
bridge, elemental, crystal-collection, exit-arch, finite-value, fingerprint,
and repeatability contracts. Stable selection uses requested seeds `0` through
`N-2`, followed by `123456789`; this includes seed `42` whenever `N >= 44`.
CI runs exactly 256 requested seeds (`0..254` plus `123456789`). The larger
10,000-seed Release audit is opt-in and is not part of CTest or GitHub Actions:

```powershell
.\build\step-10\Release\crystalbound_seed_corpus.exe --count 256
.\build\step-10\Release\crystalbound_seed_corpus.exe --count 10000
```

The Step 10 local Release audit passed all 10,000 requested seeds and 20,000
complete generated results in 126.927 seconds: 923 normal acceptances and 9,077
independently validated fallbacks. The compact manual/regression set and its
measured room, route, elevation, acceptance, and fingerprint metadata are in
[tests/testdata/showcase-seeds.md](tests/testdata/showcase-seeds.md).

GitHub Actions builds, tests, installs, and runs the exact 256-seed corpus on
Windows/Visual Studio 2022 and Linux/Ninja. CI also rejects tracked `plans/`
content and first-party CMake downloads. GPU rendering remains a manual check.

## Verified performance

Step 10 was measured in Release at 1280x720 with seed `42`, after a five-second
warm-up, on an Intel Core i7-1165G7 (4 cores/8 threads) and Intel Iris Xe using
OpenGL 3.3 driver `30.0.101.3111`. The original five-minute VSync-on baseline
measured 15,315 frames at 20.678 ms median and 26.235 ms p95. Skipping point
lights outside their exact zero-contribution range and avoiding redundant
material-kind uniform updates reduced the five-minute uncapped measurement to
8.966 ms median and 21.583 ms p95 across 24,323 frames. The median target of
16.7 ms passed; the 20 ms p95 target missed by 1.583 ms on this machine.

The profiler is opt-in, prints only to the console, closes automatically, and
does not change the normal HUD or default VSync-on gameplay:

```powershell
.\build\step-10\Release\crystalbound.exe --seed 42 --profile-seconds 300 --profile-no-vsync
```

## Current limitations

Crystalbound currently has one difficulty and no persistent save, map, sound,
shadow mapping, bloom, or post-processing. The interface is intentionally
minimal, and best times last only for the current application session.

The current renderer intentionally has no shadow mapping. A local point light can therefore
leak through a thin wall; the lantern's bounded range and attenuation limit the
artifact but cannot eliminate it.

Normal-candidate geometry acceptance is also currently low: 9,077 of the
10,000 audited requested seeds used the same validated fallback scene. Safety,
atomic activation, and determinism remain intact, but procedural scene variety
should be improved in a future generator-contract revision rather than by
weakening Step 10 validation.
