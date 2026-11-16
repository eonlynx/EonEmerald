# Eon memory and save-layout audit (expansion 1.13.0)

Date: 2026-11-16
Branch: `port/eon-features` (diagnostic build on top of `f533701fba`)

## Build totals

| Region | Used | Capacity | Used % | Free |
|---|---:|---:|---:|---:|
| EWRAM | 228,960 B | 262,144 B | 87.34% | 33,184 B |
| IWRAM | 28,705 B | 32,768 B | 87.60% | 4,063 B |
| ROM | 25,171,244 B | 33,554,432 B | 75.02% | 8,383,188 B |

## Largest EWRAM consumers

| Symbol | Bytes | Notes |
|---|---:|---|
| `gHeap` | 115,456 | Fixed `HEAP_SIZE` (0x1C300), shared with expansion |
| `gPokemonStorage` | 34,272 | Includes 128 B ASLR padding |
| `sBackupMapData` | 20,480 | 80x80 max map fits |
| `gSaveblock1` | 15,696 | Includes 128 B ASLR padding |
| `gSprites` | 4,420 | |
| `gSaveDataBuffer` | 4,096 | Save sector buffer |
| `gSaveblock2` | 4,012 | Includes 128 B ASLR padding |
| `gScanlineEffectRegBuffers` | 3,840 | |
| `sBattleRecords` | 2,656 | |
| `sPlaceDecorationGraphicsDataBuffer` | 2,212 | |
| `gLoadedSaveData` | 1,320 | |
| `gBlockRecvBuffer` | 1,280 | |
| `sDnsPaletteDmaBuffer` | 1,024 | Eon-only DNS buffer |
| `gStringVar4` | 1,000 | |

## Eon-specific deltas vs `expansion/1.13.0`

The diff of RAM declarations between `expansion/1.13.0` and HEAD adds only:

- `sDnsPaletteDmaBuffer[512]` (`u16`): 1,024 B
- `sPlayerSelectHoldFrames`: 1 B
- `gSysPcFromPokenav`: 1 B

`registeredLongItem` reuses two existing filler bytes in `SaveBlock1`, so it does
not grow the save block. `PokemonStorage::fusions`, the ASLR saveblock wrappers
and `SaveBlock3` are already present in the expansion 1.13.0 baseline.

Total Eon-only RAM delta: about **1.03 KB**, roughly **0.4% of EWRAM**.

## Save sector layout

`SECTOR_DATA_SIZE` is 3,968 B. The compile-time `STATIC_ASSERT`s in `src/save.c`
guarantee that each block fits; the current spare space is:

| Block | Size | Capacity | Spare |
|---|---:|---:|---:|
| `SaveBlock2` | 3,884 B | 3,968 B (1 sector) | 84 B |
| `SaveBlock1` | 15,568 B | 15,872 B (4 sectors) | 304 B |
| `PokemonStorage` | 34,144 B | 35,712 B (9 sectors) | 1,568 B |
| `SaveBlock3` | 4 B | 3,584 B | 3,580 B |

No sector is missing space. The tightest block is `SaveBlock2` with 84 B free,
so any new persistent field should be checked against that margin.

## DNS observations

- DNS runs every frame from `OverworldBasic()` and rewrites the whole palette
  copy (`32 palettes x 16 colours = 512 u16 = 1,024 B`).
- `sDnsPaletteDmaBuffer` is exactly 1,024 B and is followed in EWRAM by
  `sGame`; an off-by-one write would corrupt that pointer, but the loop bound is
  currently correct (`palNum < 32`, `colNum < 16`).
- There was a real out-of-bounds **read** in `DnsApplyFilters`: for `palNum == 15`
  the old condition `palNum < 15` passed `255` to
  `GetSpritePaletteTagByPaletteNum()`, which indexed `sSpritePaletteTags[16]`
  out of bounds. This has been fixed to `palNum < 16` and the getter now
  bounds-checks. This was a read only, so it is not sufficient by itself to
  explain the `0x1200D617` write/read.
- The value `0x1200` in the bad address is exactly `TAG_WEATHER_START`
  (`include/field_weather.h`), used by weather graphics and object-event shadows.
  This suggests a 16-bit tag/size overwriting the upper half of a 32-bit pointer
  rather than ordinary RAM exhaustion.

## Additional safeguards found during the audit

- `StartWeather()` did not check `AllocSpritePalette(PALTAG_WEATHER)` for failure.
  If all 16 sprite palette slots were busy it would use `0xFF` as a palette
  number and write 32 bytes far past `gPlttBufferUnfaded` (inside
  `gSaveDataBuffer`). The code now skips the copy and logs `WEATHER PAL FULL`.
- `LoadCustomWeatherSpritePalette()` used `> 16` instead of `>= 16`; it now
  rejects a failed allocation and logs `WEATHER PIC PAL FULL`.
- `UpdateSpritePaletteWithWeather()` now rejects indices `>= 16` and logs
  `WEATHER SPR PAL OOB`.
- `LoadSpritePaletteInSlot()` now validates the palette data pointer.
- The diagnostic build runs `CheckHeap()` every 64 frames and logs
  `HEAP CHECK FAILED` if the allocator metadata is corrupted.

These are real robustness fixes, but none of them writes a pointer value of
`0x1200xxxx`; they do not fully explain the observed invalid read.

## Diagnostic build

`include/config/general.h` currently sets `EON_DEBUG_MEMORY_LOG TRUE`. The logger
writes to the mGBA debug console and is compiled out when the option is FALSE.

Logs added:

| Tag | Location | Meaning |
|---|---|---|
| `EONMEM` | `src/main.c` | Struct sizes and save-sector spare at boot |
| `CORE PTRS` | `src/main.c` | Save/palette pointers became invalid |
| `SHEET BAD` | `src/sprite.c` | Invalid `SpriteSheet` pointer/size |
| `FRAMEIMG BAD` | `src/sprite.c` | Invalid sprite frame image pointer |
| `SPRCPY BAD` / `SPRREQ BAD` | `src/sprite.c` | Invalid queued sprite copy |
| `PAL BAD` / `PALF BAD` | `src/palette.c` | Invalid palette source/range |
| `SPAL BAD` | `src/sprite.c` | Invalid `SpritePalette` |
| `PALTAG OOB` | `src/sprite.c` | Palette-number out-of-bounds access |
| `GFXINFO BAD` / `OBJGFX BAD` | `src/event_object_movement.c` | Invalid object graphics info/images |
| `SPECIES OOB` | `src/event_object_movement.c` | Invalid follower species |
| `CSHEET BAD` / `CTEMPL BAD` | `src/decompress.c` | Invalid compressed sprite source |
| `DNSXFER BAD` / `DNSFILT BAD` | `src/dns.c` | Invalid DNS palette pointer |
| `TILESETANIM DMA BAD` | `src/tileset_anims.c` | Invalid animated-tileset DMA source |
| `DMA3REQ BAD` | `src/dma3_manager.c` | Invalid queued DMA3 source |

The diagnostic ROM is `pokeemerald_diag.gba`. Reproduce the error and collect
the last `[EON]`/`EON... BAD` line before the first `Bad memory Load16`.
