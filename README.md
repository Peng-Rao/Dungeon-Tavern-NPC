[![Linux Build](https://github.com/Peng-Rao/Dungeon-Tavern-NPC/actions/workflows/ci.yml/badge.svg?event=push)](https://github.com/Peng-Rao/Dungeon-Tavern-NPC/actions/workflows/ci.yml)

Design a 3D tavern environment in which the user can approach and interact with one or more virtual characters (e.g., innkeeper, bard, merchant). The user navigates in first person and can trigger the interaction by getting close to a character and pressing a key.


Requirements:
- The project must be written in C++ with Vulkan and the course Starter.hpp.
- Teams should normally have two or three students. Individual projects need a convincing motivation, and four-person groups require prior discussion.
- Every team member must register individually and must present in the same exam session.
- Registration remains open until mid-January 2027, but must happen before the presentation. The document recommends registering when implementation is about 40% complete.
- Students must understand all code in detail, including code helped by AI tools. Weak answers about implementation details can score zero for the corresponding individual question.
- Expected graphics content includes geometry loading or generation, textures, shaders written by the team, vertex formats, uniform blocks, graphics pipelines, render passes, command buffers, draw calls, navigation, interaction, lighting, and visible image quality.
- Suggested evaluation additions include animation, basic physics, simple intelligence, on-screen display, menus, scene changes, and polished visual composition.

## Project structure

The project follows the course `Skeleton2026-distribution` layout:

- `source/src/` — application sources (`DungeonApp.cpp` holds `main`, plus `Libs.cpp` compiling the framework implementation)
- `source/include/` — application headers, bundled third-party single-header libraries, and the course framework under `modules/`
- `source/shaders/` — GLSL shaders for the app (`mesh/` and `skybox/`)
- `source/assets/` — models, textures, and `scenes/scene.json`

## Build and run

Requires the Vulkan SDK (with `glslc`), CMake ≥ 3.21, and a C++20 compiler. GLFW, GLM, and Dear ImGui are fetched automatically.

```sh
./run.sh
```

or manually:

```sh
cmake --preset default
cmake --build --preset default
cd build && ./DungeonTavernNPC
```
