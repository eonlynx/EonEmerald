#!/usr/bin/env python3
"""
Add the Time Capsule attendant to every Hoenn Pokemon Center 2F map.

Usage (from the repository root):
    python tools/time_capsule/add_npc_to_maps.py

The script is idempotent: it skips maps that already have an object using
Common_EventScript_TimeCapsuleAttendant.
"""

import json
import os
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
MAPS_DIR = os.path.join(ROOT, "data", "maps")

NPC = {
    "graphics_id": "OBJ_EVENT_GFX_TEALA",
    "x": 11,
    "y": 2,
    "elevation": 0,
    "movement_type": "MOVEMENT_TYPE_FACE_DOWN",
    "movement_range_x": 1,
    "movement_range_y": 1,
    "trainer_type": "TRAINER_TYPE_NONE",
    "trainer_sight_or_berry_tree_id": "0",
    "script": "Common_EventScript_TimeCapsuleAttendant",
    "flag": "0",
}


def main():
    changed = 0
    for name in sorted(os.listdir(MAPS_DIR)):
        if not name.endswith("_PokemonCenter_2F"):
            continue
        if name.endswith("_Frlg"):
            continue
        map_dir = os.path.join(MAPS_DIR, name)
        path = os.path.join(map_dir, "map.json")
        if not os.path.exists(path):
            continue

        with open(path, "r", encoding="utf-8") as f:
            data = json.load(f)

        objects = data.setdefault("object_events", [])
        existing = next((obj for obj in objects if obj.get("script") == NPC["script"]), None)
        if existing is not None:
            # Keep the entry in sync with the current definition (sprite,
            # position, elevation, etc.).
            if existing == NPC:
                continue
            existing.clear()
            existing.update(NPC)
        else:
            objects.append(dict(NPC))
        with open(path, "w", encoding="utf-8", newline="\n") as f:
            json.dump(data, f, indent=2, ensure_ascii=False)
            f.write("\n")
        changed += 1
        print("Updated", path)

    if changed == 0:
        print("No maps changed (already patched).")
    else:
        print("Patched %d map(s)." % changed)


if __name__ == "__main__":
    main()
