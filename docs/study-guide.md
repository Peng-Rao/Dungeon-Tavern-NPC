# Guía de estudio — Dungeon Tavern NPC ↔ slides del curso

Mapa bidireccional entre las diapositivas de **Computer Graphics (M. Gribaudo)** y
los ficheros del proyecto. Objetivo: poder **defender cada parte del código** en el
oral (donde una respuesta floja sobre detalles de implementación puede valer 0).

Profesor / slides: `L00`–`L15` en `…/ComputerGraphics/slides/`.

> Solo se examina lo **nuestro**: `source/src/*` (menos las libs de terceros que
> incluye), `source/shaders/mesh/*`, `source/shaders/skybox/*` y los `.json` de
> `source/assets/`. Los `modules/*` del skeleton y las single-header libs NO.

---

## Parte A — De cada diapositiva a sus ficheros

| Lección | Qué leer / dominar | Ficheros del proyecto que explica |
|---|---|---|
| **L02 3D coords & transforms** | Traslación/rotación/escala, matrices homogéneas, composición T·R·S | `mMat` en `DungeonApp.cpp` (`updateUniformBuffer`); `objectWorld()`; rotaciones de yaw de puertas/NPCs |
| **L03 Advanced transforms & parallel projections** | Orden de transformaciones, proyección ortográfica | Matriz **ortográfica del sol** (`glm::ortho` en `updateUniformBuffer`) para el shadow map direccional |
| **L04 Axonometric & perspective projections** | Proyección perspectiva, FOV, near/far | `glm::perspective` (cámara) y la perspectiva de los spots en `computeSpotLightVP()` |
| **L05 View & World matrices** | model→world→view→clip; `lookAt`; matriz de vista | `glm::lookAt` + `ViewPrj`/`SkyViewPrj` (`GameLogic`); `sunLightVP`, `lightVP` de spots |
| **L06 Depth testing & Meshes** | Z-buffer, formato de mallas, índices | Depth test del `RP`; skybox con `LESS_OR_EQUAL`; carga de mallas glTF (`getCachedModel`) y malla procedural del suelo (`initMesh`) |
| **L07 Rendering** | Pipeline conceptual scan-line, vertex→fragment | Flujo general; `MeshSimple.vert` → `BlinnPhong.frag` |
| **L08 Pipelines** | Qué es un graphics pipeline, estados fijos (cull, depth, blend) | `Psimple`, `Pskybox` (`setCullMode`, `setCompareOp`), `PsunShadow`, `PspotShadow` |
| **L09 Light models & BRDFs** ⭐ | Direccional/point/spot; decay 1/d²; cono `clamp((lx·d−cOUT)/(cIN−cOUT))`; Lambert diffuse; Phong vs **Blinn**; tonemapping + gamma | `BlinnPhong.frag` (núcleo), `struct Light`/`Light setup` en `SceneLoader.hpp`, reconstrucción de luces en `DungeonApp.cpp` |
| **L10 Smooth shading** | Flat/Gouraud/Phong shading; interpolación de normales; matriz normal `inv(transp)` | `MeshSimple.vert` (`fragNorm = nMat * inNorm`), `nMat` en `UniformBufferObject` (`SceneTypes.hpp`) |
| **L11 UV mapping & Textures** | Coordenadas UV, sampler, SRGB→lineal, mipmaps, cube map | `albedoMap` en `BlinnPhong.frag`; UVs tileadas del suelo; `Tdungeon`/`Tground`; **cube map** `TenvMap` + `Skybox.frag` |
| **L12 Vulkan I** | Instancia, device, swap chain, command queue | Lo gestiona `BaseProject`; ver doc de la clase en `DungeonApp.cpp` |
| **L13 Vulkan II** | Buffers, memoria, **uniform blocks**, vertex input | UBOs (`GlobalUniformBufferObject`, `UniformBufferObject`, `SkyboxUBO`); `VDsimple`/`VDskybox` (vertex formats) |
| **L14 Vulkan III** | **Descriptor sets / layouts**, samplers, push constants | `DSLglobal`/`DSLlocalTextured`/`DSLskybox`; `DSglobal`/`obj.DS`; push constant `lightVP` en `SunShadow.vert` |
| **L15 Pipelines, Render Passes & Command Buffers** ⭐ | Render pass (attachments, dependencies), command buffer recording, pases offscreen | `RP`/`RPsun`/`RPspot`; `populateCommandBuffer()`; `renderSunShadow()`/`renderSpotShadows()` |

