# Eon memory, save-layout and TallGrass audit

Date: 2026-11-16
Branch: `port/eon-features`

## Memory usage (expansion 1.13.0 build)

| Region | Used | Capacity | Used % | Free |
|---|---:|---:|---:|---:|
| EWRAM | 228,960 B | 262,144 B | 87.34% | 33,184 B |
| IWRAM | 28,685 B | 32,768 B | 87.54% | 4,083 B |
| ROM | 25,169,120 B | 33,554,432 B | 75.01% | 8,385,312 B |

Largest EWRAM consumers are `gHeap` (115,456 B), `gPokemonStorage`
(34,272 B including 128 B ASLR padding), `sBackupMapData` (20,480 B) and
`gSaveblock1` (15,696 B including 128 B ASLR padding).

The Eon-specific RAM delta over the expansion 1.13.0 baseline is about 1 KB:
`sDnsPaletteDmaBuffer[512]` (1,024 B) plus two one-byte flags. `registeredLongItem`
reuses two existing filler bytes in `SaveBlock1`, so it does not grow the block.
The fusion storage, ASLR wrappers and `SaveBlock3` are already part of the
expansion 1.13.0 baseline.

## Save sector layout

`SECTOR_DATA_SIZE` is 3,968 B. The compile-time `STATIC_ASSERT`s in `src/save.c`
guarantee that every block fits.

| Block | Size | Capacity | Spare |
|---|---:|---:|---:|
| `SaveBlock2` | 3,884 B | 3,968 B (1 sector) | 84 B |
| `SaveBlock1` | 15,568 B | 15,872 B (4 sectors) | 304 B |
| `PokemonStorage` | 34,144 B | 35,712 B (9 sectors) | 1,568 B |
| `SaveBlock3` | 4 B | 3,584 B | 3,580 B |

No save sector was missing space. The tightest block is `SaveBlock2` with 84 B
free, so any new persistent field must be checked against that margin.

## TallGrass frame-table fix

The overworld error was caused by a bad merge in
`src/data/field_effects/field_effect_objects.h`:

- `sPicTable_TallGrass` had only four entries: frames `0, 1, 3, 4`.
- The 96x16 `tall_grass.png` contains six frames, and `sAnim_TallGrass` uses
  frames `1, 2, 3, 4, 5, 0`.
- Frame index 5 read past the table into
  `gFieldEffectObjectTemplate_ShadowExtraLarge`, interpreting `0x0842A7B0` as
  the data pointer and `0xA7D0` (42,896) as the size.
- That queued a 42 KB copy past the end of VRAM and corrupted memory, producing
  the `0x1200...` invalid reads.

The table now contains all six frames (`0-5`), matching the graphic and the
animation. The temporary diagnostic logger and pointer checks used to locate
the bug have been removed.
