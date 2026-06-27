# Guía de comprensión y defensa — Dungeon Tavern NPC

Guía detallada de **toda** la implementación, para entenderla y defenderla en el
oral (donde una respuesta floja sobre un detalle puede valer 0). Para cada
subsistema: **qué hace**, **cómo** (mecanismo/código), **por qué** (decisión de
diseño) y **preguntas probables**.

> Convenios: rutas `fichero:línea` son orientativas. "El profesor" = examen oral
> individual; sube el código y lo recompilan con *su* `Starter.hpp`.

Índice:
1. Panorama y arquitectura
2. El frame: qué ocurre cada fotograma
3. Espacios de coordenadas y matrices
4. Recursos GPU (vertex formats, UBOs, descriptor sets, pipelines, render passes)
5. Iluminación (BlinnPhong.frag) — L09
6. Sombras 2D (sol + spots)
7. Ciclo día/noche
8. Skybox
9. Llamas (velas/antorchas)
10. Controlador en primera persona y colisiones
11. NPCs, puertas e interacción
12. UI con ImGui (splash, diálogo, tienda, HUD)
13. Audio — descartado
14. Carga de escena y assets
15. Cumplimiento del esqueleto / compilación
16. Deviaciones respecto al curso
17. Banco de preguntas por tema

---

## 1. Panorama y arquitectura

**Qué es:** una taberna-mazmorra en primera persona, con un NPC con el que se
dialoga y comercia, iluminación dinámica (antorchas/velas + sol), sombras 2D,
ciclo día/noche y skybox.

**Framework:** heredamos de `BaseProject` (del `Starter.hpp` del curso), que posee
todo el boilerplate de Vulkan (instancia, dispositivo, swap chain, bucle de
render) y llama a nuestros *hooks* virtuales. Orden del ciclo de vida
(`DungeonApp.cpp`):

| Hook | Cuándo | Qué hacemos |
|---|---|---|
| `setWindowParameters` | antes de crear nada | tamaño/título/ratio |
| `localInit` | una vez al arrancar | layouts, texturas, pipelines, render passes, cargar escena |
| `pipelinesAndDescriptorSetsInit` | tras crear/recrear swap chain (y al redimensionar) | `create()` de pipelines/passes, `init()` de descriptor sets |
| `pipelinesAndDescriptorSetsCleanup` | antes de recrear | `cleanup()` de ese subconjunto |
| `populateCommandBuffer` | por frame | grabar los comandos de dibujo |
| `updateUniformBuffer` | por frame | lógica de juego + subir UBOs |
| `localCleanup` | al cerrar | liberar lo de `localInit` |

**Responsabilidad por fichero (lo NUESTRO, examinable):**
- `DungeonApp.cpp` — la clase `DungeonTavernNPC`; orquesta todo.
- `SceneTypes.hpp` — structs compartidos: `Light`, los UBOs, `VertexSimple`, `SceneObject`.
- `SceneLoader.hpp` — carga `scene.json` (modelos, luces, puertas, patrullas).
- `FirstPersonController.hpp` — cámara, salto/gravedad, colisiones con deslizamiento.
- `DayNightCycle.hpp` — reloj día/noche → dirección/color/intensidad del sol.
- `DialogueSystem.hpp`, `ShopSystem.hpp`, `SplashScreen.hpp` — UI ImGui.
- `InputBindings.hpp` — única fuente de verdad de teclas.
- `Libs.cpp` — unidad de compilación que define `STARTER_IMPLEMENTATION` (stb/tinygltf).
- `shaders/mesh/*`, `shaders/skybox/*` — nuestros shaders GLSL.

NO es nuestro (no se examina como autor): `include/modules/*` (skeleton) y las
single-header libs (`json.hpp`, `stb_*`, `tiny_gltf.h`, …).

**Preguntas probables:** ¿Qué hace `BaseProject` y qué pones tú? ¿Por qué
`pipelinesAndDescriptorSetsInit` está separado de `localInit`? (porque se re-ejecuta
al redimensionar la ventana: todo lo ligado al swap chain se recrea).

