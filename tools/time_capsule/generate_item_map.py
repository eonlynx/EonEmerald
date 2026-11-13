#!/usr/bin/env python3
"""
Generate the Time Capsule Gen I/II <-> Gen III item mapping tables.

The Gen III -> Gen I/II and Gen I/II -> Gen III conversions are taken from
Pokemon-Gen3-to-Gen-X (MIT licensed), exactly like the other generated tables.
The expansion renumbers its items, so the reference tables (indexed by the
official Gen III item id) cannot be used directly.  This script matches the
expansion item names against the official Gen III item names and emits:

  gTimeCapsuleItemToGen2[ITEMS_COUNT]  expansion item -> Gen I/II item (0 = none)
  gTimeCapsuleItemFromGen2[256]        Gen I/II item   -> expansion item (0 = none)

Usage (from the repository root):
    python tools/time_capsule/generate_item_map.py
"""

import os
import re
import struct
import sys

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
REF = os.path.join(ROOT, "Pokemon-Gen3-to-Gen-X")
CONSTANTS = os.path.join(ROOT, "include", "constants", "items.h")
ITEM_DATA = os.path.join(ROOT, "src", "data", "items.h")
OUTPUT = os.path.join(ROOT, "src", "data", "time_capsule", "item_map.h")

# Names that were changed between Gen III and the current expansion.
# official Gen III name -> expansion name
_ALIAS_PAIRS = {
    "Parlyz Heal": "Paralyze Heal",
    "EnergyPowder": "Energy Powder",
    "X Defend": "X Defense",
    "X Special": "X Sp. Atk",
    "Thunderstone": "Thunder Stone",
    "TinyMushroom": "Tiny Mushroom",
    "BrightPowder": "Bright Powder",
    "SilverPowder": "Silver Powder",
    "BlackGlasses": "Black Glasses",
    "NeverMeltIce": "Never-Melt Ice",
    "TwistedSpoon": "Twisted Spoon",
    "Up-Grade": "Upgrade",
    "Stick": "Leek",
    "DeepSeaTooth": "Deep Sea Tooth",
    "DeepSeaScale": "Deep Sea Scale",
}


def _norm_alias(name):
    name = name.replace("\u00e9", "e").replace("\u00c9", "E")
    name = name.replace("\u2019", "'").replace("\u2018", "'")
    name = name.replace("-", " ").replace(".", "").replace("'", "")
    name = re.sub(r"\s+", " ", name).strip().lower()
    return name


ALIASES = {_norm_alias(k): _norm_alias(v) for k, v in _ALIAS_PAIRS.items()}


def norm(name):
    name = name.replace("\u00e9", "e").replace("\u00c9", "E")
    name = name.replace("\u2019", "'").replace("\u2018", "'")
    name = name.replace("-", " ").replace(".", "").replace("'", "")
    name = re.sub(r"\s+", " ", name).strip().lower()
    return name


def parse_enum():
    src = open(CONSTANTS, encoding="utf-8").read()
    body = src.split("enum __attribute__((packed)) Item", 1)[1]
    body = body.split("{", 1)[1].split("};", 1)[0]
    body = re.sub(r"//[^\n]*", "", body)
    # Drop preprocessor lines; the TM/HM aliases are macro generated and are
    # handled by name below.
    body = "\n".join(l for l in body.splitlines() if not l.strip().startswith("#"))
    entries = [e.strip() for e in body.split(",") if e.strip()]

    values = {}
    next_value = 0
    for entry in entries:
        m = re.match(r"^([A-Za-z_][A-Za-z0-9_]*)\s*(?:=\s*(.+))?$", entry)
        if not m:
            continue
        name, expr = m.group(1), m.group(2)
        if expr is None:
            value = next_value
        else:
            expr = expr.strip()
            if re.fullmatch(r"0x[0-9A-Fa-f]+|\d+", expr):
                value = int(expr, 0)
            elif expr in values:
                value = values[expr]
            else:
                continue
        values[name] = value
        next_value = value + 1
    return values


def parse_names(values):
    src = open(ITEM_DATA, encoding="utf-8").read()
    names = {}
    for m in re.finditer(r"\[([A-Za-z_][A-Za-z0-9_]*)\]\s*=\s*\{(.*?)\n\s*\},", src, re.S):
        const, block = m.group(1), m.group(2)
        nm = re.search(r'\.name\s*=\s*ITEM_NAME\("((?:[^"\\]|\\.)*)"\)', block)
        if not nm:
            continue
        text = nm.group(1).replace('\\"', '"').replace("\\n", " ")
        if const in values:
            names[values[const]] = text
        else:
            # The TM/HM blocks use macro-generated aliases, but their display
            # name is TMxx / HMxx and the expansion numbers them in order.
            tm = re.fullmatch(r"TM(\d{2})", text)
            hm = re.fullmatch(r"HM(\d{2})", text)
            if tm:
                names[582 + int(tm.group(1)) - 1] = text
            elif hm:
                names[682 + int(hm.group(1)) - 1] = text
    return names


