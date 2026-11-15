# EonEmerald + pokeemerald-expansion merge decisions

This file records decisions made while porting EonEmerald onto pokeemerald-expansion 1.10.2,
so they are explicit and reviewable.

A full per-file inventory of the original 205 merge conflicts is in
[`docs/eon-merge-resolution.md`](eon-merge-resolution.md):

- 123 files resolved as `theirs` (expansion) - Eon changes may still need porting.
- 35 files resolved as `ours` (EonEmerald) - expansion changes may still need review.
- 24 deleted, 18 manually merged, 5 already identical.

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
- Eon shop behavior is restored: TMs/HMs, key items and berries are single-purchase.
  Owned items show as sold out; bag-owned items use `gText_YouAlreadyHaveThis`.
- Eon item and TM prices were ported for every item shared with Eon (325 price lines).
- Item configs were set to Eon/Gen 3 behavior: Gen 3 healing values, 100 EV vitamin cap,
  20% type-boost items, half sell value, held Exp. Share, non-key Escape Rope/fossils,
  Gen 3 repel/fishing rules and the old Premier Ball bonus.
- Eon Oran Berry heals 20 HP when held and Quick Claw has a 30% activation rate.
- Eon repurposed several old items into effects that expansion now has as separate modern
  items (Power Herb, Covert Cloak, Eviolite, Focus Sash, Loaded Dice, Leech/Boxing Glove).
  Those modern expansion items are kept instead of duplicating the effect on the old item.

## Battle configs

- Core battle mechanics were aligned with Eon's Gen 3 engine: critical hits, paralysis,
  confusion, multi-hit distribution, damage formulas, EXP gain, weather duration, balls,
  turn order, status timing and similar `B_*` settings.
- Whiteout money uses Eon's FRLG-style badge-based formula (`B_WHITEOUT_MONEY GEN_4`) rather
  than the Gen 3 half-money rule.
- `B_UPDATED_TYPE_MATCHUPS == GEN_5` reproduces Eon's type chart (including Fairy and
  Ghost/Dark vs Steel) and was verified against Eon's `gTypeEffectiveness` table.
- `B_PHYSICAL_SPECIAL_SPLIT` deliberately remains modern (`GEN_LATEST`) because Eon's
  own move table has per-move physical/special categories rather than Gen 3 type-based ones.
- `B_UPDATED_MOVE_DATA`, `B_UPDATED_MOVE_TYPES`, `B_UPDATED_MOVE_FLAGS`,
  `B_UPDATED_ABILITY_DATA`, `B_METRONOME_MOVES` and `B_NATURE_POWER_MOVES` remain modern
  until Eon's move/ability data is audited and ported move by move.

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
- EonEmerald's custom abilities now have real expansion IDs and effects (no aliases):
  - `ABILITY_PERMAFROST` (311, "Frost Body"): Fire immunity, burn immunity, hail immunity,
    Clear Body-style stat-loss protection, and a 1/3 contact freeze chance.
  - `ABILITY_ROCK_BODY` (312, "Rock Body"): absorbs Rock moves to restore 1/4 HP,
    hail immunity, and Clear Body-style stat-loss protection.
  - `ABILITY_GALVANIZED` (313, "Chrome Body"): 30% reduction to physical damage,
    hail immunity, and Clear Body-style stat-loss protection.
- `ABILITIES_COUNT_EON` is 314; all expansion ability arrays and the ROM header use it.

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
- Battle Frontier Brain sets: all seven facilities' silver/gold parties use Eon's species, items, IVs, natures, EVs and moves.
- Converted Eon trainer and Frontier data now uses the modern item IDs for Eon's old
  repurposed slots (Focus Sash, Eviolite, Punching Glove, Macho Brace, Loaded Dice) instead of
  the old item IDs.
- Battle Frontier exchange corner: Eon's expanded item lists and BP prices are active; the
  placeholder descriptions were filled in and the Exp. Share / Life Orb entries corrected.
