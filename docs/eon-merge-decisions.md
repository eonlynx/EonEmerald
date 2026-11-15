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
- Gen 3 battle config sweep, verified Eon type chart (`B_UPDATED_TYPE_MATCHUPS GEN_5`), while
  keeping the per-move physical/special split.
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
- Battle loss and prize-money messages use Eon's text and badge-based money values.
- Party menu QoL: nickname and move-relearner actions are available directly from the field
  party menu, and SELECT starts a quick swap from the field party menu.

## Pending / review

### High priority

- **Eon move data audit.** 351 shared moves still differ from Eon in power, accuracy, PP, type,
  category, target or priority (e.g. Eon's buffed Absorb/Aurora Beam/Dragon Breath and its
  per-move PSS assignments). Do not blindly overwrite expansion data; review in batches because
  Eon also contains quirks such as Arm Thrust priority 1 and special Fire Punch.
- **Eon conditional PSS.** Eon's `src/pokemon.c` changes move categories for specific species
  (Breloom/Bullet Seed, Togetic/Clefable/Tri Attack, several Ghost users, Hitmonchan/Medicham
  elemental punches, etc.). Expansion has no per-species category override; decide whether to
  implement it in the damage-calc path.
- **Remaining `battle_script_commands.c` / `battle_util.c` Eon effects** not covered by the
  custom abilities: move effects, item effects, weather damage exceptions, EXP distribution,
  trainer money and Pickup tables.
- **`battle_main.c` Eon trainer party generation and turn-order changes.** Verify the converted
  `trainers.party` data already covers Eon's party construction rules; port anything missing.

### Medium priority

- **Eon ability-data flags audit** (`B_UPDATED_ABILITY_DATA` still modern). The old engine's
  Overgrow/Blaze/Torrent/Liquid Ooze/Thick Fat/Snow Warning buffs are now ported.
- **Eon battle messages** still pending: the Exp. All message and Eon's snow/hail wording.
  The whiteout defeat/prize-money battle text is ported.
- **Eon party-menu QoL leftover:** the move-held-item action is still pending; nickname,
  move-relearner and SELECT quick-swap are ported.
- **Eon cloud-over-water weather effect** is still not wired in; the snowflake timer fix was
  already present in expansion.
- **Eon pickup table** (Choice items/Leftovers as rare Pickup finds) remains to be reviewed
  separately.
- **Battle Frontier exchange corner / reward prices** and any remaining Frontier data beyond
  the 882 monster sets and the seven Brain parties.
- **Eon-only strings.** 286 Eon `gText_*` symbols are absent from current `src/strings.c`, but
  none are referenced by the current tree. Add only when restoring specific Eon text.
- **Object-event and overworld graphics data** from the `THEIRS` list: Kanto leader gfx are
  restored and all map-referenced `OBJ_EVENT_GFX_*` symbols now resolve. Continue auditing
  custom follower/overworld assets that may only be referenced from C code.
- **`TryChangeDeoxysForm`** is referenced only by `data/scripts/change_deoxys_form.inc`, which is
  not included by `data/event_scripts.s`. The special was also absent from Eon's source, so the
  file remains dead unless it is wired into a map later.

### Lower priority

- **Review the 35 `OURS` files** (mostly graphics, map scripts and assets) for expansion-side
  changes that should be merged while keeping Eon assets.
- **`m4a.c` / DNS day-night music.** Eon's m4a diff has identical branches and `src/dns.c` is
  currently unused dead code, so it was not wired in.
- **Eon repurposed old items.** Effects like Power Herb, Covert Cloak, Eviolite, Focus Sash and
  Loaded Dice already exist as separate expansion items; decide whether old items should also be
  renamed/repurposed instead of only keeping the modern versions.
- **`hns/master` merge** once the current port branch is stable.
