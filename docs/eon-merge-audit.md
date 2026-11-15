# Auditoría del merge EonEmerald + expansion 1.10.2

Rama: `port/eon-features`
Último commit auditado: `a497578fdf`
Fecha: 2026-11-15

Esta auditoría revisa el merge completo desde el principio: qué está cerrado, qué queda
realmente pendiente y qué decisiones pueden ser discutibles o incorrectas. No sustituye a una
compilación ni a una prueba en emulador; toda la validación es estática.

## 1. Estado general

- Merge base: expansion 1.10.2 sobre pret `50d325f081`; EonEmerald injertado desde
  `main-grafted`.
- Conflictos originales: 205. Inventario por archivo en `docs/eon-merge-resolution.md`.
- `trainers.party` es la fuente de verdad; `trainers.h` está generado desde ella.
- No se ha hecho merge con `hns/master`, por decisión expresa.
- No se ha compilado el ROM ni ejecutado pruebas de batalla, guardado o mapas.

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

## 2. Trabajo cerrado

Se considera cerrado y validado estáticamente:

- Conversión de 864 entrenadores y 863 parties a `trainers.party`.
- Items custom de Eon: Bulwark Vest, Link Stone, llaves de la Abandoned Ship, TM Endure.
- Precios de items/TM, reglas de compra única, berries, Oran Berry a 20 HP y Quick Claw al 30%.
- Datos de especie de Eon, habilidades custom con IDs reales y efectos propios.
- Learnsets Gen 3, TM/HM/tutores y `teachable_learnsets.h`.
- 882 slots de Battle Frontier y 42 sets de los siete Frontier Brains.
- Exchange Corner: listas, precios, descripciones y correcciones de Exp. Share/Life Orb.
- Pickup de Eon, incluidos Choice items y Leftovers raros.
- PSS condicional por especie, aplicado al daño, IA, TV, efectos secundarios y UI.
- DNS día/noche, paletas de terreno por hora y efecto de nubes sobre agua.
- Textos de whiteout, granizo/nieve, Exp. All y derrota.
- Menú de equipo: nickname, move relearner, SELECT quick-swap y mover objetos.
- Revisión de los 35 archivos `OURS`: 34 idénticos a Eon; Mauville solo difiere por el
  chequeo de mochila llena de la Basement Key.
- Eliminación del script muerto `change_deoxys_form.inc`.

## 3. Pendiente real

### 3.1 Diferencias de movimientos: 69 movimientos

Quedan 69 movimientos compartidos con diferencias de potencia, precisión, PP, tipo, objetivo
o prioridad. No son un bloque homogéneo:

- Efectos repurposados: Charge → Volt Switch, Flash/Water Sport/Spider Web dañinos,
  Metal Sound, Spite, etc.
- Objetivos raros: clima, Haze, Perish Song, Petal Dance, Sing, Supersonic, etc.
- Prioridades raras: Arm Thrust +1, Memento +4, Transform +3, Zap Cannon -1, Teleport 0, etc.
- Precisión de movimientos que en expansion son “siempre aciertan” (0 → 100), que sería un
  cambio de comportamiento no deseado.

La decisión anterior fue no importar esos cambios por ser experimentales. Se mantiene, pero
está documentada como exclusión deliberada, no como trabajo terminado.

### 3.2 Validación ausente

- No hay compilación completa.
- No hay pruebas de batalla dobles, cambios de forma, Mega Evolución, Teracristal, Dynamax,
  Frontier, guardado/carga, DNS ni Exp. All.
- No se ha verificado el tamaño final del ROM ni la tabla de items/habilidades en runtime.

### 3.3 Consistencia pendiente de items

- `battle_tent.h` y `trainer_hill.h` siguen usando IDs viejos (`ITEM_FOCUS_BAND`,
  `ITEM_MENTAL_HERB`). No se remapearon como los entrenadores normales y Frontier.
- Con `I_EON_LEGACY_ITEM_EFFECTS = TRUE` esos IDs reciben efectos modernos; con `FALSE`
  conservan los efectos viejos. Es una inconsistencia deliberada pero conviene revisarla.
- Las descripciones del Exchange Corner solo son condicionales para algunos items
  (Everstone/Eviolite, Lucky Punch/Boxing Glove, Lucky Egg/Loaded Dice). Mental Herb usa el
  texto de Power Herb, correcto con legacy activo pero incorrecto si se desactiva.

### 3.4 Contenido no referenciado

- 286 símbolos `gText_*` de Eon no existen en `src/strings.c`; ninguno está referenciado por
  el árbol actual.
- Quedan assets de object-event/overworld que podrían existir solo en Eon y no estar
  referenciados por mapas; el chequeo actual cubre los que sí están referenciados.

## 4. Decisiones posiblemente incorrectas o de alto riesgo

### 4.1 Repurposado de items viejos: riesgo alto

`I_EON_LEGACY_ITEM_EFFECTS = TRUE` hace que los slots viejos tengan **efectos modernos**:

