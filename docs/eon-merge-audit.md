# EonEmerald + pokeemerald-expansion merge audit

Branch: `merge/1.15.3`
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
| 1.15.3 | Merged and built; commit pending |
| 1.16.4 | Pending |
| 1.17.2 | Pending |

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

## 4. Static counts

```text
trainers.party: 864
items: 881 (ITEMS_COUNT 881)
maps: 981 (including the imported FRLG map set)
abilities: 314 (ABILITIES_COUNT_EON 314)
object graphics: 401 (NUM_OBJ_EVENT_GFX 401)
trainer front pics: 168
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

## 6. Known risks

- The Eon dynamic trainer-level scaling (`B_EON_DYNAMIC_TRAINER_LEVELS`) remains
  enabled. It averages the player's party and can vary between attempts. Frontier,
  eReader and Trainer Hill are excluded.
- The 95 excluded move changes are a deliberate difference from the original Eon hack.
- 1.14 introduced new move, item and ability data. Data-only differences must be
  reviewed before the 1.14 line is considered final.
- The 1.15.3 FRLG map set is imported and builds, but its runtime behavior in an
  Emerald-mode save has not been tested yet.
- Save compatibility must be re-tested after each minor merge because of expansion's
  save-layout changes.
- `hns/master` must remain unmerged.

## 7. Next steps

1. Publish the 1.15.3 ROM artifact and commit the merge.
2. Merge 1.16.4 and 1.17.2 in order.
3. Run runtime tests: normal/double battles, Mega/Terastal/Dynamax, Battle Frontier,
   Trainer Hill, Battle Tent, saving/loading, Exp. All and DNS transitions.