---

## 2. El frame: qué ocurre cada fotograma

Dos funciones por frame:

**A) `updateUniformBuffer` (CPU — lógica + datos):**
1. `GameLogic()` lee input, mueve cámara, detecta interacción, calcula `ViewPrj`.
2. Avanza `animTime` y el ciclo día/noche (`dayNight.update`).
3. Anima puertas (ease exponencial) y NPCs (patrulla / girar al jugador).
4. **Reconstruye el array de luces** desde cero: cada llama encendida añade su luz
   (+ asigna slots de shadow map a los spots). Añade el sol si está sobre el horizonte.
5. Calcula `sunLightVP` (matriz ortográfica del sol).
6. Sube los UBOs: `DSglobal` (global), `DSskybox`, y el UBO por objeto.
7. `buildImGuiFrame()` y reenvía el command buffer.

**B) `populateCommandBuffer` (GPU — grabar dibujo), EN ESTE ORDEN:**
1. `renderSunShadow()` — pinta profundidad de la escena desde el sol → mapa de sombra del sol.
2. `renderSpotShadows()` — un pase por antorcha activa → su mapa de sombra.
3. `RP.begin` (pase principal): dibuja todos los objetos con `Psimple` (Blinn-Phong),
   muestreando los mapas de sombra ya escritos.
4. Skybox al final (`Pskybox`), empujado al far plane.
5. ImGui encima.
6. `RP.end`.

**Por qué ese orden:** los pases de sombra son *offscreen* y deben completarse
**antes** del pase principal, porque el fragment shader los muestrea. Las
dependencias del render pass (`ATDEP_DEPTH_TRANS`) garantizan que la profundidad
escrita sea visible para el muestreo.

**Preguntas probables:** ¿Por qué el skybox se dibuja el último? (para que solo
rellene el fondo donde no hay geometría, vía depth test). ¿Por qué las sombras
antes del pase principal?

---

## 3. Espacios de coordenadas y matrices

Cadena: **modelo → mundo (`mMat`) → vista (`View`) → clip (`Prj`)**. En el shader,
`gl_Position = mvpMat * pos`, con `mvpMat = Prj * View * mMat`.

- **`mMat`** (model/world): `T(pos) · R(yaw) · S(scale) · model->Wm`. `Wm` es la
  matriz base que trae el glTF. Compuesta en `objectWorld()` y en `updateUniformBuffer`.
- **`View`**: `glm::lookAt(cameraPos, cameraPos + camForward, up)`.
- **`Prj`**: `glm::perspective(FOVy, Ar, near, far)` con `Prj[1][1] *= -1`.

**Dos cámaras (tecla `C`):** la principal es **primera persona + perspectiva**. Con
`C` se cambia a una cámara **cenital ORTOGRÁFICA** (proyección paralela, `glm::ortho`)
que el usuario **orbita con el ratón**: horizontal → azimut (`overheadYaw`), vertical →
elevación (`overheadPitch`, limitada a 15°–85° para que el *up* del `lookAt` no degenere).
Las teclas `+`/`-` hacen **zoom** (multiplicador `overheadZoom` sobre los semiejes del
`ortho`: encoger el marco = acercar). Cubre el requisito de *"ver la escena desde
distintos puntos"* y deja la elección **paralela vs perspectiva** visible en pantalla.
La cenital se enmarca en la esfera envolvente (`sceneCenter`/`sceneRadius`) con
`halfW/halfH == Ar` para no deformar; mismo Y-flip de Vulkan. El *look* de primera
persona se congela mientras la cenital usa el ratón.
- **`nMat`** (normal matrix): `inverse(transpose(mMat))`. Se usa para transformar
  normales; **no** se usa `mMat` directamente porque con escalado no uniforme las
  normales dejarían de ser perpendiculares a la superficie. En `MeshSimple.vert`:
  `fragNorm = normalize((nMat * vec4(inNorm,0)).xyz)` (w=0 descarta la traslación).

