# EonEmerald + pokeemerald-expansion merge audit

Branch: `merge/1.17.2`
Date: 2026-11-17

This document records the current state of the incremental merge of
pokeemerald-expansion into EonEmerald. Expansion provides the architecture; EonEmerald
provides the content, data, balance and custom behavior. `hns/master` is intentionally not
merged.

## 1. Merge chain

| Expansion version | Status |
| --- | --- |
| 1.10.2 | Merged |
| 1.10.3 | Merged |
| 1.11.0 | Merged |
| 1.12.0 | Merged |
| 1.13.0 | Merged |
| 1.13.4 | Merged, built and committed (`fcfb6880eb`, learnsets `e2a3c4a93d`) |
| 1.14.4 | Merged, built and committed (`ccb1943df2`) |
| 1.15.3 | Merged, built and committed (`4f9f378a96`) |
| 1.16.4 | Merged, built and committed (`286344b0e5`) |
| 1.17.2 | Merged, built and committed (`3eff72f60d`) |

The project must always use the latest patch release of each minor line:
1.13.4, 1.14.4, 1.15.3, 1.16.4 and 1.17.2.

## 2. Standing decisions

### 2.1 Items

- `I_EON_ITEM_EFFECTS` defaults to `TRUE`. Both `TRUE` and `FALSE` builds must compile.
- With `TRUE`, the old item slots keep Eon's identities: Everstone acts as Eviolite,
  Focus Band as Focus Sash, Lucky Egg as Loaded Dice, Lucky Punch as Punching Glove,
  Mental Herb as Power Herb and Cleanse Tag as Covert Cloak.
- Modern items (Eviolite, Focus Sash, Loaded Dice, Punching Glove, Power Herb,
  Covert Cloak) remain available.
- Since 1.15.3, Eon-only items are assigned IDs 874-879. `ITEM_EXP_ALL` is 880
  and `ITEMS_COUNT` is 881. In 1.14.4 they were 855-860, 861 and 862 respectively.
- Electabuzz still evolves with `ITEM_ELECTIRIZER`.
- Battle Tent, Trainer Hill, Frontier and wild held-item tables use the modern item IDs
  that match Eon's intended effects.

### 2.2 Experience All

- `ITEM_EXP_ALL` is a key item that toggles party-wide experience.
- The held Exp. Share remains available through `I_EXP_SHARE_ITEM GEN_3`.
- `I_EXP_SHARE_FLAG` (`0x494`) remains the flag that enables the party effect.
- Eon's message and shared experience rules are used whenever Exp. All is active.

### 2.3 Moves

- Conflicting vanilla move data uses expansion 1.14 values.
- The 95 experimental Eon move changes (repurposed effects, unusual targets,
  unusual priorities and accuracy rewrites) are deliberately excluded.
- The 67 Eon changes whose effects did not change are kept.

### 2.4 Conditional physical/special split

- Eon's per-species category overrides are implemented by
  `GetMoveCategoryForSpecies`, `GetMoveCategoryForBattler` and
  `IsMovePhysicalForBattler`.
- The override uses the base species, so Mega Evolutions keep their species override.
- It uses the effective move type, so dynamic Ghost-type moves also receive the
  Ghost-type override.
- The effective category is used by the battle move menu, summary screen,
  move relearner and Pokédex.

### 2.5 Custom abilities

- `ABILITY_PERMAFROST` = 311 ("Frost Body"): Fire immunity, burn immunity,
  hail immunity, Clear Body-style stat-loss protection and a 1/3 contact freeze chance.
- `ABILITY_ROCK_BODY` = 312: absorbs Rock moves to restore 1/4 HP, hail immunity
  and Clear Body-style stat-loss protection.
- `ABILITY_GALVANIZED` = 313 ("Chrome Body"): 30% reduction to physical damage,
  hail immunity and Clear Body-style stat-loss protection.
- `ABILITIES_COUNT_EON` = 314.

### 2.6 Berries and Heart Scale

- Razz/Bluk/Nanab/Pinap/Wepear Berries cure infatuation.
- Belue/Durin/Pamtre/Watmel Berries restore lowered stats.
- Cornn/Magost/Nomel/Rabuta/Spelon Berries restore HP.
- Heart Scale heals a held Luvdisc for 1/8 max HP at the end of the turn.
- Since 1.14 these effects live in `src/battle_hold_effects.c` and
  `src/data/hold_effects.h`, not in the old `ItemBattleEffects` block in
  `src/battle_util.c`.

