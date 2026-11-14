# EonEmerald merge conflict resolution inventory

> Status note: this file records the original per-file resolution direction.
> Current completion status and the live pending-work list are tracked in
> [`docs/eon-merge-decisions.md`](eon-merge-decisions.md).

Merge commit: `af2c7595cd` (`expansion/1.10.2`).

Git semantics in that merge:

- `ours` = EonEmerald (`main-grafted`)
- `theirs` = pokeemerald-expansion 1.10.2

So `OURS` below means expansion-side changes still need to be reviewed/applied where relevant, and `THEIRS` means EonEmerald-side changes still need to be ported where relevant.

## Summary

| Resolution | Files |
|---|---:|
| THEIRS | 123 |
| OURS | 35 |
| DELETED | 24 |
| MANUAL | 18 |
| SAME | 5 |
| **Total** | **205** |

## Files resolved as OURS (EonEmerald) - review expansion-side changes

- `data/maps/MauvilleCity/scripts.inc` (Eon diff 1, expansion diff 1)
- `graphics/items/icon_palettes/bug_tm_hm.pal` (Eon diff 19, expansion diff 19)
- `graphics/items/icon_palettes/fairy_tm_hm.pal` (Eon diff 19, expansion diff 19)
- `graphics/items/icons/absorb_bulb.png` (Eon diff 0, expansion diff 0)
- `graphics/items/icons/air_balloon.png` (Eon diff 0, expansion diff 0)
- `graphics/items/icons/cell_battery.png` (Eon diff 0, expansion diff 0)
- `graphics/items/icons/choice_scarf.png` (Eon diff 0, expansion diff 0)
- `graphics/items/icons/choice_specs.png` (Eon diff 0, expansion diff 0)
- `graphics/items/icons/electirizer.png` (Eon diff 0, expansion diff 0)
- `graphics/items/icons/eviolite.png` (Eon diff 0, expansion diff 0)
- `graphics/items/icons/focus_sash.png` (Eon diff 0, expansion diff 0)
- `graphics/items/icons/life_orb.png` (Eon diff 0, expansion diff 0)
- `graphics/items/icons/magmarizer.png` (Eon diff 0, expansion diff 0)
- `graphics/items/icons/muscle_band.png` (Eon diff 0, expansion diff 0)
- `graphics/items/icons/protective_pads.png` (Eon diff 0, expansion diff 0)
- `graphics/items/icons/toxic_orb.png` (Eon diff 0, expansion diff 0)
- `graphics/items/icons/wise_glasses.png` (Eon diff 0, expansion diff 0)
- `graphics/pokemon/aerodactyl/mega/front.png` (Eon diff 0, expansion diff 0)
- `graphics/pokemon/ariados/anim_front.png` (Eon diff 0, expansion diff 0)
- `graphics/pokemon/bulbasaur/icon.png` (Eon diff 0, expansion diff 0)
- `graphics/pokemon/combusken/back.png` (Eon diff 0, expansion diff 0)
- `graphics/pokemon/combusken/shiny.pal` (Eon diff 32, expansion diff 32)
- `graphics/pokemon/deoxys/anim_front.png` (Eon diff 0, expansion diff 0)
- `graphics/pokemon/deoxys/normal.pal` (Eon diff 30, expansion diff 30)
- `graphics/pokemon/deoxys/shiny.pal` (Eon diff 30, expansion diff 30)
- `graphics/pokemon/deoxys/speed/anim_front.png` (Eon diff 0, expansion diff 0)
- `graphics/pokemon/deoxys/speed/normal.pal` (Eon diff 19, expansion diff 19)
- `graphics/pokemon/ledian/anim_front.png` (Eon diff 0, expansion diff 0)
- `graphics/pokemon/mawile/footprint.png` (Eon diff 0, expansion diff 0)
- `graphics/pokemon/question_mark/circled/anim_front.png` (Eon diff 0, expansion diff 0)
- `graphics/pokemon/question_mark/circled/back.png` (Eon diff 0, expansion diff 0)
- `graphics/pokemon/question_mark/footprint.png` (Eon diff 0, expansion diff 0)
- `graphics/pokemon/salamence/anim_front.png` (Eon diff 0, expansion diff 0)
- `graphics/pokemon/taillow/anim_front.png` (Eon diff 0, expansion diff 0)
- `graphics/types/fairy.png` (Eon diff 0, expansion diff 0)