**Tres detalles Vulkan que SÍ te pueden preguntar:**
1. **`Prj[1][1] *= -1`**: Vulkan tiene el eje Y de clip invertido respecto a OpenGL;
   se corrige negando la fila Y de la proyección.
2. **Profundidad [0,1]**: Vulkan usa rango de profundidad `[0,1]` (OpenGL `[-1,1]`).
   Por eso `Starter.hpp` define `GLM_FORCE_DEPTH_ZERO_TO_ONE` **antes** de incluir GLM
   (motivo del orden de includes documentado en `SceneTypes.hpp`).
3. **Coordenadas homogéneas y división perspectiva** (L02): un punto es `(x,y,z,w)`;
   el cartesiano es `(x/w, y/w, z/w)`. En las sombras de spot haces esa división a
   mano (`proj = sp.xyz / sp.w`) porque la proyección es perspectiva; en la del sol
   (ortográfica) `w==1` y la división es inocua.

**Preguntas probables:** ¿Diferencia `mMat`/`mvpMat`/`nMat`? ¿Por qué `nMat` es la
inversa traspuesta? ¿Por qué `Prj[1][1]*=-1`? ¿Qué es la división perspectiva?

---

## 4. Recursos GPU

### Vertex formats (L13)
- **`VDsimple`**: `pos(vec3) + norm(vec3) + UV(vec2)` — el formato completo para
  iluminar y texturizar.
- **`VDskybox`**: **solo `pos`**, sobre el *mismo* buffer (stride sigue siendo el de
  `VertexSimple`). Es la idea de "distintos formatos para distintas necesidades":
  el skybox y los pases de profundidad solo necesitan posición. Usar el formato
  completo en un pase depth-only dispararía un *warning* de validación por atributos
  no usados.

### Uniform Buffer Objects (L13)
- **`GlobalUniformBufferObject`** (set 0): `eyePos` (xyz + nº luces en w),
  `sunLightVP`, `spotLightVP[MAX_SHADOW_SPOTS]`, `lights[MAX_LIGHTS]`.
- **`UniformBufferObject`** (set 1, por objeto): `mvpMat`, `mMat`, `nMat`, `matParams`
  (x=exponente especular, yzw=emissive).
- **`SkyboxUBO`**: `mvpMat` (rotación-only), `sunDirDay` (dir al sol + dayFactor),
  `sunColor`.
- **`alignas(16)`**: respeta las reglas de alineación std140 de Vulkan (vec4/mat4 a 16
  bytes) para que el layout C++ coincida con el del shader.

### Descriptor sets y layouts (L14)
- **`DSLglobal`** (set 0): binding 0 = UBO global; binding 1 = mapa de sombra del sol;
  binding 2 = array de `MAX_SHADOW_SPOTS` mapas de sombra de spot.
- **`DSLlocalTextured`** (set 1): binding 0 = UBO por objeto; binding 1 = textura albedo.
- **`DSLskybox`**: binding 0 = `SkyboxUBO`; binding 1 = cubemap.
- Cada `SceneObject` tiene su `DS` (su UBO + su textura). El set global es uno y se
  *bindea* una vez por frame.

### Pipelines (L08) y Render passes (L15)
- **`Psimple`**: pase principal, `MeshSimple.vert`+`BlinnPhong.frag`.
- **`Pskybox`**: `setCullMode(FRONT)` (estamos *dentro* del cubo) y
  `setCompareOp(LESS_OR_EQUAL)` (el cielo, empujado al far plane, solo sobrevive donde
  no hay geometría más cercana).
- **`PsunShadow` / `PspotShadow`**: depth-only, `setCullMode(NONE)` (las paredes pueden
  ser planos de una cara; cull podría dejarlas fuera del mapa y filtrar luz).
- **Render passes**: `RP` (principal, al swap chain), `RPsun` (offscreen depth-only,
  tamaño fijo `SUN_SHADOW_RES=2048`), `RPspot[4]` (`SPOT_SHADOW_RES=1024`). Los
  offscreen tienen tamaño fijo porque cubren la escena/cono, no la ventana.

