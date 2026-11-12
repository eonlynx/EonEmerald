# Time Capsule tooling

Scripts used to generate the committed Time Capsule data. A normal build does
not run these; they are kept for reproducibility.

## `generate_tables.py`

Regenerates `src/data/time_capsule/tables.h` from the binary tables in
`Pokemon-Gen3-to-Gen-X/data` (MIT licensed). The generated header includes the
source SHA-1 checksums and the command used to generate it.

```sh
python tools/time_capsule/generate_tables.py
```

The reference project must be present next to `pokeemerald-expansion` when the
script is run. The generated header is committed, so builds do not need it.

## `add_npc_to_maps.py`

Adds the Time Capsule attendant object event to every Hoenn
`*_PokemonCenter_2F` map. It is idempotent; it skips maps that already have the
NPC.

```sh
python tools/time_capsule/add_npc_to_maps.py
```

The NPC is placed at `(11, 2)`, elevation 0, immediately to the right of the
existing Direct Corner attendant (`(10, 2)`). It uses
`OBJ_EVENT_GFX_TEALA`, like the other Cable Club attendants, and the shared
script `Common_EventScript_TimeCapsuleAttendant`.