⭐ = las dos lecciones con mayor probabilidad de pregunta sobre este proyecto.

---

## Parte B — De cada fichero a sus diapositivas

### `source/shaders/` (lo más examinable)
- **`BlinnPhong.frag`** → **L09** (todo), **L11** (sampler `albedoMap`, SRGB), **L05** (posiciones/normales en world space). El corazón gráfico.
- **`MeshSimple.vert`** → **L05** (mvp/m matrices), **L10** (normal con `nMat`), **L13** (vertex input / UBO).
- **`SunShadow.vert` / `SunShadow.frag`** → **L03/L04** (orto vs perspectiva de la luz), **L06** (depth-only), **L15** (pase offscreen).
- **`Skybox.vert` / `Skybox.frag`** → **L06** (`LESS_OR_EQUAL`, `pos.xyww`), **L11** (samplerCube), **L05** (vista rotation-only).

### `source/src/`
- **`SceneTypes.hpp`** → **L09** (`struct Light`, tipos), **L13** (layout de UBOs, `alignas(16)`), **L05/L10** (`mMat`/`nMat`).
- **`DungeonApp.cpp`** → **L12–L15** (toda la infraestructura Vulkan), **L08** (pipelines), **L09** (montaje de luces), **L05** (cámara). El que une todo.
- **`SceneLoader.hpp`** → **L09** (parámetros de cada luz: spot/point, cono, decay), **L11** (resolución de texturas).
- **`FirstPersonController.hpp`** → **L05** (cámara `lookAt`, yaw/pitch → forward); navegación + colisiones (extra).
- **`DayNightCycle.hpp`** → **L09** (dirección/color/intensidad de la luz direccional; sol↔luna).
- **`InputBindings.hpp`** → interacción (no es tema de slide; OSD/menús como extra).
- **`DialogueSystem.hpp` / `ShopSystem.hpp` / `SplashScreen.hpp`** → extras del README (interacción, menús, OSD con ImGui). No son temas de slide pero **suben nota**.
- **`Libs.cpp`** → unidad de compilación de las libs (stb/tinygltf). Patrón del skeleton.

---

## Orden de lectura recomendado (1 semana)

1. **L05** (matrices/espacios) — base de todo lo demás.
2. **L09** ⭐ con `BlinnPhong.frag` abierto al lado — la lección estrella.
3. **L10** con `MeshSimple.vert` — normales y `nMat`.
4. **L11** con `Skybox.frag` y el suelo — texturas y cube map.
5. **L13 + L14** — UBOs, vertex formats, descriptor sets (la "fontanería" Vulkan).
6. **L15** ⭐ con `populateCommandBuffer` + `renderSunShadow` — render passes y los pases offscreen de sombras.
7. **L06/L08** de repaso — depth test y pipelines.

> Nota: las **sombras 2D** (sol ortográfico + spots perspectivos) no son una
> lección propia: se apoyan en L03/L04 (proyecciones), L05 (matriz de luz),
> L06 (depth) y L15 (pase offscreen). Es tu extensión "lucida" — domínala con
> esas cuatro.

---

## Deviaciones del proyecto respecto al curso (prepáralas para el oral)

1. **Cono de spot con `smoothstep`** en vez del `clamp` lineal de L09 → borde más suave.
2. **Tonemapping Reinhard por canal** `c/(c+1)` en vez del recomendado por
   luminancia `c/(Y+1)` con `Y=0.2126R+0.7152G+0.0722B`.
3. **Atenuación con alcance finito** `max(0,1−(d/range)²)` en vez de `(g/d)^β`
   → cada antorcha tiene un radio acotado.
4. **Blinn** (`N·H`) en vez de **Phong** (`r·ωr`) para el especular → evita calcular `r`.