**Preguntas probables:** ¿Qué es un descriptor set vs un layout? ¿Por qué dos vertex
formats? ¿Por qué el skybox tiene cull FRONT y depth LESS_OR_EQUAL? ¿Por qué los
pases de sombra usan cull NONE? ¿Para qué `alignas(16)`?

---

## 5. Iluminación — `BlinnPhong.frag` (núcleo, L09)

Ecuación de L09: `L = Σ_l (modelo de luz) · (BRDF)`. Tu bucle `for(i<numLights)`
suma `blinnPhong(...)` por luz.

**Tres tipos de luz** (`pos.w` = tipo):
- **Direccional** (sol): `L = normalize(-dir.xyz)`, sin atenuación.
- **Point** (velas, "fill" de antorcha): `L = (p - frag)/|p-frag|`, atenúa con distancia.
- **Spot** (antorchas): como point + factor de cono.

**BRDF = difuso + especular:**
- **Difuso (Lambert)**: `albedo * lightColor * max(N·L, 0)`. El `max(..,0)` evita que
  caras de espaldas "resten" luz (L09 lo dice explícito).
- **Especular (Blinn)**: `lightColor * pow(N·H, specExp) * N·L`, con `H=normalize(L+V)`
  (half-vector). Se pondera por `N·L` para matar el brillo rasante que cruzaría el
  suelo al amanecer/atardecer.

**Atenuación** (`computeAttenuation`): si `range>0`, `max(0, 1-(d/range)²)` (alcance
finito); si `range==0`, `1/(1+d²)`.

**Cono del spot**: `cosAngle = dot(-L, dir)`, `spotFactor = smoothstep(cones.y, cones.x,
cosAngle)` con `cones.x=cos(inner) > cones.y=cos(outer)`.

**Ambient hemisférico**: en vez de un relleno plano, mezcla un tono "cielo" (frío,
arriba) y "suelo" (cálido, abajo) según `N.y`, y lo escala con la intensidad del sol
(`ambientScale`, con un suelo de 0.15 para que de noche las esquinas no sean negro puro).

**Salida**: tonemapping Reinhard `color/(color+1)` + gamma `pow(color, 1/2.2)`.

**Mapeo exacto a L09** (clave para defender): la dirección `lx` apunta hacia la luz
(por eso niegas `dir` en la direccional); el cono es el `clamp((lx·d−cOUT)/(cIN−cOUT))`
del curso pero con `smoothstep`; Lambert y Blinn son modelos del curso; el clamp final
a [0,1] lo hace Vulkan solo a la salida.

**Preguntas probables:** explica el half-vector y por qué Blinn y no Phong; qué es
`specExp`; por qué `max(N·L,0)`; por qué reconstruyes las luces cada frame; por qué la
antorcha es spot y la vela point; qué hace el ambient hemisférico.

---

## 6. Sombras 2D (sol + spots) — tu extensión "lucida"

Técnica de *shadow mapping* (E07 del curso): pintar la profundidad de la escena
**desde la luz** en un mapa, y en el pase principal comparar la profundidad del
fragmento contra ese mapa; si hay algo más cercano a la luz, está en sombra.

**Dos variantes:**
- **Sol (direccional) → proyección ORTOGRÁFICA.** `glm::ortho` enmarcando toda la
  escena (`sceneCenter`/`sceneRadius`). Rayos paralelos → caja, no frustum.
- **Spot (antorcha) → proyección PERSPECTIVA** (`computeSpotLightVP`): una luz con
  posición y cono → frustum que converge. FOV = ángulo externo del cono + margen.

**`lightVP`** = `Prj_luz · View_luz`, con el mismo flip de Y de Vulkan. Se usa para
(a) renderizar el mapa de profundidad y (b) muestrearlo en el fragment.

