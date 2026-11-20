# Battle Frontier outside split

The Battle Frontier outside area is split into four layouts so each half stays
inside the GBA secondary-tileset limits:

| Layout | Map | Secondary tileset |
| --- | --- | --- |
| `LAYOUT_BATTLE_FRONTIER_OUTSIDE_WEST` | `BattleFrontier_OutsideWest` | `gTileset_BattleFrontierOutsideWestSouth` |
| `LAYOUT_BATTLE_FRONTIER_OUTSIDE_EAST` | `BattleFrontier_OutsideEast` | `gTileset_BattleFrontierOutsideEastSouth` |
| `LAYOUT_BATTLE_FRONTIER_OUTSIDE_WEST_NORTH` | `BattleFrontier_OutsideWest_North` | `gTileset_BattleFrontierOutsideWestNorth` |
| `LAYOUT_BATTLE_FRONTIER_OUTSIDE_EAST_NORTH` | `BattleFrontier_OutsideEast_North` | `gTileset_BattleFrontierOutsideEastNorth` |

The original full tilesets are the source of truth and should not be replaced by
the generated split copies:

```text
data/tilesets/secondary/battle_frontier_outside_west
data/tilesets/secondary/battle_frontier_outside_east
```

## Border requirement

Map connections are rendered with the current map's secondary tileset. Matching
metatile IDs is not enough: the 16-byte metatile entries, the secondary tile
indices and the palette banks must also match on both sides of every seam.
Otherwise the area transition shows the wrong tiles even when the IDs look
correct in Porymap.

The engine copies these bands:

* west connection: 7 columns of the eastern map;
* east connection: 8 columns of the western map;
* north/south connection: 7 rows of the neighbouring map.

## Rebuilding the split tilesets

From the decomp project root:

```sh
python <poryeasy>/porytiles_map.py preserve-rebuild --project-root .
```

This rebuilds the four split tilesets from the original West/East bases,
preserves original metatile/tile IDs where possible, places imported content in
free slots in map order, and runs the exact border pass automatically.

To run or verify only the border pass:

```sh
python <poryeasy>/porytiles_map.py exact-border --project-root . --dry-run
python <poryeasy>/porytiles_map.py exact-border --project-root .
```

`exact-border` is idempotent. On the committed split state it reports:

```text
entries changed:      0
tile patterns moved:  0
palette copies:       0
map appearance delta: 0
visible WS/ES ew: 0 mismatch(es)
visible WN/EN ew: 0 mismatch(es)
visible WS/WN ns: 0 mismatch(es)
visible ES/EN ns: 0 mismatch(es)
```

`poryeasy import` also runs the exact border pass automatically after importing
one of these four maps. Use `--no-exact-border` only for debugging.

## Notes

* A single base tileset cannot hold every appearance from both halves: the West
  half exceeds the 512-tile secondary limit when all current art is combined.
  The four split tilesets are therefore intentional, not a workaround.
* Backups are written under `.poryeasy-backups/`.
* Do not hand-edit the generated split tilesets when changing shared border
  art. Change the source/import and rerun the workflow so the IDs, tile
  positions and palettes stay aligned.