## Files resolved as THEIRS (expansion) - port EonEmerald changes

- `asm/macros/movement.inc` (Eon diff 2, expansion diff 4)
- `data/battle_anim_scripts.s` (Eon diff 424, expansion diff 41853)
- `data/battle_scripts_1.s` (Eon diff 375, expansion diff 12025)
- `data/scripts/item_ball_scripts.inc` (Eon diff 8, expansion diff 661)
- `data/specials.inc` (Eon diff 1, expansion diff 21)
- `data/text/contest_strings.inc` (Eon diff 104, expansion diff 458)
- `graphics/battle_anims/sprites/substitute.png` (Eon diff 0, expansion diff 0)
- `graphics/battle_anims/sprites/substitute_back.png` (Eon diff 0, expansion diff 0)
- `graphics/battle_interface/status.png` (Eon diff 0, expansion diff 0)
- `graphics/battle_interface/status2.png` (Eon diff 0, expansion diff 0)
- `graphics/battle_interface/status3.png` (Eon diff 0, expansion diff 0)
- `graphics/battle_interface/status4.png` (Eon diff 0, expansion diff 0)
- `graphics/fonts/latin_narrow.png` (Eon diff 0, expansion diff 0)
- `graphics/fonts/latin_normal.png` (Eon diff 0, expansion diff 0)
- `graphics/fonts/latin_small.png` (Eon diff 0, expansion diff 0)
- `graphics/interface/menu_info.png` (Eon diff 0, expansion diff 0)
- `graphics/interface/status_icons.png` (Eon diff 0, expansion diff 0)
- `graphics/pokemon/unown/b/front.png` (Eon diff 0, expansion diff 0)
- `graphics/pokemon/unown/c/front.png` (Eon diff 0, expansion diff 0)
- `graphics/pokemon/unown/d/front.png` (Eon diff 0, expansion diff 0)
- `graphics/pokemon/unown/e/front.png` (Eon diff 0, expansion diff 0)
- `graphics/pokemon/unown/f/front.png` (Eon diff 0, expansion diff 0)
- `graphics/pokemon/unown/g/front.png` (Eon diff 0, expansion diff 0)
- `graphics/pokemon/unown/h/front.png` (Eon diff 0, expansion diff 0)
- `graphics/pokemon/unown/i/front.png` (Eon diff 0, expansion diff 0)
- `graphics/pokemon/unown/j/front.png` (Eon diff 0, expansion diff 0)
- `graphics/pokemon/unown/k/front.png` (Eon diff 0, expansion diff 0)
- `graphics/pokemon/unown/l/front.png` (Eon diff 0, expansion diff 0)
- `graphics/pokemon/unown/m/front.png` (Eon diff 0, expansion diff 0)
- `graphics/pokemon/unown/n/front.png` (Eon diff 0, expansion diff 0)
- `graphics/pokemon/unown/o/front.png` (Eon diff 0, expansion diff 0)
- `graphics/pokemon/unown/p/front.png` (Eon diff 0, expansion diff 0)
- `graphics/pokemon/unown/q/front.png` (Eon diff 0, expansion diff 0)
- `graphics/pokemon/unown/r/front.png` (Eon diff 0, expansion diff 0)
- `graphics/pokemon/unown/s/front.png` (Eon diff 0, expansion diff 0)
- `graphics/pokemon/unown/t/front.png` (Eon diff 0, expansion diff 0)
- `graphics/pokemon/unown/u/front.png` (Eon diff 0, expansion diff 0)
- `graphics/pokemon/unown/v/front.png` (Eon diff 0, expansion diff 0)
- `graphics/pokemon/unown/w/front.png` (Eon diff 0, expansion diff 0)
- `graphics/pokemon/unown/x/front.png` (Eon diff 0, expansion diff 0)
- `graphics/pokemon/unown/y/front.png` (Eon diff 0, expansion diff 0)
- `graphics/pokemon/unown/z/front.png` (Eon diff 0, expansion diff 0)
- `include/battle.h` (Eon diff 16, expansion diff 757)
- `include/battle_main.h` (Eon diff 3, expansion diff 70)
- `include/battle_scripts.h` (Eon diff 9, expansion diff 659)
- `include/battle_tower.h` (Eon diff 8, expansion diff 27)
- `include/battle_util.h` (Eon diff 3, expansion diff 358)
- `include/constants/abilities.h` (Eon diff 10, expansion diff 265)
- `include/constants/battle.h` (Eon diff 6, expansion diff 409)
- `include/constants/battle_move_effects.h` (Eon diff 4, expansion diff 566)
- `include/constants/battle_string_ids.h` (Eon diff 5, expansion diff 1221)
- `include/constants/event_object_movement.h` (Eon diff 2, expansion diff 17)
- `include/constants/event_objects.h` (Eon diff 41, expansion diff 134)
- `include/constants/heal_locations.h` (Eon diff 13, expansion diff 1)
- `include/constants/hold_effects.h` (Eon diff 17, expansion diff 234)
- `include/constants/item_effects.h` (Eon diff 3, expansion diff 30)
- `include/constants/items.h` (Eon diff 34, expansion diff 1451)
- `include/constants/party_menu.h` (Eon diff 8, expansion diff 46)
- `include/constants/pokemon.h` (Eon diff 8, expansion diff 237)
- `include/constants/trainers.h` (Eon diff 69, expansion diff 12)
- `include/data.h` (Eon diff 2, expansion diff 251)
- `include/global.h` (Eon diff 3, expansion diff 169)
- `include/graphics.h` (Eon diff 100, expansion diff 5089)
- `include/item_use.h` (Eon diff 2, expansion diff 39)
- `include/overworld.h` (Eon diff 1, expansion diff 11)
- `include/party_menu.h` (Eon diff 3, expansion diff 19)
- `include/pokemon.h` (Eon diff 7, expansion diff 661)
- `include/strings.h` (Eon diff 76, expansion diff 413)
- `src/battle_ai_switch_items.c` (Eon diff 39, expansion diff 2423)
- `src/battle_anim.c` (Eon diff 56, expansion diff 535)
- `src/battle_bg.c` (Eon diff 172, expansion diff 178)
- `src/battle_controller_player.c` (Eon diff 154, expansion diff 3220)
- `src/battle_gfx_sfx_util.c` (Eon diff 27, expansion diff 865)
- `src/battle_main.c` (Eon diff 252, expansion diff 3967)
- `src/battle_message.c` (Eon diff 128, expansion diff 3020)
- `src/battle_pyramid.c` (Eon diff 40, expansion diff 256)
- `src/battle_script_commands.c` (Eon diff 642, expansion diff 18361)
- `src/battle_tower.c` (Eon diff 340, expansion diff 566)
- `src/battle_tv.c` (Eon diff 4, expansion diff 315)
- `src/battle_util.c` (Eon diff 529, expansion diff 13254)
- `src/berry.c` (Eon diff 168, expansion diff 2032)
- `src/data/battle_frontier/battle_frontier_mons.h` (Eon diff 3432, expansion diff 3590)
- `src/data/graphics/items.h` (Eon diff 49, expansion diff 2109)
- `src/data/graphics/pokemon.h` (Eon diff 830, expansion diff 29561)
- `src/data/item_icon_table.h` (Eon diff 36, expansion diff 407)
- `src/data/items.h` (Eon diff 1017, expansion diff 15028)
- `src/data/object_events/object_event_graphics.h` (Eon diff 28, expansion diff 192)
- `src/data/object_events/object_event_graphics_info.h` (Eon diff 334, expansion diff 579)
- `src/data/object_events/object_event_graphics_info_pointers.h` (Eon diff 52, expansion diff 10)
- `src/data/object_events/object_event_pic_tables.h` (Eon diff 305, expansion diff 1653)
- `src/data/party_menu.h` (Eon diff 17, expansion diff 118)
- `src/data/pokemon/item_effects.h` (Eon diff 37, expansion diff 318)
- `src/data/pokemon/species_info.h` (Eon diff 2088, expansion diff 11916)
- `src/data/trainer_parties.h` (Eon diff 4179, expansion diff 12435)
- `src/data/trainers.h` (Eon diff 3729, expansion diff 45460)
- `src/data/union_room.h` (Eon diff 1, expansion diff 35)
- `src/daycare.c` (Eon diff 66, expansion diff 515)
- `src/egg_hatch.c` (Eon diff 4, expansion diff 37)
- `src/event_object_movement.c` (Eon diff 19, expansion diff 3216)
- `src/field_control_avatar.c` (Eon diff 50, expansion diff 252)
- `src/field_effect.c` (Eon diff 70, expansion diff 148)
- `src/field_player_avatar.c` (Eon diff 67, expansion diff 625)
- `src/field_specials.c` (Eon diff 111, expansion diff 329)
- `src/field_weather.c` (Eon diff 6, expansion diff 181)
- `src/field_weather_effect.c` (Eon diff 91, expansion diff 37)
- `src/frontier_util.c` (Eon diff 169, expansion diff 543)
- `src/graphics.c` (Eon diff 10, expansion diff 612)
- `src/item_menu.c` (Eon diff 108, expansion diff 130)
- `src/item_use.c` (Eon diff 64, expansion diff 634)
- `src/m4a.c` (Eon diff 61, expansion diff 1)
- `src/menu.c` (Eon diff 1, expansion diff 190)
- `src/move_relearner.c` (Eon diff 21, expansion diff 112)
- `src/overworld.c` (Eon diff 142, expansion diff 332)
- `src/party_menu.c` (Eon diff 282, expansion diff 2166)
- `src/pokemon.c` (Eon diff 485, expansion diff 6557)
- `src/pokemon_icon.c` (Eon diff 168, expansion diff 1016)
- `src/pokemon_storage_system.c` (Eon diff 45, expansion diff 395)
- `src/pokemon_summary_screen.c` (Eon diff 231, expansion diff 662)
- `src/pokenav_menu_handler_gfx.c` (Eon diff 55, expansion diff 36)
- `src/shop.c` (Eon diff 44, expansion diff 88)
- `src/strings.c` (Eon diff 1824, expansion diff 364)
- `src/trainer_see.c` (Eon diff 18, expansion diff 219)
- `src/wild_encounter.c` (Eon diff 31, expansion diff 349)