**En el fragment** (`sunShadowFactor`/`spotShadowFactor`):
1. Proyectar `worldPos` al clip de la luz; en spot, **división perspectiva** `xyz/w`.
2. Mapear a UV `[0,1]`; si cae fuera del frustum → "iluminado" (factor 1).
3. **Bias** dependiente de pendiente: `max(0.0015*(1-N·L), 0.0004)` → evita *shadow
   acne* (auto-sombreado) sin causar *peter-panning*.
4. **PCF 3×3**: promedia 9 muestras alrededor del texel → bordes suaves.

**Detalles de gestión** (`updateUniformBuffer`):
- Cada antorcha encendida toma un *slot* de spot shadow si queda libre (máx
  `MAX_SHADOW_SPOTS=4`); `cones.z` lleva el índice de slot al shader (-1 = no proyecta).
- El emisor se salta a sí mismo en su pase (`emitterIndex`) para no auto-sombrearse.
- El **suelo recibe pero no proyecta** (`tag=="ground"` se salta en los pases de sombra).
- `shadowStrength` (0..1) hace que la sombra del sol **entre/salga gradualmente** con
  la altura solar → sin el "flash" del antiguo on/off booleano.

**Por qué spot y no point para sombras:** un point light iluminaría en todas
direcciones → necesitaría un *cube shadow map* (6 caras), que queda fuera del temario
(E07 enseña el mapa 2D). Modelar la antorcha como spot permite **un solo mapa 2D**.

**Preguntas probables:** ¿Orto vs perspectiva, por qué cada una? ¿Qué es el bias y por
qué depende de la pendiente? ¿Qué es PCF? ¿Por qué el sol necesita más resolución
(2048) que los spots (1024)? ¿Por qué el emisor se excluye?

---

## 7. Ciclo día/noche — `DayNightCycle.hpp`

Reloj normalizado `timeOfDay ∈ [0,1)` (0=medianoche, 0.5=mediodía), avanza solo
(`SPEED = 1/120`, un día cada 120 s). De ahí deriva un `State`:
- `phase = (timeOfDay-0.25)*2π`, `elevation = sin(phase)` (0 en horizonte, 1 cénit).
- `toSun = normalize(-cos(phase), elevation, 0.20)` (este→cénit→oeste, leve inclinación Z).
- `aboveFade = smoothstep(0, 0.15, elevation)` — el sol solo ilumina sobre el horizonte
  (si no, brillaría hacia arriba a través del suelo: el "flash" del amanecer).
- `dayMix = smoothstep(0, 0.75, elevation)` — mezcla sol cálido bajo → blanco de mediodía.
- Color: `mix(luna, mix(warmSun, noonSun, dayMix), aboveFade)`.
- Intensidad: 0 de noche; de día rampa 0.45→1.2.

**Por qué no hay luz direccional de noche:** la luna no proyecta; de noche el exterior
solo se ilumina por las antorchas que se cuelan por puertas/ventanas. La direccional no
atenúa, así que debe quedarse en la escala de las luces de antorcha (~0.6-1.0) o
inundaría el interior.

**Preguntas probables:** ¿Cómo modelas el arco del sol? ¿Por qué el fade empieza en el
horizonte? ¿Por qué la luz es continua y no controlable por el jugador? (decisión: es
ambiente, no mecánica).

---

## 8. Skybox — `Skybox.vert` / `Skybox.frag`

- La posición local del cubo *es* la dirección de muestreo del cubemap (`fragDir = inPosition`).
- **Empujado al far plane**: `gl_Position = pos.xyww` (z=w → NDC z=1). Con depth test
  `LESS_OR_EQUAL` el cielo solo sobrevive donde no se escribió profundidad más cercana
  → se ve por ventanas, puerta y reja.
- **Vista rotación-only** (`SkyViewPrj = Prj * mat4(mat3(View))`): se quita la
  traslación para que el cielo no se desplace con la cámara.
- Tinte día/noche: de noche multiplica el panorama por un azul muy oscuro; mezcla con
  `dayFactor`. Dibuja un disco de sol/luna con halo cuyo tamaño crece con la altura
  (`bodyFade` evita el "pop" al cruzar el horizonte). Gamma como el pase principal.

