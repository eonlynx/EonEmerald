# Auditoría del merge EonEmerald + expansion 1.10.2

Rama: `port/eon-features`
Fecha: 2026-11-15

Esta es la auditoría consolidada después de la pasada final de decisiones y de la primera
compilación completa. El ROM compila; las pruebas en emulador siguen pendientes.

## 1. Estado general

- Merge base: expansion 1.10.2 sobre pret `50d325f081`; EonEmerald injertado desde
  `main-grafted`.
- Conflictos originales: 205. Inventario por archivo en `docs/eon-merge-resolution.md`.
- `trainers.party` es la fuente de verdad; `trainers.h` está generado desde ella.
- No se ha mergeado `hns/master`, por decisión expresa.
- El ROM compila con `make -j12` (modern) usando devkitARM. No se han ejecutado pruebas de
  batalla, guardado o mapas.

Conteos estáticos actuales:

```text
movimientos: 935
items: 836 / ITEMS_COUNT 836
habilidades: 314 / ABILITIES_COUNT_EON 314
trainers.party: 864
object gfx: 254 / NUM 254
JSON de mapas: válido
marcadores de conflicto: 0
worktree: limpio
```

### 1.1 Correcciones de compilación

La primera compilación completa requirió estos ajustes, todos incluidos en el árbol:

- `ITEM_TM_ENDURE` vuelve a ser el TM45 generado por `FOREACH_TM`; se eliminó el ID manual
  que colisionaba con el enum de `include/item.h`. `ITEM_EXP_ALL` queda en el ID 835 y
  `ITEMS_COUNT` en 836.
- Las reglas de `Makefile` y `map_data_rules.mk` convierten los `.party`/`.s` a LF antes de
  `trainerproc`/`preproc`. En Windows, `cpp` emite CRLF y el parser de enums de `preproc` no
  lo tolera.
- Se restauraron las clases de facility de Kanto/Johto (`FACILITY_CLASS_BROCK`..`DRAKE`) y
  `TRAINER_CLASS_LEADER_2` como alias de `TRAINER_CLASS_LEADER` para los lookups de Frontier.
- Se restauraron los heal locations de Eon (Islas Sete, Ruta 121/123, Faraway, Navel Rock,
  Birth Island e Indigo Plateau).
- `src/dns.c` usa `DNS_TIME_*` para no chocar con los macros `TIME_DAY`/`TIME_NIGHT` de
  `rtc.h`.
- Se eliminó `.disableReflectionPaletteLoad` de los graphics info de Eon: expansion usa ese
  bit para `compressed` y el campo no tenía código que lo leyera.
- Se añadieron las declaraciones de texto del Exchange Corner y los scripts de item balls de
  Eon que faltaban (`item_ball_scripts.inc`).
- `teachable_learnsets.h` y `battle_partners.h` se regeneraron durante la compilación.

## 2. Decisiones consolidadas en esta pasada

### 2.1 Items viejos: se conservan ambos mundos

Se eliminó `I_EON_LEGACY_ITEM_EFFECTS` porque era una decisión de alto impacto y creaba
duplicados/nombres repetidos. Ahora:

- Los slots viejos conservan sus efectos originales de expansion: Everstone previene evolución,
  Focus Band da el 10%, Lucky Egg sube EXP, Mental Herb cura, Cleanse Tag reduce encuentros y
  Lucky Punch boostea a Chansey.
- Los efectos que Eon quería se usan a través de los items modernos: Power Herb, Covert Cloak,
  Eviolite, Focus Sash, Loaded Dice y Punching Glove.
- Los entrenadores, Frontier y Exchange Corner usan esos items modernos donde Eon usaba el
  efecto repurposado.
- No hay nombres duplicados ni funciones clásicas perdidas.

Esto mantiene la intención de Eon sin romper el ecosistema de items de expansion.

### 2.2 Exp. All: item separado, no flag de mensaje

Se eliminó `B_EON_EXP_ALL_MESSAGE` y se separaron las dos cosas:

- `ITEM_EXP_ALL` es un key item nuevo (ID 835) que activa/desactiva el reparto de EXP.
- El held Exp. Share sigue existiendo con `I_EXP_SHARE_ITEM GEN_3`.
- `I_EXP_SHARE_FLAG 0x494` sigue siendo el flag que enciende el efecto de party.
- El mensaje de Eon y la cantidad compartida se usan siempre que Exp. All está activo.
- Rustboro entrega Exp. All; el Exchange Corner sigue ofreciendo el held Exp. Share.

La ventaja es que no hay que elegir entre el held Exp. Share de expansion y el key item de Eon.