## 3. 1.14.4 integration notes

- 30 conflicts were resolved.
- Expansion 1.14 moved item battle effects from `src/battle_util.c` to the new
  `src/battle_hold_effects.c` and `src/data/hold_effects.h`. Eon's Heart Scale and
  Cure Attract effects were ported to that architecture.
- `include/constants/heal_locations.h` was removed because heal locations are JSON-based.
- Map constants are generated as an `enum`; stale `#define` output must be deleted before
  regenerating `include/constants/map_groups.h`.
- API changes handled during the merge:
  - `GetMoveType` is now a static inline in `include/move.h`.
  - `GetBattlerHoldEffect` takes one argument.
  - Move damage tracking uses Eon's per-battler PSS helper in
    `MoveDamageDataHpUpdate`.
  - New 1.14 config macros were restored (`B_CRITICAL_CAPTURE_IF_OWNED`,
    `B_PARTING_SHOT_SWITCH`, `B_BATON_PASS_TRAPPING`, `B_TRANSFORM_*`,
    `B_TAUNT_ME_FIRST`, `I_ORAS_DOWSING_*`).
  - Fishing config moved to `include/config/fishing.h`; Eon values were kept there.
  - The obsolete party-menu `MENU_MOVES` action was removed in favor of expansion's
    `MENU_SUB_MOVES` move-relearner flow.
- Eon's 53 battle-environment palettes and time-of-day battle palette selection are kept.
- The Eon region map layout, Sevii/Kanto heal locations and Kanto/Johto trainers are kept.

## 3.1 1.15.3 integration notes

- 91 conflicts were resolved.
- Object graphics: the Eon Kanto leaders keep IDs 242-254 and the Apricorn tree 255;
  expansion's FRLG graphics were shifted to 256-400. `NUM_OBJ_EVENT_GFX` is 401.
- `TrainerPicID` now includes the 13 Eon Kanto leader front pics (Brock through Blue)
  immediately before `TRAINER_PIC_FRONT_COUNT`. The front sprite table has 168 entries
  aligned with the enum, and the 13 Eon leader pics are appended in the same order.
- Facility classes: the Eon Kanto/Johto leaders use `0x77`-`0x8C`,
  `FACILITY_CLASSES_COUNT` is `0x8D`, and both lookup arrays were extended.
  `TRAINER_CLASS_LEADER_2` aliases `TRAINER_CLASS_LEADER`.
- Map data: expansion 1.15.3 introduced its FRLG map set. 419 FRLG map directories and
  335 FRLG/RS layouts were imported. The 9 duplicate FRLG map IDs that collide with
  existing Eon maps were removed from `map_groups.json` in favor of the Eon maps.
- LOCALIDs are generated by `mapjson` from each map's `local_id` fields. The old
  `.set LOCALID_*` lines were removed from map scripts, and Eon maps that still needed
  them received `local_id` entries (daycare man/mons, memorial man, Champion's room
  NPCs, Pokemon League nurse and guards).
- Learnsets: `src/data/pokemon/teachable_learnsets.h` is no longer tracked; it is
  regenerated from the JSON learnsets with `P_LEARNSET_HELPER_TEACHABLE TRUE`.
  The Python helpers were patched to open files as UTF-8 on Windows.
- Item battle effects were rebuilt from 1.15.3 in `src/battle_hold_effects.c` and
  `src/data/hold_effects.h`, with Eon's Heart Scale and Cure Attract effects ported
  to the new activation tables.
- Battle backgrounds: 1.15 moved the environment palette out of
  `struct BattleBackground`. Eon's time-of-day DNS palette selection was restored
  against the new `gBattleEnvironmentInfo[].palette` field.
- Doors use the 1.15 `INCGFX` table, the General flower animation uses the four-frame
  1.15 version, and duplicate object palette tags and flag definitions were removed.
- Legacy Eon call sites were adapted to 1.15 APIs:
  `GetMoveCategoryForBattler`/`IsMovePhysicalForBattler` take `enum BattlerId`,
  `IsBattlerTurnDamaged` takes `EXCLUDING_SUBSTITUTES`, confusion self-damage uses
  `gBattleStruct->unableToUseMove`, and battle context fields use `ctx.abilityDef`.
- Script-level repairs: restored `Text_CurrentTooFast`,
  `MoveTutor_AfterChooseBoxMon`, the Battle Frontier Exchange vitamin/Kiss Poster
  scripts and their shared confirmation text, and added the Pokemon constants include
  needed by Eon's Jirachi/Celebi mystery gifts.