**Preguntas probables:** ¿Qué es un cubemap y cómo se muestrea? ¿Por qué `xyww`? ¿Por
qué cull FRONT? ¿Por qué quitas la traslación de la vista?

---

## 9. Llamas (velas y antorchas)

Estado en el propio `SceneObject` (no en una lista global): `isFlame`, `lit`, su
`Light`, `baseIntensity`, `flamePhase`, y opcionalmente un `litModel`.

- **Malla lit/unlit**: una antorcha trae dos mallas (`torch` y `torch_lit`). Si el par
  existe en disco se cargan ambas y se dibuja la que toque según `lit`; comparten
  textura y UBO, así que cambiar es solo *bindear* otro buffer. Velas sin variante
  (`candle_triple`) no cambian de malla.
- **Encender/apagar**: pulsar E sobre la llama solo invierte `lit`. Como cada frame se
  reconstruye el array de luces, encender/apagar una no afecta a las demás (no hay
  índices compartidos que mantener).
- **Antorcha = spot que proyecta sombra + un point "fill"**: el spot da el haz
  direccional (y la sombra), pero una llama real brilla en todas direcciones; por eso
  se añade un point tenue co-situado que ilumina la pared y props cercanos (sin cono ni
  sombra).
- **Flicker**: `factor = 0.9 + 0.1*wobble`, con `wobble` = suma de dos senos a
  frecuencias no relacionadas (11 y 6.3) + `flamePhase` por objeto → parpadeo orgánico,
  nunca más brillante que el valor de diseño, y desincronizado entre llamas.

**Preguntas probables:** ¿Por qué la luz vive en el objeto y no en una lista global?
¿Por qué dos senos para el flicker? ¿Por qué la antorcha tiene dos luces?

---

## 10. Controlador en primera persona y colisiones — `FirstPersonController.hpp`

- **Mirar**: el ratón actualiza `yaw`/`pitch` (pitch clamp ±89°); `forward` se
  recompone con trigonometría. `MOUSE_SENS` escala el delta.
- **Salto/gravedad**: salto solo en el *flanco* de pulsación (`jumpStarted`), no
  mientras se mantiene; integra `verticalVelocity` con `GRAVITY` hasta tocar
  `EYE_HEIGHT` (suelo).
- **Colisiones con deslizamiento**: intenta el movimiento completo; si choca, prueba
  cada eje horizontal por separado. Caminar en diagonal contra una pared conserva la
  componente paralela y descarta solo la que empuja contra ella → se desliza en vez de
  pegarse. `tryZ` parte del `newPos.x` ya aceptado para no atravesar esquinas.
- El jugador es un AABB (`PLAYER_RADIUS`, `EYE_HEIGHT`) que se prueba contra los
  colliders de los objetos `collidable`.

**Colliders** (módulo del skeleton): AABB (caja alineada a ejes), OOBB (orientada) y
BVH (jerarquía de cajas, p. ej. la puerta con hueco transitable). Se ajustan con
`fitOOBB`/`initBVH` y se actualizan con `setWorldMatrix`.

**Preguntas probables:** ¿Cómo consigues el deslizamiento por paredes? ¿Por qué el
salto es edge-triggered? ¿Diferencia AABB/OOBB/BVH y dónde usas cada uno?

---

## 11. NPCs, puertas e interacción

- **Puertas**: la malla tiene la bisagra en su origen, así que abrir/cerrar es animar
  `yaw` entre `openYaw`/`closedYaw` con ease exponencial (`DOOR_SWING_RATE`). Mientras
  se mueve **no colisiona** (no atrapa al jugador); recupera el collider al posarse. Al
  abrir, no se cierra encima del jugador (`tryToggleDoor` comprueba colisión en la pose
  destino).
- **NPCs**: si están hablando contigo o entras en su `NPC_PERSONAL_SPACE`, giran a
  mirarte (giro de arco más corto, `turnNpcToward`). Si no, los de patrulla recorren
  sus waypoints (cíclico, con pausa) y los estáticos vuelven a su `restYaw`.