def decode_official_names():
    d = open(os.path.join(REF, "data", "item_names.bin"), "rb").read()
    size_initial = ((d[0] >> 0) & 1) + 1
    size_offsets = ((d[0] >> 1) & 1) + 1
    shift = (d[0] >> 2) & 7
    shift_init = d[0] >> 5
    if size_initial == 1:
        initial = d[1] << shift_init
        pos = 2
    else:
        initial = struct.unpack_from("<H", d, 1)[0] << shift_init
        pos = 4
    count = initial - pos
    if size_offsets == 2:
        count >>= 1

    def offset(i):
        if size_offsets == 1:
            return d[pos + i] << shift
        return struct.unpack_from("<H", d, pos + 2 * i)[0] << shift

    names = []
    for i in range(count):
        start = initial + offset(i)
        end = d.find(b"\0", start)
        names.append(d[start:end].decode("latin-1"))
    return names


def main():
    values = parse_enum()
    exp_names = parse_names(values)
    official = decode_official_names()

    exp_by_norm = {}
    for item_id, name in exp_names.items():
        exp_by_norm.setdefault(norm(name), []).append(item_id)

    def find_exp(official_name):
        key = norm(official_name)
        key = ALIASES.get(key, key)
        candidates = exp_by_norm.get(key, [])
        return min(candidates) if candidates else 0

    fwd = open(os.path.join(REF, "data", "item_gen3_to_12.bin"), "rb").read()
    rev_raw = open(os.path.join(REF, "data", "item_gen12_to_3.bin"), "rb").read()
    rev = [struct.unpack_from("<H", rev_raw, 2 * i)[0] for i in range(256)]

    item_count = values.get("ITEMS_COUNT", max(values.values()) + 1)
    to_gen2 = [0] * item_count
    misses = []
    for official_id in range(len(fwd)):
        gen2_item = fwd[official_id]
        if gen2_item in (0, 0xFE, 0xFF):
            continue
        name = official[official_id]
        exp_id = find_exp(name)
        if not exp_id:
            misses.append((official_id, name))
            continue
        # A few names map to more than one expansion item (aliases); keep the
        # canonical one that is actually a holdable item.
        to_gen2[exp_id] = gen2_item

    from_gen2 = [0] * 256
    for gen2_item in range(256):
        official_id = rev[gen2_item]
        if official_id == 0xFFFF or official_id >= len(official):
            continue
        exp_id = find_exp(official[official_id])
        if exp_id:
            from_gen2[gen2_item] = exp_id
        else:
            misses.append((official_id, official[official_id]))

    # Sanity checks against well-known items.
    checks = [
        ("Poke Ball", 4), ("Great Ball", 3), ("Ultra Ball", 2),
        ("Master Ball", 1), ("Potion", 18), ("Antidote", 9),
        ("Full Restore", 14), ("Max Potion", 15), ("Revive", 39),
        ("Rare Candy", 32), ("PP Up", 62), ("Ether", 63),
    ]
    for name, want in checks:
        exp_id = find_exp(name)
        got = to_gen2[exp_id] if exp_id else None
        status = "OK" if got == want else "MISMATCH"
        print("%-14s exp=%-4s gen2=%-4s want=%-4s %s" %
              (name, exp_id, got, want, status))

    # The TM/HM names in the official table are "TM01"..; map them by name.
    for n in range(1, 51):
        name = "TM%02d" % n
        exp_id = find_exp(name)
        if exp_id:
            to_gen2[exp_id] = fwd[288 + n]
    for n in range(1, 9):
        name = "HM%02d" % n
        exp_id = find_exp(name)
        if exp_id:
            to_gen2[exp_id] = fwd[338 + n]

    print("item_count", item_count)
    print("unmatched official items", len(set(misses)))
    for official_id, name in sorted(set(misses))[:20]:
        print("  miss", official_id, repr(name))

    with open(OUTPUT, "w", encoding="utf-8", newline="\n") as out:
        out.write("// AUTO-GENERATED FILE - DO NOT EDIT BY HAND.\n")
        out.write("// Generated by: tools/time_capsule/generate_item_map.py\n")
        out.write("// Sources: Pokemon-Gen3-to-Gen-X data/item_gen3_to_12.bin,\n")
        out.write("//          data/item_gen12_to_3.bin and data/item_names.bin (MIT),\n")
        out.write("//          plus the expansion's item constants/names.\n\n")
        out.write("#ifndef GUARD_TIME_CAPSULE_ITEM_MAP_H\n")
        out.write("#define GUARD_TIME_CAPSULE_ITEM_MAP_H\n\n")
        out.write("// expansion item id -> Gen I/II item id (0 = cannot be held)\n")
        out.write("static const u8 gTimeCapsuleItemToGen2[ITEMS_COUNT] = {\n")
        for i in range(0, len(to_gen2), 16):
            out.write("    " + ", ".join("0x%02X" % b for b in to_gen2[i:i + 16]) + ",\n")
        out.write("};\n\n")
        out.write("// Gen I/II item id -> expansion item id (0 = no equivalent)\n")
        out.write("static const u16 gTimeCapsuleItemFromGen2[256] = {\n")
        for i in range(0, 256, 8):
            out.write("    " + ", ".join("0x%03X" % b for b in from_gen2[i:i + 8]) + ",\n")
        out.write("};\n\n")
        out.write("#endif // GUARD_TIME_CAPSULE_ITEM_MAP_H\n")

    print("Wrote", OUTPUT)


if __name__ == "__main__":
    main()