- Everstone → Eviolite
- Focus Band → Focus Sash
- Lucky Egg → Loaded Dice
- Lucky Punch → Boxing Glove
- Mental Herb → Power Herb
- Cleanse Tag → Covert Cloak

Problemas:

1. Los items modernos siguen existiendo en sus IDs modernos. Hay nombres duplicados y dos
   formas de obtener el mismo efecto.
2. Se pierden funciones originales que no tienen equivalente moderno:
   - Everstone ya no previene evolución.
   - Focus Band ya no da el 10% de sobrevivir.
   - Lucky Egg ya no aumenta EXP.
   - Mental Herb ya no cura efectos de atracción/ataduras.
   - Cleanse Tag ya no reduce encuentros.
   - Lucky Punch ya no boostea a Chansey.
3. Eon en realidad solo renombraba varios de estos items y conservaba el efecto viejo. Nuestra
   versión es una interpretación “moderna”, no una copia exacta de Eon.
4. Los held items salvajes y las tiendas que usan los IDs viejos cambian de efecto.

Recomendación: decidir explícitamente entre:

- **Opción A (actual):** slots viejos con efectos modernos, modernos también disponibles.
  Es funcional, pero pierde funciones antiguas y duplica nombres.
- **Opción B (más fiel a Eon):** slots viejos con nombres de Eon y efectos viejos; los items
  modernos quedan aparte. Conserva funciones, pero duplica nombres.
- **Opción C (swap completo):** intercambiar nombre/efecto entre slot viejo y moderno. Conserva
  todas las funciones y evita duplicados, pero exige remapear datos y descripciones.
- **Opción D:** volver a `I_EON_LEGACY_ITEM_EFFECTS = FALSE` y usar solo items modernos.

### 4.2 Datos de movimientos híbridos: riesgo alto

Se portaron 93 movimientos con potencia/precisión/PP/tipo de Eon. En varios de los 26 del
segundo lote Eon también cambiaba el efecto, pero nosotros no. Eso produce híbridos que no son
ni Eon ni expansion:

- Waterfall: 120 BP, pero sin el recoil de Double Edge de Eon.
- Strength: 100 BP Rock, pero sin las bajadas de Superpower.
- Mega Punch: 75 BP Fighting, pero sin Absorb.
- Cut: 80 BP Bug, pero sin flinch.
- Take Down: 80 BP Psychic, pero sin flinch.
- Blast Burn/Frenzy Plant/Hydro Cannon: 130 BP/100 acc, pero siguen siendo recarga.
- Sky Attack: 120 BP/100 acc/10 PP, pero sigue siendo de dos turnos.
- Heat Wave: 80 BP/100 acc, pero mantiene el secundario moderno.
- Crabhammer: 120 BP/85 acc/5 PP, pero mantiene High Critical de expansion.

Esto puede crear movimientos más fuertes o más débiles que en ambas versiones. Recomendación:
decidir si se portan también efectos/objetivos/prioridades para esos 26, o revertir sus stats
al valor de expansion y dejar solo los 67 movimientos cuyo efecto no cambió.

### 4.3 Exp. All key item: riesgo medio-alto

Se cambió `I_EXP_SHARE_ITEM` a `GEN_6` y `I_EXP_SHARE_FLAG` a `0x494`. Eso elimina el held
Exp. Share y lo convierte en key item Exp. All. Es fiel a la intención de Eon, pero:

- Cambia el flujo de Rustboro y del Exchange Corner.
- `B_EON_EXP_ALL_MESSAGE` controla a la vez el texto del mensaje y el nombre del item; no son
  dos decisiones independientes.
- Si se desactiva el modo Gen6, el mensaje de Eon queda inalcanzable.
- El flag `0x494` es el antiguo “Unused Flag”; funciona, pero está hardcodeado en config.

Recomendación: separar el nombre del item del texto del mensaje si se quiere configurar por
separado, y confirmar que el held Exp. Share ya no se necesita.

### 4.4 Remapeo de items en entrenadores/Frontier: riesgo medio

Se cambiaron datos de Eon a items modernos:

- 24 Focus Band → Focus Sash.
- 5 Everstone → Eviolite.
- 1 Lucky Punch → Punching Glove.
- 1 Electirizer → Macho Brace.
- Frontier: 52 Focus Sash, 3 Loaded Dice, 1 Punching Glove, 1 Macho Brace, 2 Eviolite.
- Frontier Brains: 4 Focus Sash.

El cambio de Electirizer a Macho Brace parece correcto (la tabla de Eon lo usaba como Macho
Brace). Los demás cambian el comportamiento real respecto a Eon, porque Eon conservaba el
efecto viejo en la mayoría. Si se busca fidelidad estricta, habría que usar los IDs viejos y
decidir qué efecto tienen.

### 4.5 Escalado dinámico de niveles: riesgo medio

`B_EON_DYNAMIC_TRAINER_LEVELS = TRUE` implementa el promedio del equipo y el ajuste de Eon.
Diferencias con el código original:

- Se saltan huevos.
- Se limita el nivel a `MAX_LEVEL`.
- Se usa `Random() % 5`, por lo que la party puede variar entre intentos.
- Se aplica también a entrenadores partner y rematches.
- Frontier/eReader/Trainer Hill quedan fuera.

No se ha probado en runtime. Es fácil que el nivel efectivo de algunos entrenadores quede por
debajo o por encima de lo esperado.

### 4.6 PSS condicional: resuelto, pero con matices

Se corrigió para usar la especie base (Megas incluidas) y el tipo efectivo (movimientos Ghost
dinámicos). Aun así:

- La lista de especies es manual y no cubre formas regionales nuevas que Eon no tenía.
- El Pokédex usa `sPokedexListItem->dexNum`; es correcto para la especie seleccionada, pero
  no hay contexto de batalla.
- La UI sigue mostrando la categoría base en contextos que no pasan especie.

No lo considero un error, solo una limitación conocida.

### 4.7 DNS y paletas de batalla: riesgo bajo-medio

- `DnsApplyFilters` se llama en overworld y batalla. Es fiel a Eon.
- La paleta de terreno por hora se aplica en `DrawMainBattleBackground`, pero
  `LoadChosenBattleElement` todavía carga la paleta base en algunos flujos de entrada a
  batalla. Si se llama después, puede sobrescribir el efecto horario. Conviene probarlo en
  transiciones de batalla.
- `GetTimeBasedBattlePalette` no cubre terrenos nuevos de expansion (soaring, snow, etc.);
  usa la paleta base. Es aceptable, pero no es “Eon para todos los terrenos”.

### 4.8 Exchange Corner y descripciones condicionales: riesgo bajo

- Las descripciones de Everstone/Eviolite, Lucky Punch/Boxing Glove y Lucky Egg/Loaded Dice
  son condicionales.
- Mental Herb usa el texto de Power Herb; correcto con legacy activo, incorrecto con legacy
  desactivado.
- `ITEM_EXP_SHARE` pasó a key item; la descripción del Exchange Corner se actualizó, pero
  otros textos o scripts podrían seguir asumiendo held item.

### 4.9 `trainers.h` editado a mano: riesgo bajo

`trainers.h` es generado por `trainerproc` desde `trainers.party`. Se editó a mano para
sincronizar los items modernos. La fuente (`trainers.party`) también se actualizó, así que una
regeneración debería producir el mismo resultado, pero conviene ejecutar el generador y
comparar antes de confiar en la edición manual.

## 5. Decisiones que considero correctas

- Mantener `B_PHYSICAL_SPECIAL_SPLIT = GEN_LATEST` y resolver los casos de Eon con PSS
  condicional, en vez de volver a categorías por tipo.
- `B_UPDATED_TYPE_MATCHUPS = GEN_5` para reproducir el chart de Eon.
- `B_WHITEOUT_MONEY = GEN_4` por el sistema de dinero por medallas de Eon.
- Mantener `B_UPDATED_MOVE_DATA`, `B_UPDATED_MOVE_TYPES`, `B_UPDATED_MOVE_FLAGS` y
  `B_UPDATED_ABILITY_DATA` modernos, portando casos concretos en lugar de aceptar todo el
  table de Eon.
- No importar los 69 cambios de efecto/objetivo/prioridad sin una revisión específica.
- No mergear `hns/master` todavía.
- Haber conservado los 35 archivos `OURS` de Eon y revisado que 34 son idénticos.

## 6. Recomendaciones concretas, en orden

1. **Decidir la política de items viejos** (sección 4.1). Es la decisión con más impacto
   visible y la que puede romper funciones clásicas.
2. **Decidir si se aceptan los híbridos de movimientos** (sección 4.2) o se revierten los 26
   stats de movimientos cuyo efecto no se portó.
3. **Compilar y probar**: batalla normal, doble, Mega, Frontier, guardado, Exp. All, DNS,
   nubes y party menu.
4. **Revisar Battle Tent/Trainer Hill** y unificar los IDs de items si se desea.
5. **Opcional:** separar el flag de Exp. All en “item” y “mensaje”.
6. **Opcional:** añadir los 286 strings no referenciados solo si se restaura texto de Eon.
7. **No** mergear `hns/master` hasta cerrar 1–3.

## 7. Validación realizada en esta auditoría

- Conteo de items, habilidades, movimientos, entrenadores y object graphics.
- JSON de mapas y marcadores de conflicto.
- Balance de llaves en los archivos modificados.
- Preprocesado de `src/data/items.h` con los flags de Eon activos para comprobar que las
  ramas `#if` expanden correctamente.
- Revisión de nombres de items usados por `trainers.party` (los alias `Kings Rock` y `Stick`
  resuelven correctamente a `ITEM_KINGS_ROCK` e `ITEM_STICK`).
- Revisión de las diferencias de movimientos contra `main-grafted` (69 restantes).
- No se compiló ni se ejecutó el ROM.