### 2.3 Movimientos: se eliminaron los híbridos

Se revirtió el segundo lote de stats que había quedado a medias: movimientos cuyo efecto Eon
también cambiaba (Waterfall, Strength, Mega Punch, Cut, Take Down, Blast Burn/Frenzy
Plant/Hydro Cannon, Sky Attack, etc.) vuelven a los valores de expansion. Así no hay
movimientos “ni de Eon ni de expansion”.

Se mantienen los 67 movimientos cuyo efecto no cambió y que por tanto son seguros:
Absorb, Mega Drain, Aurora Beam, Dragon Breath, Rock Slide, Poison Tail, los cambios de tipo
Comet Punch/Fury Attack/Dizzy Punch/Twineedle/Spike Cannon y los buffs de precisión de estado.

Quedan 95 movimientos con diferencias de efecto, objetivo o prioridad, excluidos a propósito.

### 2.4 PSS condicional: corregido para formas y tipos dinámicos

`GetMoveCategoryForSpecies` ahora:

- Usa la especie base, así que las Mega Evoluciones conservan el override de su especie.
- Usa el tipo efectivo del movimiento, así que movimientos Ghost dinámicos también reciben el
  override de Ghost.
- Sigue mostrando la categoría efectiva en el menú de batalla, resumen, move relearner y
  Pokédex.

### 2.5 Exp. All, berries y Heart Scale

- Razz/Bluk/Nanab/Pinap/Wepear curan enamoramiento.
- Belue/Durin/Pamtre/Watmel restauran stats bajados.
- Cornn/Magost/Nomel/Rabuta/Spelon restauran HP.
- Heart Scale cura 1/8 a un Luvdisc que la lleve al final del turno.

## 3. Pendiente real

### 3.1 Los 95 movimientos restantes

Son los cambios experimentales de Eon:

- Efectos repurposados: Charge → Volt Switch, Flash/Water Sport/Spider Web dañinos, etc.
- Objetivos raros: clima, Haze, Perish Song, Petal Dance, Sing, Supersonic, etc.
- Prioridades raras: Arm Thrust +1, Memento +4, Transform +3, Zap Cannon -1, Teleport 0, etc.
- Precisión 0 → 100 en movimientos que expansion considera “siempre aciertan”.

No se portan porque introducen comportamientos raros o rotos en dobles/IA. Están documentados
como exclusión deliberada.

### 3.2 Compilación y pruebas

La compilación completa pasa con `make -j12` (modern). Faltan pruebas de:

- Batalla normal y doble.
- Mega Evolución, Teracristal y Dynamax.
- Battle Frontier, Trainer Hill y Battle Tent.
- Guardado/carga y compatibilidad de saves.
- DNS, nubes, Exp. All y el nuevo item ID 835.

### 3.3 Consistencia de items

- `battle_tent.h` y `trainer_hill.h` siguen usando IDs viejos (`ITEM_FOCUS_BAND`,
  `ITEM_MENTAL_HERB`). Ahora conservan sus efectos originales, lo cual es coherente con
  expansion, pero no con los entrenadores normales/Frontier que usan items modernos.
- Los held items salvajes siguen usando IDs viejos; por tanto conservan efectos originales.
  Eon los habría repurposado. Es una diferencia menor pero real.
- El Exchange Corner ya usa items modernos para Focus Sash, Eviolite, Punching Glove, Loaded
  Dice y Power Herb.

### 3.4 Contenido no referenciado

- 286 símbolos `gText_*` de Eon no existen en `src/strings.c`; ninguno está referenciado.
- Quedan posibles assets de object-event/overworld que solo existirían en Eon y no están
  referenciados por mapas.

### 3.5 `hns/master`

No mergeado, por decisión expresa. No debe hacerse hasta cerrar las pruebas de regresión.

## 4. Riesgos que quedan

### 4.1 Escalado dinámico de niveles: riesgo medio

`B_EON_DYNAMIC_TRAINER_LEVELS = TRUE` sigue siendo el único flag Eon-specific que queda.
Implementa el promedio del equipo y el ajuste de Eon, con estas diferencias:

- Se saltan huevos.
- Se limita el nivel a `MAX_LEVEL`.
- Usa `Random() % 5`, así que la party puede variar entre intentos.
- Se aplica a entrenadores partner y rematches.
- Frontier/eReader/Trainer Hill quedan fuera.

Es una diferencia de balance grande. Si se busca una experiencia más cercana a expansion,
ponerlo en `FALSE`; si se busca Eon, dejarlo en `TRUE`.

