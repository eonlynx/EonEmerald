# EonEmerald + pokeemerald-expansion merge audit

Branch: `merge/1.14.4`
Date: 2026-11-16

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
| 1.14.4 | Merge resolved; build validation in progress |
| 1.15.3 | Pending |
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
- Eon-only items are assigned IDs 855-860. `ITEM_EXP_ALL` is 861 and
  `ITEMS_COUNT` is 862.
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
- In 1.14 these effects live in `src/battle_hold_effects.c` and
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

## 4. Static counts

```text
trainers.party: 864
items: 862 (ITEMS_COUNT 862)
maps: 573
abilities: 314 (ABILITIES_COUNT_EON 314)
object graphics: 256 (NUM_OBJ_EVENT_GFX 256)
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
- The 1.14.4 build with `I_EON_ITEM_EFFECTS FALSE` is being validated.
- Battle, save, Frontier and DNS runtime tests are still pending for the 1.14 line.

## 6. Known risks

- The Eon dynamic trainer-level scaling (`B_EON_DYNAMIC_TRAINER_LEVELS`) remains
  enabled. It averages the player's party and can vary between attempts. Frontier,
  eReader and Trainer Hill are excluded.
- The 95 excluded move changes are a deliberate difference from the original Eon hack.
- 1.14 introduced new move, item and ability data. Data-only differences must be
  reviewed before the 1.14 line is considered final.
- Save compatibility must be re-tested after each minor merge because of expansion's
  save-layout changes.
- `hns/master` must remain unmerged.

## 7. Next steps

1. Complete the 1.14.4 build with `I_EON_ITEM_EFFECTS TRUE` and `FALSE`.
2. Commit 1.14.4 and regenerate generated files.
3. Merge 1.15.3, 1.16.4 and 1.17.2 in order.
4. Run runtime tests: normal/double battles, Mega/Terastal/Dynamax, Battle Frontier,
   Trainer Hill, Battle Tent, saving/loading, Exp. All and DNS transitions.