- **Detección de interacción** (`GameLogic`): recorre objetos interactuables (llama,
  puerta, NPC), y elige el más cercano que esté **a la vista** mediante un test barato:
  distancia < `INTERACT_DIST` y alineación `dot(dir, lookH) > INTERACT_DOT`. **No** es un
  raycast real contra mallas — es una aproximación. Recuerda el índice para que E actúe
  sobre ese exacto. El prompt del crosshair se adapta ("[E] Light/Extinguish/Open/Talk").
- **E** es edge-triggered; con la tienda abierta, E solo cierra la tienda.

**Preguntas probables:** ¿Cómo decides qué objeto interactúa? (di que es dot+distancia,
no raycast). ¿Cómo evitas que la puerta atrape al jugador? ¿Cómo gira el NPC por el
camino más corto?

---

## 12. UI con ImGui (splash, diálogo, tienda, HUD)

ImGui se integra sobre Vulkan (`imgui_impl_vulkan` + `imgui_impl_glfw`) usando recursos
públicos de `BaseProject` (instancia, device, cola, render pass, MSAA). Los iconos de
la tienda se registran de forma perezosa en el primer frame (el backend Vulkan sube
después de `localInit`).

**Patrón común — "request flags":** las clases de UI no llaman a otros sistemas; solo
**registran intención** (`startRequest`, `shopRequest`, `closeRequest`…) que la app
*consume* (`consume...()`, one-shot). Así el diálogo no depende de la tienda, etc.
(bajo acoplamiento).

- **SplashScreen**: menú inicial (Start/Controls/Quit) sobre la escena viva; reestiliza
  ImGui a "madera de taberna" (los `PushStyleColor(5)`/`Var(2)` deben cuajar con sus
  `Pop`, o el estilo se filtra a otras ventanas).
- **DialogueSystem**: grafo por NPC (árbol de nodos con texto y hasta 3 opciones 1/2/3);
  una opción salta a otro nodo, termina, o lanza acción ("shop").
- **ShopSystem**: tabla de compra/venta; vender paga la mitad (división entera);
  botones deshabilitados si no hay stock/oro o no posees el ítem.
- **HUD**: crosshair + prompt contextual; panel de debug (FPS, posición).

**Preguntas probables:** ¿Cómo se comunica el diálogo con la tienda sin acoplarlas?
(flags one-shot). ¿Por qué los iconos se registran en el primer frame?

---

## 13. Audio — descartado

**Decisión del equipo: el audio NO forma parte del proyecto.** Existía un
`AudioSystem.hpp` (wrapper de miniaudio) pero estaba **sin conectar** (declarado en
`DungeonApp.cpp` pero sin `init()` ni reproducción → no sonaba). Se retira del proyecto
para no dejar código muerto ni una dependencia (miniaudio) ajena al skeleton.

> Si te preguntan por audio en el oral: simplemente **no es una feature** del proyecto.
> No menciones miniaudio. (`AudioSystem.hpp` y `miniaudio.h` ya se eliminaron del repo,
> así que no queda código muerto ni dependencia ajena al skeleton.)

---

## 14. Carga de escena y assets — `SceneLoader.hpp`

`scene.json` (en `source/assets/scenes/`) lista objetos: `model`, `pos`, `yaw`,
`scale`, `tag`, y opcionales (`patrol`, `colliderBoxes`, `emissive`, `npcId`…). El
loader es **templado** para no depender de los tipos concretos (definidos en
`DungeonApp.cpp`); resuelve modelos/texturas por *callbacks* que pegan a la caché.

- **Cachés** (en la app): `modelCache` (por ruta de modelo), `textureCache` (por ruta de
  textura → props del dungeon comparten un atlas), `modelTextureCache` (qué textura usa
  cada modelo; si no tiene → `Tdungeon`).
