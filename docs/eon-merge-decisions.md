# EonEmerald + pokeemerald-expansion merge decisions

This file records decisions made while porting EonEmerald onto pokeemerald-expansion 1.10.2,
so they are explicit and reviewable.

## Trainers

- `src/data/trainers.party` is the source of truth (competitive party syntax).
- `COMPETITIVE_PARTY_SYNTAX` is `TRUE`; `trainerproc` generates `src/data/trainers.h`.
- `src/data/trainer_parties.h` is kept as the empty stub that expansion 1.10.2 includes from `src/data.c`.
- EonEmerald trainer data (864 trainers) was converted to this format instead of keeping the old split C format.

## Running indoors

- The map flag `gMapHeader.allowRunning` is preserved.
- `OW_RUNNING_INDOORS` keeps expansion's generation meaning.
- Added `OW_RUNNING_INDOORS_GEN_3`:
  - `TRUE`: running indoors is allowed even when `OW_RUNNING_INDOORS == GEN_3` (EonEmerald default).
  - `FALSE`: vanilla Gen 3 behavior; each map's `allow_running` flag is respected.
- `src/bike.c` uses both settings instead of removing the map check.

## Script command `checkpcspace`

- EonEmerald's `checkpcspace` command was kept.
- Expansion 1.10.2 uses opcode `0xE3` for `SCR_OP_DYNMULTICHOICE`.
- `checkpcspace` was moved to the next free opcode, `SCR_OP_CHECKPCSPACE = 0xE5`, and the macro/table were updated.

## Items

- EonEmerald custom items were added at the end of the item list to avoid renumbering expansion items.
- `ITEM_COMBAT_VEST` ("Bulwark Vest") and `ITEM_LINK_STONE` are separate items.
- Link Stone was also added as an alternative to Linking Cord for trade evolutions.
- Abandoned Ship room keys were restored as key items.

## Graphics

- Unown `front.png` files were restored from expansion because expansion's `src/data/graphics/pokemon.h` references them.
- EonEmerald's custom Pokémon graphics and palettes were kept where they conflicted with expansion's versions, except battle UI graphics that are tied to expansion's UI code.

## Time / RTC

- Both EonEmerald's real RTC call (`RtcCalcLocalTime`) and expansion's fake RTC (`FakeRtc_TickTimeForward`) are kept in `src/play_time.c`.

## Following Pokémon

- Expansion's follower system is present and configurable.
- `OW_FOLLOWERS_ENABLED` remains `FALSE` by default so EonEmerald's default experience prevails.
- Set it to `TRUE` to enable following Pokémon; `OW_POKEMON_OBJECT_EVENTS` is already enabled.

## Species data

- Real EonEmerald changes versus its pret base were ported into expansion's `species_info/gen_*_families.h` files
  (base stats, types, abilities, wild held items and related base data).
- `P_UPDATED_*` defaults are aligned with EonEmerald/Gen 3 behavior instead of expansion's Gen 9 defaults.
- EonEmerald's custom abilities are currently mapped as follows until their effects are ported:
  - `ABILITY_PERMAFROST` -> `ABILITY_FLASH_FIRE`
  - `ABILITY_ROCK_BODY` -> `ABILITY_STURDY`
  - `ABILITY_GALVANIZED` -> `ABILITY_FUR_COAT`
  The original names/effects are recorded here so they can be restored properly later.

## Completed ports

- 864 trainers and 863 parties converted from the old split C format to `trainers.party` / competitive syntax.
- EonEmerald custom items, Link Stone evolutions and Kanto trainer portraits.
- EonEmerald species base data (stats, types, abilities, wild held items) via diff against its pret base.
- Gen 3 level-up learnsets converted to `struct LevelUpMove` in `gen_3.h`.
- Common UI strings from EonEmerald overlaid onto expansion's `strings.c`.
- TM/HM and tutor compatibility ported through expansion's learnset helper:
  - Eon `TUTOR_MOVE_*` script ids converted to `MOVE_*`.
  - Eon TM/tutor compatibility stored in `tools/learnset_helpers/porymoves_files/custom.json`.
  - `teachable_learnsets.h` regenerated; Mew remains expansion's intentional special case.
- Hold-to-use second registered item QoL, including `registeredLongItem` in `SaveBlock1`, tap/hold detection and the Tap/Hold bag submenu.
- Missing Eon symbols restored: `gText_YouAlreadyHaveThis` and the Endure TM (`ITEM_TM_ENDURE`).
- Battle Frontier monster sets: all 882 Eon slots converted to expansion `TrainerMon`, with Eon held items and EV spreads.
- TM sales limited in shops like Eon: owned TMs/HMs show as sold out and use the single-purchase prompt.
- Eon/Gen 3 defaults for breeding, EV cap, TM literacy, legendary IVs and single-use TMs.

## Pending / review

- EonEmerald's `battle_util.c` custom ability behavior (Permafrost, Rock Body, Galvanized, Rain Dish/Hydration/Natural Cure tweaks) was written for the old engine and must be reimplemented against expansion's ability system, not copied directly.
- Shop/TM sale behavior (`POCKET_KEY_ITEMS`/berries sold-out checks, limited TM sales) needs adapting to expansion's rewritten shop code.
- EonEmerald evolutions other than Link Stone, Battle Frontier data, and remaining data tables where they differ.
- Full effects for the custom abilities currently aliased to Flash Fire / Sturdy / Fur Coat.
- Review default configs (`B_*`, `I_*`, `P_*`, `OW_*`) so EonEmerald remains the default experience while expansion features stay available.
- EonEmerald-only strings that expansion moved to other files (not added blindly to avoid duplicate labels).
- Graphical/data tweaks where expansion reorganized the target files (shop UI, battle background palettes, Frontier mons).