- TM sales limited in shops like Eon: owned TMs/HMs show as sold out and use the single-purchase prompt.
- Eon/Gen 3 defaults for breeding, EV cap, TM literacy, legendary IVs and single-use TMs.
- Eon egg-hatch QoL (faster Flame Body/Magma Armor) and Poke Doll trainer-approach suppression.
- Real custom ability effects and IDs (Frost Body, Rock Body, Chrome Body) plus Eon's Rain Dish
  status cure and Natural Cure 1/32 end-turn healing.
- Eon ability buffs: Overgrow heals above half HP, Blaze burns on contact and blocks freeze,
  Torrent/Liquid Ooze heal in rain, Liquid Ooze absorbs Poison moves, and Thick Fat/Snow Warning
  grant Eon's hail healing and Dewgong/Seel status cure.
- Eon shop single-purchase rules for TMs/HMs, berries and key items.
- Eon item/TM prices and Gen 3 item config defaults.
- Eon held-item effects restored: Razz/Bluk/Nanab/Pinap/Wepear Berries cure infatuation,
  Belue/Durin/Pamtre/Watmel restore lowered stats, Cornn/Magost/Nomel/Rabuta/Spelon restore HP,
  and Heart Scale restores 1/8 HP to a held Luvdisc at the end of the turn.
- Eon Pickup distribution, including the rare Choice Band/Choice Specs/Choice Scarf
  and Leftovers finds at high levels.
- Gen 3 battle config sweep, verified Eon type chart (`B_UPDATED_TYPE_MATCHUPS GEN_5`), while
  keeping the per-move physical/special split.
- Curated Eon move-data batches: 93 moves use Eon's power, accuracy, PP and type values. This
  includes Absorb, Mega Drain, Aurora Beam, Dragon Breath, Rock Slide, Poison Tail, the
  Normal-to-coverage type changes such as Comet Punch/Fury Attack/Dizzy Punch/Twineedle/Spike
  Cannon, and the stat changes for effect-repurposed moves such as Cut, Crabhammer, Heat Wave,
  Horn Attack, Mega Punch, Strength, Take Down and Waterfall. Category, target, priority and
  effect reworks remain expansion-side. Bind/Clamp/Wrap/Submission now pin Eon's accuracy and
  PP instead of following the generation config.
- Eon conditional physical/special split applied in the damage path: elemental punches for
  Medicham/Hitmonchan/Electabuzz/Sneasel, Breloom Bullet Seed, Togetic/Clefable Tri Attack,
  Delcatty Uproar, Wigglytuff Hyper Voice, Sludge Bomb, Leaf Blade, Solar Beam, Powder Snow,
  Silver Wind, Air Cutter, Dive and Ghost moves for Eon's listed species. The battle move
  menu icon, Battle TV scoring, AI move checks, Counter/Mirror Coat tracking and category-based
  effects (Weak Armor, Toxic Debris, Jaboca/Rowap Berry, Hustle accuracy) use the same
  per-battler category. Summary, move-relearner and Pokédex icons use the species context too.
- Config switches for optional Eon behavior: `B_EON_DYNAMIC_TRAINER_LEVELS`,
  `B_EON_EXP_ALL_MESSAGE` and `I_EON_LEGACY_ITEM_EFFECTS`, all defaulting to the current
  EonEmerald behavior and documented in the config headers.
- Removed the unreferenced `data/scripts/change_deoxys_form.inc` dead script.
- Missing `gPokedexCaughtScreen_Pal` definition and valid icons for Eon room keys and Endure TM.
- Eon battle menu casing (`Fight`, `Type/`, `Wally`).
- Kanto gym leader / Elite Four overworld graphics restored for the preserved Eon maps
  (graphics IDs, pics, palettes, pic tables, graphics info, pointers and palette tags).
- Manual-merge regressions fixed:
  - Cut/Rock Smash/Strength/Waterfall/Dive check the party move and execute the field effect.
  - Battle Frontier tutors set `VAR_0x8005` to the correct move and Endure has its proper text.
  - Wattson gives the Basement Key again and Rustboro gives the Exp. Share.
  - `SwitchMonAbility` is implemented and registered for the Fallarbor ability switcher.
