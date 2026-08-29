# Crystalbound

Crystalbound is a first-person cave exploration game written in C++17 with
OpenGL 3.3. You begin in an underground chamber and follow a connected route
through five elemental areas: Fire, Air, Earth, Water, and Aether.

Each elemental chamber contains one glowing crystal. Collect all five, reach
the exit chamber, and press `E` at the active portal to finish the run. A timer
tracks how long the escape takes.

Five maze rooms separate the main chambers. Their room shells stay fixed, but
the maze layout changes with the game seed. Using the same seed recreates the
same generated maze.

## Gameplay

- Explore seven authored chambers connected by flat tunnels.
- Find the Fire, Air, Earth, Water, and Aether crystals.
- Cross five generated mazes on the route to the exit.
- Avoid hazards such as the Fire chamber's lava. Falling returns you to a safe
  entrance checkpoint.
- Activate the exit portal after collecting every crystal.

There is no map, so chamber landmarks and elemental colors help with
orientation.

## Controls

| Input | Action |
| --- | --- |
| `W`, `A`, `S`, `D` | Move |
| Mouse | Look around |
| `Shift` | Sprint |
| `Space` | Jump |
| `E` | Collect a crystal or activate the exit portal |
| `Esc` | Pause and release the mouse |

Select **Begin Exploration** on the opening screen to start the timer and
capture the mouse.

## Requirements

The main development setup is:

- Windows 10 or Windows 11
- Visual Studio 2022 with the **Desktop development with C++** workload
- CMake 3.24 or newer
- A graphics driver with OpenGL 3.3 Core support

The required libraries are stored under `third_party/`, so configuration and
building do not download dependencies.

## Build and run

Configure and build the Release version from PowerShell:

```powershell
cmake -S . -B build/crystalbound -G "Visual Studio 17 2022" -A x64 -DCRYSTALBOUND_BUILD_TESTS=ON
cmake --build build/crystalbound --config Release --target crystalbound
./build/crystalbound/Release/crystalbound.exe
```

Use a fixed seed to replay the same generated maze:

```powershell
./build/crystalbound/Release/crystalbound.exe --seed 42
```

Run `crystalbound.exe --help` to see the command-line options.

Release is recommended for normal play. The project contains several large OBJ
files, so a Debug build can take much longer to load before its window appears.

## Run the tests

```powershell
cmake --build build/crystalbound --config Release --target crystalbound_tests crystalbound_seed_corpus
ctest --test-dir build/crystalbound -C Release --output-on-failure
./build/crystalbound/Release/crystalbound_seed_corpus.exe --count 256
```

The tests cover deterministic generation, maze reachability, chamber and object
collision, crystal collection, exit interaction, scene fingerprints, and
generated-seed repeatability.

## Graphics work

Crystalbound uses indexed triangle meshes, model/view/projection
transformations, GLSL vertex and fragment shaders, Phong lighting, emissive
crystals, procedural rock and wood textures, distance fog, and depth-buffered
hidden-surface removal. Chamber and maze models are authored in Blender and
loaded from OBJ/MTL files. Gameplay collision is built separately from the
render meshes so detailed decorations do not make movement unstable.

The project currently has no sound, map, shadow mapping, or save system.
