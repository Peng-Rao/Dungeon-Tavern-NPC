/**
 * @file Libs.cpp
 * @brief Single translation unit that compiles the framework + single-header
 *        library implementations.
 */

// Single translation unit that compiles the framework library implementations.
//
// The skeleton's module headers have NO include guards and split declaration
// from implementation behind *_IMPLEMENTATION macros, so the implementation
// must be pulled into exactly one .cpp (mirrors the skeleton's own Libs.cpp).
// Defining STARTER_IMPLEMENTATION here also compiles the bundled single-header
// libraries (tinyobjloader, stb_image, stb_image_write, sinfl, tinygltf).
//
// We only need Starter: Colliders.hpp is header-only (all-inline) and is pulled
// in where used via SceneTypes.hpp; TextMaker/Scene/Animations are unused by
// this project (we have our own SceneLoader and ImGui-based DialogueSystem).
#define STARTER_IMPLEMENTATION
#include <modules/Starter.hpp>