### 4.2 Remapeo de items en entrenadores/Frontier: riesgo medio

Los datos convertidos usan items modernos:

- 24 Focus Band → Focus Sash.
- 5 Everstone → Eviolite.
- 1 Lucky Punch → Punching Glove.
- 1 Electirizer → Macho Brace.
- Frontier: 52 Focus Sash, 3 Loaded Dice, 1 Punching Glove, 1 Macho Brace, 2 Eviolite.
- Frontier Brains: 4 Focus Sash.

Eon en varios casos conservaba el efecto viejo, así que esto cambia el comportamiento real.
La alternativa sería volver a los IDs viejos y aceptar los efectos originales. La decisión
actual prioriza que los entrenadores usen el efecto que Eon quería.

### 4.3 Exp. All nuevo item: riesgo bajo-medio

- `ITEM_EXP_ALL` ocupa el ID 835 y `ITEMS_COUNT` pasa a 836.
- Es un key item con `ItemUseOutOfBattle_ExpAll`.
- El held Exp. Share sigue en su ID original.
- Falta probar que el menú de items, el registro, el guardado y el Exchange Corner manejen
  correctamente el nuevo ID.

### 4.4 DNS y paletas de batalla: riesgo bajo

- `DnsApplyFilters` ahora solo se ejecuta en overworld. En batalla se usa la transferencia
  normal de paletas, que es lo que Eon hacía de todos modos (no aplicaba filtro en combate).
- `LoadChosenBattleElement` ya usa `GetTimeBasedBattlePalette` en las dos rutas de carga.
- `GetTimeBasedBattlePalette` no cubre terrenos nuevos de expansion; usa la paleta base.

### 4.5 Detalles menores

- `trainers.h` se editó a mano aunque es generado; `trainers.party` está sincronizado, pero
  conviene regenerarlo con `trainerproc` y comparar.
- Las descripciones del Exchange Corner para los items viejos que ya no se usan pueden quedar
  como strings muertos; no afectan.
- `B_EON_EXP_ALL_MESSAGE` ya no existe; el mensaje de Eon es siempre el usado cuando Exp. All
  está activo.
- `I_EXP_SHARE_FLAG` usa el valor de `FLAG_EXP_SHARE` (0x494), ahora documentado en
  `include/constants/flags.h`.
- Los item balls duplicados de Eon se normalizaron: FieryPath, ShoalCave y Slateport usan
  `Common_EventScript_FindItem` con flags propias, y RusturfTunnel usa un item ball de Revive.
  Se eliminaron los 165 scripts de item balls de Eon que quedaron sin uso.

## 5. Decisiones que considero correctas

- Mantener `B_PHYSICAL_SPECIAL_SPLIT = GEN_LATEST` y resolver Eon con PSS condicional.
- `B_UPDATED_TYPE_MATCHUPS = GEN_5`.
- `B_WHITEOUT_MONEY = GEN_4`.
- Mantener modernos `B_UPDATED_MOVE_DATA`, `B_UPDATED_MOVE_TYPES`, `B_UPDATED_MOVE_FLAGS` y
  `B_UPDATED_ABILITY_DATA`.
- No importar los 95 movimientos experimentales.
- No mergear `hns/master`.
- Conservar los items modernos y los items viejos originales, en vez de duplicar nombres.
- No aceptar híbridos de stats sin efectos.
- No mergear `hns/master`.

## 6. Siguiente orden recomendado

1. Probar batalla normal, doble, Mega, Frontier, guardado y Exp. All.
2. Decidir si el escalado dinámico se queda en `TRUE` o se desactiva por defecto.
3. Revisar Battle Tent/Trainer Hill y wild held items si se quiere consistencia total.
4. Regenerar `trainers.h` con `trainerproc` y comparar.
5. Probar DNS en transiciones de batalla.
6. No mergear `hns/master` hasta cerrar 1–5.

## 7. Validación realizada

- Conteo de items, habilidades, movimientos, entrenadores y object graphics.
- JSON de mapas y marcadores de conflicto.
- Balance de llaves en los archivos modificados.
- Preprocesado de `src/data/items.h` y comprobación de que las ramas de items modernos/Exp. All
  expanden correctamente.
- Revisión de nombres de items usados por `trainers.party`.
- Revisión de las diferencias de movimientos contra `main-grafted` (95 restantes).
- Compilación completa con `make -j12` (modern); se generó un ROM de 32 MB.
- Segunda pasada de `make -j12` sin errores.
- Correcciones de build: TM Endure/Exp. All, CRLF en `.party`/`.s`, clases de facility,
  heal locations, conflicto DNS `TIME_*`, campo de graphics info y textos del Exchange Corner.