## 3.2 1.16.4 integration notes

- 25 conflicts were resolved.
- `TrainerPicID` was reorganized: back pics now come first and front pics lost the
  `_FRONT_` prefix. Compatibility macros preserve the old `TRAINER_PIC_FRONT_*` and
  `TRAINER_PIC_BACK_*` names. The 13 Eon Kanto leader pics were added as
  `TRAINER_PIC_BROCK` through `TRAINER_PIC_BLUE` before `TRAINER_PIC_COUNT`.
- The old `gTrainerSprites` table was replaced by `gTrainerPicInfo[TRAINER_PIC_COUNT]`;
  the Eon leader entries were appended in the new format.
- Facility classes are now an enum. The Eon Kanto/Johto classes were appended before
  `FACILITY_CLASSES_COUNT`, both lookup arrays were extended, and
  `TRAINER_CLASS_LEADER_2` still aliases `TRAINER_CLASS_LEADER`.
- Object graphics became an enum. Eon's custom Kanto leader overworld graphics remain
  mapped in the Emerald table, expansion's FRLG sprites remain in the FRLG section,
  and `OBJ_EVENT_GFX_SURGE` is kept as an extra entry for Eon's Kanto maps.
- Berries now use `BERRY_ID_*` and the nested `struct Berry` layout. Eon's Gen 3 yield
  values were reapplied to the 43 affected berries.
- `DamageContext` now uses `abilities[]` instead of `abilityDef`. Eon's per-species
  physical/special split is preserved through `IsMovePhysicalForBattler`, and weather
  checks use the new `IsBattlerWeatherAffected(holdEffect, weather, flag)` signature.
- Stat-change move effects were removed from the effect enum. Eon AI checks now use
  `IsStatLoweringMove`, and the Eon abilities were added to the new
  `CanAbilityPreventStatLoss` in `src/battle_stat_change.c`.
- Rain Dish has a single merged case: expansion's rain heal plus Eon's Hydration-style
  status cure. Eon's custom healing abilities now use `BattleScript_AbilityHpHeal`.
- The obsolete party-menu move-relearner callbacks were removed in favor of the 1.16
  move-relearner flow; the category icon still uses Eon's per-species PSS.
- Script-menu strings moved to `src/data/script_menu.h`; their duplicate definitions
  were removed from `src/strings.c`. Expansion's missing text symbols were added back.
- Item IDs remain 874-879 for Eon items, `ITEM_EXP_ALL` 880 and `ITEMS_COUNT` 881.
- Eon's save-block `registeredLongItem` field and expansion's `dailySeed` now coexist,
  and time events use the new `gParties` storage.

## 3.3 1.17.2 integration notes

- 18 conflicts were resolved.
- Trainer party generation moved from `battle_main.c` to `battle_setup.c` and now uses
  `GenerateMonFromTrainerMon`. Eon's dynamic trainer-level scaling was ported into the
  new function, preserving the Frontier/eReader/Trainer Hill exclusions.
- The party generator now handles personality, nature, gender, shininess, IVs/EVs,
  abilities, held items, Dynamax and Terastal through `trainer_util.c`.
- `TrainerApproachMusic` became an enum with the new FRLG music values;
  `TRAINER_CLASS_LEADER_2` still aliases `TRAINER_CLASS_LEADER`.
- Berry confusion hold effects were migrated to `HOLD_EFFECT_CONFUSE_FLAVOR` with
  `secondaryId` flavors. Eon keeps its 1000 price and its healing/status behavior.
- Item data keeps Eon prices and descriptions; only the new flavor-based berry fields
  and other 1.17 architecture changes were adopted.
- Eon's `healBlock` checks use the new `volatiles.healBlockTimer` field.
- Rain Dish keeps the merged expansion heal plus Eon status cure, and Eon's custom
  healing abilities use `BattleScript_AbilityHpHeal`.
- The Eon per-species physical/special split is preserved in the new damage and
  accuracy helpers, including the Paradox abilities and Hustle accuracy check.
- The 1.17 stat-change refactor is adopted; Eon AI uses `IsStatLoweringMove` and the
  new `CanAbilityPreventStatLoss` covers the Eon abilities.
- The obsolete Eon `GetEggCyclesToSubtract` copy was removed; daycare uses its own
  implementation.
- Object graphics, item IDs (`ITEMS_COUNT 881`), map count and trainer pic count are
  unchanged from 1.16.4.

## 3.4 Post-1.17.2 fixes (2026-11-17)