## Files manually merged

- `.gitignore` (Eon diff 2, expansion diff 7)
- `README.md` (Eon diff 24, expansion diff 201)
- `asm/macros/event.inc` (Eon diff 7, expansion diff 972)
- `data/maps/AbandonedShip_Rooms2_1F/map.json` (Eon diff 23, expansion diff 4)
- `data/maps/BattleFrontier_Lounge7/scripts.inc` (Eon diff 44, expansion diff 24)
- `data/maps/PetalburgWoods/map.json` (Eon diff 77, expansion diff 16)
- `data/maps/Route109/map.json` (Eon diff 48, expansion diff 8)
- `data/maps/Route115/map.json` (Eon diff 98, expansion diff 24)
- `data/maps/Route119_WeatherInstitute_2F/scripts.inc` (Eon diff 2, expansion diff 6)
- `data/maps/Route127/map.json` (Eon diff 37, expansion diff 12)
- `data/maps/Route133/map.json` (Eon diff 64, expansion diff 12)
- `data/maps/RusturfTunnel/map.json` (Eon diff 46, expansion diff 8)
- `data/script_cmd_table.inc` (Eon diff 1, expansion diff 473)
- `data/scripts/field_move_scripts.inc` (Eon diff 179, expansion diff 183)
- `data/scripts/obtain_item.inc` (Eon diff 79, expansion diff 34)
- `graphics_file_rules.mk` (Eon diff 5, expansion diff 55)
- `src/bike.c` (Eon diff 2, expansion diff 31)
- `src/play_time.c` (Eon diff 2, expansion diff 2)

## Deleted files

- `data/battle_ai_scripts.s`
- `graphics/object_events/pics/misc/item_ball.png`
- `graphics/pokemon/question_mark/double/front.png`
- `include/battle_ai_script_commands.h`
- `ld_script.ld`
- `src/data/battle_moves.h`
- `src/data/pokemon/evolution.h`
- `src/data/pokemon/level_up_learnsets.h`
- `src/data/pokemon/pokedex_text.h`
- `src/data/pokemon/tmhm_learnsets.h`
- `src/data/pokemon/tutor_learnsets.h`
- `src/data/pokemon_graphics/back_pic_coordinates.h`
- `src/data/pokemon_graphics/enemy_mon_elevation.h`
- `src/data/pokemon_graphics/front_pic_coordinates.h`
- `src/data/text/abilities.h`
- `src/data/text/item_descriptions.h`
- `src/data/text/move_descriptions.h`
- `src/data/text/move_names.h`
- `src/data/text/nature_names.h`
- `src/data/text/species_names.h`
- `src/data/text/trainer_class_names.h`
- `src/data/trainer_graphics/front_pic_anims.h`
- `src/data/trainer_graphics/front_pic_tables.h`
- `sym_ewram.txt`