- Limpieza posterior: mapa duplicado de Cerulean Cave, item balls/flags duplicados, scripts de
  item balls sin uso y ruta de DNS en batalla.
- No se ejecutó el ROM ni se hicieron pruebas runtime.

## 8. Auditoría de duplicados y día/noche

### 8.1 Mapas y eventos

- Hay 573 mapas y 573 IDs únicos, con 496 layouts únicos.
- Se eliminó `CeruleanCave_1F`, que duplicaba `MAP_CERULEAN_CAVE_1F` y no estaba referenciado
  por `map_groups.json`. También se eliminó su layout huérfano y su `.include` de
  `data/event_scripts.s`.
- Los item balls duplicados de Eon se corrigieron:
  - FieryPath, ShoalCave y Slateport usan `Common_EventScript_FindItem` con flags propias.
  - RusturfTunnel usa un item ball de Revive con `FLAG_ITEM_RUSTURF_TUNNEL_REVIVE`.
  - Se eliminaron los 165 scripts de item balls de Eon que ya no se usan.
- Las flags `FLAG_UNUSED_0x470` y `FLAG_UNUSED_0x472` de Five Isle Meadow se renombraron a
  `FLAG_ITEM_FIVE_ISLE_MEADOW_MAX_ELIXIR` y `FLAG_ITEM_FIVE_ISLE_MEADOW_PP_MAX`.

### 8.2 Entrenadores y gráficos

- `trainers.party` tiene 864 entrenadores y todos los IDs son únicos. Brock, Misty, Surge,
  Erika, Koga, Sabrina, Blaine, Giovanni, Lorelei, Bruno, Agatha, Lance, Blue, Glacia y Drake
  existen una sola vez como entrenadores normales.
- Battle Frontier usa 300 slots. Hay 34 nombres repetidos dos veces con el mismo `monSet`
  (por ejemplo, `FRONTIER_TRAINER_JORDAN` y `FRONTIER_TRAINER_ARMANDO` son Brock). Son slots
  distintos, no IDs duplicados, pero sí contenido repetido. Se pueden deduplicar si se quiere
  que cada líder aparezca una sola vez.
- No hay funciones globales duplicadas; el linker las rechazaría. Tampoco hay object graphics
  ni trainer pics con el mismo nombre.

### 8.3 Día y noche

- Expansion ya tiene un sistema nativo de hora: `GetTimeOfDay`, `OW_TIMES_OF_DAY` y los
  rangos de `include/rtc.h`. Ese sistema controla evoluciones, encuentros y otros checks de
  gameplay, pero **no tiñe paletas**.
- `dns.c` de Eon no duplica ese sistema: añade la capa visual (filtros de paleta, luces de
  ventanas y paletas de batalla por hora). Son complementarios.
- DNS usa 6 fases visuales (midnight, dawn, day, sunset, nightfall, night) y expansion usa 4
  fases de gameplay (morning, day, evening, night). Se movió el enum de DNS a `dns.h` para que
  `battle_bg.c` use nombres en vez de números.
- Lo que se podría portar al sistema nativo: las tablas de filtros, la iluminación y las
  paletas de batalla, usando `GetTimeOfDay` para la fase amplia y `gLocalTime.hours/minutes`
  solo para las transiciones suaves. Unificarlo del todo obliga a elegir una sola frontera
  horaria; no se hizo porque cambiaría el aspecto de Eon.

### 8.4 Rendimiento de DNS

- `DnsApplyFilters` ya no se ejecuta en batalla; allí se usa la transferencia normal de
  paletas. Antes se recorrían 512 colores por frame para copiarlos sin filtrar.
- Durante el día, cuando el filtro es 0 y no hay luces activas, tampoco se reconstruye el
  buffer: se transfiere directamente `gPlttBufferFaded`.
- Si el mapa es una excepción de DNS, `DnsApplyFilters` sale inmediatamente.
- Se eliminaron `gCombatPalExceptions`, `IsCombat` y `DnsApplyFilterToColour`, que ya no se
  usaban.
- `LoadChosenBattleElement` ahora usa `GetTimeBasedBattlePalette` en ambas rutas de carga.
- El log de mGBA con `Bad BIOS Load` y `Bad memory Load` viene del HLE/BIOS y del nivel de
  log INFO; no es un error del ROM. Si vuelve a ir lento, poner el log de mGBA en `Fatal` o
  desactivarlo y cargar un BIOS real evita el spam de mensajes.