### Map region and constant desync

- 14 non-FRLG maps were tagged `REGION_KANTO` inside Emerald groups: `UnusedContestHall1`,
  `MtPyre_6F`, `ShoalCave_LowTideLowerRoom`, `NewMauville_Inside`, `ScorchedSlab`,
  `AquaHideout_UnusedRubyMap1/2/3`, `MagmaHideout_2F_3R`, `SafariZone_Southwest`,
  `NavelRock_Up2`, `NavelRock_Up3`, `NavelRock_Top` and `NavelRock_Bottom`.
- `mapjson groups` filters maps whose region does not match the target game, but
  `generate_map_constants_text` still assigned their constants. Every later map was
  shifted by one: `InsideOfTruck` (40) loaded `SSTidalCorridor` (41).
- All 14 maps were changed to `REGION_HOENN` and `groups.inc`, `headers.inc`,
  `connections.inc`, `events.inc` and `include/constants/map_groups.h` were regenerated.
- Verified all 573 compiled maps: every `MAP_*` constant matches its group and index in
  `groups.inc`. `InsideOfTruck` is group 25, index 40
  (`MAP_INSIDE_OF_TRUCK = (40 | (25 << 8))`).
- The FRLG map set remains excluded: 981 maps in `map_groups.json`, 573 compiled after
  filtering the 408 `*_Frlg` maps.

### Day/night

- Expansion's `OW_ENABLE_DNS` and Eon's `src/dns.c` were both tinting the overworld,
  which double-darkened night maps. `OW_ENABLE_DNS` is now `FALSE` and Eon's DNS remains
  the only active system.
- `OW_BATTLE_ONLY_FORMS` is `FALSE`, matching `OW_FOLLOWERS_ENABLED FALSE`.

### Species and content trimming

- EonEmerald is a Generation 3 project. `P_GEN_4_POKEMON` through `P_GEN_9_POKEMON`,
  Mega Evolutions, Primal Reversions, Ultra Burst, Gigantamax, Tera, fusion forms,
  regional forms, Pikachu extra forms and `P_NEW_EVOS_IN_REGIONAL_DEX` are `FALSE`.
- `P_CROSS_GENERATION_EVOS` is `FALSE`, but `P_GEN_2_CROSS_EVOS` and
  `P_GEN_3_CROSS_EVOS` remain `TRUE` so the babies and evolutions introduced in
  Gen 2 and Gen 3 stay available.
- `NATIONAL_DEX_COUNT` now resolves to `NATIONAL_DEX_DEOXYS` (386).
- `OW_POKEMON_OBJECT_EVENTS` had to stay `TRUE`: expansion's `wild_encounter_ow.c`
  references `overworldShinyPaletteFemale` unconditionally, so disabling it requires a
  guard in expansion code. EonEmerald does not use species overworld graphics.
- Gen 4+ species data, cries and overworld graphics are no longer linked. No
  `Cry_*`/`gMonFrontPic_*` symbols exist for disabled species.

### Still compiled

- Items: `ITEMS_COUNT` is 881. Expansion 1.17.2 has no per-item enable config, so all
  item data and icons are linked, including Gen 4+ items.
- Moves: `MOVES_COUNT` is 848. Expansion 1.17.2 has no per-move enable config.
- FRLG: map, layout, tileset, trainer and map-script bulk data are excluded in Emerald
  builds. Only small shared FRLG-labelled helpers remain (for example
  `data/scripts/set_gym_trainers.inc`).

### Build result

- EWRAM: 227,284 B (86.70%)
- IWRAM: 28,380 B (86.61%)
- ROM used: 15,414,156 B (45.94%)
- Final `pokeemerald.gba`: 16,777,216 B
- Previous 1.17.2 build: 27,091,748 B used, 33,554,432 B file.

### Pending regressions

- Eon's regional Hoenn dex had 411 entries in `pre-main`; the merged expansion list has
  202. Restoring it is a data merge task, not a config change.
- The start menu refuses to open the Pokédex when
  `GetNationalPokedexCount(FLAG_GET_SEEN)` is 0. This guard also exists in `pre-main`.
  Changing the species count also invalidates old saves.
- `UpdateOverworldWildEncounter()` still runs every frame and linearly scans
  `gWildMonHeaders` even with `WE_OW_ENCOUNTERS FALSE`. Removing that cost requires
  touching expansion code.

## 4. Static counts