- Eon berry tree yields (higher min/max yields) are applied through the existing
  generation-selectable `YIELD_RATE` macro.
- HMs can be forgotten through the summary screen move deleter, matching Eon.
- Kanto/Sevii maps use `MUS_RG_SURF` for surfing, including map transitions and entering Surf.
- Eon day/night palette filtering (DNS) is active in the overworld and battle loops, and
  battle terrain backgrounds use the time-of-day palettes (day, morning, sunset, night).
- Eon's cloud weather effect only renders clouds over pond water and puddles.
- Reviewed the 35 `OURS` files: 34 are byte-identical Eon assets. Mauville City's script only
  differs by the already-added bag-full check when Wattson gives the Basement Key.
- Battle loss and prize-money messages use Eon's text and badge-based money values.
- Eon's snow-themed hail battle text is used for weather start, continuation, end and
  hail damage.
- Party menu QoL: nickname and move-relearner actions are available directly from the field
  party menu, SELECT starts a quick swap, and held items can be moved or swapped between
  party members from the item submenu.

## Open decisions / config

No high-priority missing references remain. The remaining choices are deliberately configurable
or intentionally excluded:

- **Trainer level scaling:** `B_EON_DYNAMIC_TRAINER_LEVELS` (default `TRUE`) enables Eon's
  player-party-average scaling for normal trainer battles. Frontier, eReader and Trainer Hill
  battles keep fixed levels.
- **Exp. All wording:** `B_EON_EXP_ALL_MESSAGE` (default `TRUE`) switches the Gen6-style
  Exp. Share team message to Eon's Exp. All text and shared EXP amount.
- **Old item slots:** `I_EON_LEGACY_ITEM_EFFECTS` (default `TRUE`) repurposes the old item
  slots: Mental Herb becomes Power Herb, Cleanse Tag becomes Covert Cloak, Everstone becomes
  Eviolite, Focus Band becomes Focus Sash, Lucky Egg becomes Loaded Dice and Lucky Punch becomes
  Boxing Glove. Modern items stay separate, so disable this only if you want the old slots to
  keep their original expansion effects.
- **Move data:** 69 shared moves still differ, but they are Eon's experimental effect, target
  and priority reworks (Charge/Volt Switch, damaging Flash/Water Sport/Spider Web, Arm Thrust
  priority 1, Memento/Teleport priority, weather-move targets, etc.). They are intentionally
  excluded; expansion keeps per-move PSS and modern effect implementations. The 93 safe
  power/accuracy/PP/type changes are ported, including the status-move accuracy buffs.
- **Ability data:** Eon's custom ability effects are implemented in code; modern
  `B_UPDATED_ABILITY_DATA` values remain intentionally.
- **Battle script/util effects:** reviewed at function/table level. The remaining Eon diff is
  old-engine refactoring plus the move reworks above; trainer money, whiteout, Pickup and the
  optional Eon Exp. All message are covered.
- **Battle Frontier:** the 882 monster sets, seven Brain parties and the exchange corner item
  lists, prices and descriptions are ported. No other referenced Eon Frontier data was found
  missing.
- **Eon-only strings and object graphics:** the 286 unreferenced Eon `gText_*` symbols are not
  needed unless restoring specific text; all map-referenced object-event graphics resolve.
- **`TryChangeDeoxysForm`:** the unreferenced dead script was removed.
- **m4a day/night music:** Eon's `MPlayStart` diff has two identical branches, so there is no
  behavior to port; DNS palette filtering is wired in.
- **`hns/master`:** not merged, per instruction.

## Static validation

```text
movimientos: 935
items: 836 / ITEMS_COUNT 836
habilidades: 314 / ABILITIES_COUNT_EON 314
trainers.party: 864
object gfx: 254 / NUM 254
JSON de mapas: válido
conflictos: 0
worktree: limpio
```