- **glTF** vía `Model::init(..., GLTF)`. La textura embebida se resuelve leyendo
  `images[0].uri` del glTF (`texturePathForModel`).
- **Suelo procedural**: un quad grande en y=0 generado con `initMesh` (no se carga de
  archivo), UVs tileadas; se añade *después* de calcular los bounds para no inflar el
  frustum del sol.
- **Tags** que colisionan: `wall/structure/furniture/prop` (+ puertas). `npc`/`ground`/
  `light_source` no colisionan.

**Preguntas probables:** ¿geometría cargada vs generada? (glTF vs suelo procedural). ¿Por
qué compartes texturas por atlas? ¿Cómo sabes la textura de cada modelo?

---

## 15. Cumplimiento del esqueleto / compilación (ver también la verificación hecha)

- ✅ **Compila** (binario generado, shaders incluidos).
- ✅ **Headers del skeleton verbatim** (`Starter.hpp` etc. sin editar; solo movidos).
- ✅ **`STARTER_IMPLEMENTATION` una sola vez** en `Libs.cpp`; `Starter.hpp` incluido por
  ruta estándar → sustituir su versión instrumentada no rompe la compilación del código.
- ⚠ **Build offline desde cero FALLA**: el `CMakeLists.txt` descarga GLFW/GLM/ImGui por
  red (`FetchContent`). Solo compila aquí porque están en caché. **Acción pendiente**:
  vendorizar deps o `find_package` con fallback, y confirmar con el profesor si compila
  con tu CMake o con el suyo (ImGui no es del skeleton).

---

## 16. Deviaciones respecto al curso (defenderlas)

1. **`smoothstep` en el cono del spot** en vez del `clamp` lineal de L09 → borde más suave.
2. **Tonemapping Reinhard por canal** `c/(c+1)` en vez del recomendado por luminancia
   `c/(Y+1)`, `Y=0.2126R+0.7152G+0.0722B` → más simple.
3. **Atenuación de alcance finito** `max(0,1-(d/range)²)` en vez de `(g/d)^β` → radio
   acotado por luz (las antorchas no inundan la taberna).
4. **Blinn** (`N·H`) en vez de **Phong** (`r·ωr`) → evita calcular el rayo reflejado.
5. **NO toon shading**: se eligió Blinn-Phong continuo; el toon (bandas) chocaría con
   sombras suaves/PCF, atenuación y día/noche. (Es un modelo opcional de L09).

---

## 17. Banco de preguntas por tema (las más probables)

**Vulkan / pipeline:** ¿Qué es render pass / pipeline / command buffer / descriptor set?
¿Por qué pases offscreen para sombras? ¿Por qué `pipelinesAndDescriptorSetsInit` aparte?

**Matrices:** `mMat` vs `mvpMat` vs `nMat`. ¿Por qué `nMat`=inv-transp? ¿`Prj[1][1]*=-1`?
¿División perspectiva? ¿Profundidad [0,1] y `GLM_FORCE_DEPTH_ZERO_TO_ONE`?

**Iluminación (L09):** half-vector y Blinn vs Phong; `max(N·L,0)`; tipos de luz; cono del
spot; atenuación; ambient hemisférico; tonemapping + gamma; ¿por qué el color puede pasar
de 1?

**Sombras:** orto vs perspectiva; bias y acne; PCF; Y-flip; ¿por qué spot y no point
(cubemap)?; slots y exclusión del emisor.

**Día/noche y skybox:** arco del sol; por qué no hay direccional de noche; `xyww`; cull
FRONT; vista sin traslación; cubemap.

**Geometría:** glTF vs suelo procedural; vertex formats (`VDsimple`/`VDskybox`); índices.

**Interacción/juego:** detección por dot+distancia; deslizamiento por paredes; salto
edge-triggered; puertas con bisagra; patrón request-flags de la UI.

**Texturas (L11):** UV; sampler; SRGB→lineal; atlas compartido; cubemap.

**Flanco a tener cubierto:** el **build offline** (§15). Tenlo resuelto antes del examen.
(El audio queda descartado, §13.)