```text
trainers.party: 864
items: 881 (ITEMS_COUNT 881)
maps: 981 in map_groups.json; 573 compiled in Emerald builds (408 FRLG maps filtered)
abilities: 323 (ABILITIES_COUNT_EON 323)
object graphics: 389 (NUM_OBJ_EVENT_GFX 389)
trainer pics: 171 (front and back, including TRAINER_PIC_NONE)
conflict markers: 0
```

## 5. Runtime status

- 1.13.0 overworld runtime test passed.
- The TallGrass crash (`Bad memory Load16`) was fixed by restoring the six-frame
  `sPicTable_TallGrass`; all temporary diagnostics were removed.
- 1.13.4 builds successfully.
- 1.14.4 builds successfully with `I_EON_ITEM_EFFECTS TRUE`:
  - EWRAM: 228,996 B (87.36%)
  - IWRAM: 28,684 B (87.54%)
  - ROM: 25,229,652 B (75.19%)
- The 1.14.4 build with `I_EON_ITEM_EFFECTS FALSE` also links:
  - EWRAM: 228,996 B (87.36%)
  - IWRAM: 28,684 B (87.54%)
  - ROM: 25,229,544 B (75.19%)
- Battle, save, Frontier and DNS runtime tests are still pending for the 1.14 line.
- 1.15.3 builds successfully with `I_EON_ITEM_EFFECTS TRUE`:
  - EWRAM: 228,712 B (87.25%)
  - IWRAM: 28,504 B (86.99%)
  - ROM: 26,692,952 B (79.55%)
- The 1.15.3 build with `I_EON_ITEM_EFFECTS FALSE` also links:
  - EWRAM: 228,712 B (87.25%)
  - IWRAM: 28,504 B (86.99%)
  - ROM: 26,692,820 B (79.55%)
- Runtime tests for the 1.15 line are still pending.
- 1.16.4 builds successfully with `I_EON_ITEM_EFFECTS TRUE`:
  - EWRAM: 227,608 B (86.83%)
  - IWRAM: 28,384 B (86.62%)
  - ROM: 26,857,152 B (80.04%)
- The 1.16.4 build with `I_EON_ITEM_EFFECTS FALSE` also links:
  - EWRAM: 227,608 B (86.83%)
  - IWRAM: 28,384 B (86.62%)
  - ROM: 26,857,296 B (80.04%)
- Runtime tests for the 1.16 line are still pending.
- 1.17.2 builds successfully with `I_EON_ITEM_EFFECTS TRUE`:
  - EWRAM: 227,444 B (86.76%)
  - IWRAM: 28,380 B (86.61%)
  - ROM: 27,091,748 B (80.74%)
- The 1.17.2 build with `I_EON_ITEM_EFFECTS FALSE` also links:
  - EWRAM: 227,444 B (86.76%)
  - IWRAM: 28,380 B (86.61%)
  - ROM: 27,091,644 B (80.74%)
- Runtime tests for the 1.17 line are still pending.
- 1.17.2 post-merge fixes (map regions, single DNS, Gen 3-only species) build with
  `I_EON_ITEM_EFFECTS TRUE`:
  - EWRAM: 227,284 B (86.70%)
  - IWRAM: 28,380 B (86.61%)
  - ROM: 15,414,156 B (45.94%), final `pokeemerald.gba` 16,777,216 B
- Runtime tests for the post-merge fixes are still pending. A new save is required
  because the species count changed the Pokédex flag layout.

## 6. Known risks

- The Eon dynamic trainer-level scaling (`B_EON_DYNAMIC_TRAINER_LEVELS`) remains
  enabled. It averages the player's party and can vary between attempts. Frontier,
  eReader and Trainer Hill are excluded.
- The 95 excluded move changes are a deliberate difference from the original Eon hack.
- 1.14 introduced new move, item and ability data. Data-only differences must be
  reviewed before the 1.14 line is considered final.
- The imported FRLG map set builds, but its runtime behavior in an Emerald-mode save
  has not been tested yet.
- Save compatibility must be re-tested after each minor merge because of expansion's
  save-layout changes. 1.17 also refactored trainer party generation, so trainer
  parties should be spot-checked in-game.
- `hns/master` must remain unmerged.

## 7. Next steps

1. Publish the 1.17.2 ROM artifact and commit the merge.
2. Run runtime tests: normal/double battles, Mega/Terastal/Dynamax, Battle Frontier,
   Trainer Hill, Battle Tent, saving/loading, Exp. All and DNS transitions.
3. Review the 1.17.2 trainer party generation refactor in-game, especially the Eon
   dynamic trainer-level scaling.
