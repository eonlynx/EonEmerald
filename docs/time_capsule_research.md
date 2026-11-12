# Time Capsule GBA ↔ Gen I/II — Research and Implementation Plan

> Research document for adding a **Time Capsule** to `pokeemerald-expansion`:
> a new NPC on the second floor of Pokémon Centers offering **Trade Center**
> and **Colosseum** against Pokémon Red/Blue/Yellow (Gen I) and
> Gold/Silver/Crystal (Gen II), using the GBA link port in Game Boy mode
> (Game Boy Color link cable).

This document does not implement anything by itself. Its purpose is to settle:

- the exact link protocol (handshake, room selection, party exchange, patch
  lists, trade menu, confirmation, exit);
- automatic Gen I vs Gen II detection;
- Pokémon conversion in both directions;
- compatibility checks and player-facing messages;
- concrete integration points in `pokeemerald-expansion`;
- the real feasibility of Colosseum;
- the test plan.

---

## 0. Design decisions (summary)

### 0.1 One NPC, automatic detection (recommended)

**A single NPC** named “TIME CAPSULE” with **TRADE CENTER / COLOSSEUM / INFO /
EXIT** is enough for Gen I and Gen II. The protocol already tells us who is on
the other end:

| Byte sent by the GB game | Interpretation | Mode the GBA must use |
|---|---|---|
| `0x60` | Gen I (RBY Cable Club) | **Gen I**: Gen I party format, 151 species / 165 moves, no Eggs, no real held items |
| `0x61` | Gen II (GSC Trade Center) | **Gen II**: Gen II party format, 251 species / 251 moves, Eggs allowed, mail block present |

Therefore:

- **Crystal connects from any Pokémon Center**, using its **normal Trade
  Center**. To Crystal, the GBA looks like **another Gen II game** (another
  Crystal). The Ecruteak Time Capsule receptionist is not needed.
- **Red/Blue/Yellow connect from the Cable Club of any Pokémon Center**. To
  RBY, the GBA looks like **another Gen I game**.
- The strict Gen I rules apply only when the detected peer is Gen I. Crystal
  uses the more permissive Gen II profile.

This avoids asking the player to choose a generation before connecting and
avoids two NPCs. “Time Capsule” is the Emerald-side feature name; underneath it
is a normal GB link using the profile of the detected generation.

### 0.2 Alternative: two NPCs / forced modes

The same engine can expose two entries:

- `TIME CAPSULE (GEN I)`: forces the Gen I profile. If a Gen II game connects,
  it can be pushed into GSC's Time Capsule mode (reply `0x00` during the
  timeout check; see §3.2), but Crystal would have to use the Ecruteak Time
  Capsule receptionist instead of its normal Trade Center.
- `TIME CAPSULE (GEN II)`: forces the Gen II profile. If a Gen I game connects,
  show an error.

This does not duplicate the protocol: only `gTimeCapsuleForcedGen` (0 = auto,
1 = Gen I, 2 = Gen II) and the restriction profile change. **It is not needed
for the desired use case** (Crystal from any Pokémon Center), so one NPC with
auto-detection is the recommendation. If two NPCs are wanted later, they can be
two scripts calling the same `special` with a forced mode.

### 0.3 Colosseum: phase 2

Trade Center is entirely feasible with the work in this document.
**Colosseum against Gen I/II is a separate project**: the link protocol is not
enough; the GBA would have to reimplement the Gen II battle engine (and another
one for Gen I), because both games simulate the battle locally and only exchange
actions. See §7.

For the first release we can:

- add the `COLOSSEUM` option but show “The COLOSSEUM link to the past is not
  ready yet.”; or
- hide it behind `TIME_CAPSULE_COLOSSEUM_ENABLED` (default `FALSE`).

---

## 1. What each reference project provides

### 1.1 `Pokemon-Gen3-to-Gen-X` (Lorenzooone) — **primary reference**

A GBA homebrew that **already implements the real link against unmodified
Gen I/II cartridges**. It is the best source for the protocol and the SIO layer.

Key files:

| File | Contents |
|---|---|
| `source/sio.c`, `include/sio.h` | Normal 8-bit SIO driver, master/slave, IRQ. |
| `source/communicator.c` | Gen I/Gen II handshake and trade-menu state machine. |
| `source/sio_buffers.c`, `include/sio_buffers.h` | Exact Gen I/II block layouts, patch lists, mail. |
| `source/gen_converter.c` | Gen3 ↔ Gen1/Gen2 conversion (species, moves, items, EVs, IVs, exp, text). |
| `source/gen12_methods.c` | Gender, shininess, Unown, stats, levels, Pokérus, names. |
| `source/pid_iv_tid.c` | Legal PID/IV generation preserving nature, gender, shininess and Unown. |
| `source/text_handler.c` | Gen3 ↔ Gen1/2 charset tables. |
| `data/*.bin` | Tables: Gen1 species, items, text, exp, stats, etc. |

**MIT licensed**, so it can be reused/ported with attribution.

### 1.2 `Poke_Transporter_GB` (GearsProgress / Striaton Lab)

A **one-way** Gen I/II → Gen III transfer based on **ACE payloads** injected
into the GB game. It is not the model for a bidirectional Time Capsule, but it
provides:

- the **PCCS** conversion spec (used by the project);
- the `LinkSPI.hpp` driver from `gba-link-connection`;
- hardware notes (GBC cable, voltages, EZ Flash, multiboot);
- confirmation that a GBA in GBA mode can talk to a GB/GBC through the link
  port using a **Game Boy Color link cable**.

**Do not copy the ACE approach**: it depends on the GB ROM version, does not
work with bootlegs, and is not a normal link. The Time Capsule should use the
standard trade protocol, like `Pokemon-Gen3-to-Gen-X`.

### 1.3 `PokemonGB_Online_Trades_and_Battles`

Python implementation of the Gen I/II link protocol (trade and battle) for
online play. Very useful as a **second source** and to understand:

- `utilities/gsc_trading.py`, `gsc_trading_data_utils.py`;
- `utilities/rby_trading.py`, `rby_trading_data_utils.py`;
- `utilities/gsc_battling.py`, `rby_battling.py`;
- `notes/gsc_trading_notes.txt`, `notes/gsc_battling_notes.txt`.

It confirms sequences such as:
`0x01` first connection, `0xFE` wait, `0x61` saved, `0xD1/0xD2` room,
`0x75/0x76` trade start, `0xFD` sync, `0x70-0x75` Pokémon selection,
`0x71` no trade, `0x72` trade, `0x7F` exit.

### 1.4 `pokecrystal` / `pokered`

The **authoritative source** for the GB side: which bytes the game sends, what
it expects, how the Time Capsule validates Pokémon, and which restrictions it
applies.

Key files:

- `pokecrystal/home/serial.asm` — serial ISR, byte exchange, timeouts.
- `pokecrystal/engine/link/link.asm` — connection, `LinkCommunications`, trade
  menu, success byte, `Gen2ToGen1LinkComms`.
- `pokecrystal/engine/link/link_trade.asm` — trade UI.
- `pokecrystal/engine/link/time_capsule.asm` — `ValidateOTTrademon`,
  `CheckAnyOtherAliveMonsForTrade`.
- `pokecrystal/maps/Pokecenter2F.asm` — `CheckTimeCapsuleCompatibility`,
  rejection messages, Time Capsule NPC.
- `pokered/home/serial.asm` — Gen I byte exchange.
- `pokered/engine/link/cable_club.asm` — Gen I trade.
- `pokered/engine/menus/main_menu.asm` — `LinkMenu` (Gen I room selection).

### 1.5 `gba-link-connection`

Reference for SIO registers and 8-bit/32-bit normal mode. The copy included in
this repo (`gba-link-connection/`) is C++ and designed for tonc/libtonc, not for
`pokeemerald-expansion`. **Do not compile it as-is**; use it as a reference to
write the C driver.

---

## 2. Physical layer: GBA ↔ Game Boy through the link port

### 2.1 Hardware

- **Game Boy Color link cable**. A GBA link cable will not work; the GBA cable
  is wired for the GBA protocol (SD) and not for the GB link.
- The GBA must be running the game in **GBA mode** (not CGB mode). The GBA link
  port in GBA mode can use normal 8-bit SIO, which is compatible with the
  GB/CGB shift register.
- Voltages: GBA 3.3 V, GB 5 V. The community (Goppier, Poke_Transporter)
  confirms that a GBC cable does not damage either system; it is still
  officially unsupported hardware.
- Do not use the GBA Wireless Adapter or a GBA link cable. The game must
  disable RFU/GBA link during the session.

### 2.2 Why the GBA must be the **slave**

In GBA normal SIO:

- As **master**, the GBA can only generate **256 kHz** or **2 MHz**.
- The GB/CGB in normal mode uses **8192 Hz** (slow) or **262144 Hz** (GBC fast).
  Gen I/II games use **8192 Hz** (slow).

Therefore the GBA **cannot be master** at 8192 Hz. It must be **slave**
(external clock), and the GB game must be master. Fortunately, the GB games
alternate roles during connection, so the GBA can stay slave by always sending
`0x02` (“I want external clock”); the GB ends up as master.

> `Pokemon-Gen3-to-Gen-X` does exactly this: `SIO_SLAVE`, `SIO_8`, external
> clock, and `master = 0` by default.

### 2.3 Registers and configuration

For the GBA:

```c
REG_RCNT   = 0;                                  // Normal mode
REG_SIOCNT = SIO_8BIT_MODE | SIO_SO_HIGH;        // 8-bit, external clock (slave)
// bit 14 = IRQ enable
REG_SIOCNT |= SIO_INTR_ENABLE;
REG_IE    |= INTR_FLAG_SERIAL;
```

- `SIO_8BIT_MODE` = `0x0000`.
- `SIO_SO_HIGH` = bit 3 (`0x0008`) = SO high while idle.
- `SIO_INTR_ENABLE` = bit 14 (`0x4000`).
- Bit 7 (`SIO_START`) is set to arm the shift register; it clears itself after
  8 bits and raises `INTR_FLAG_SERIAL`.

The GBA must **not** start transfers; it only arms the register and waits for
the GB clock. The serial handler:

1. reads `REG_SIODATA8` (received byte);
2. writes the next byte to send into `REG_SIODATA8`;
3. re-arms `SIO_START` (and handles SO according to the slave procedure);
4. acknowledges `REG_IF`.

### 2.4 Real-time behaviour and VBlank risk

At 8192 Hz, one byte takes **≈1.22 ms** (122 µs per bit). The GBA has plenty of
margin, but the serial handler has **lower priority than VBlank** in the GBA
interrupt controller (bit 7 vs bit 0). If
`pokeemerald-expansion`'s VBlank handler takes longer than ~1.2 ms (sound
mixing, DMA, etc.), bytes can be lost.

Design measures:

- **Short** serial handler, in IWRAM (`IWRAM_CODE`), no heavy calls.
- Circular TX/RX queues in IWRAM; the handler only pops/pushes.
- During the GB session, use a **light VBlank**: stop music
  (`m4aMPlayAllStop`), avoid large animations/DMA, and reduce the VBlank
  callback.
- Do not write flash while streaming a party block. See §6.9 (save order).
- If a byte is lost, abort the session cleanly and notify the player; do not
  attempt to “resynchronise” blindly.

### 2.5 Streaming vs. state machine in the IRQ

Two options:

1. **Everything in the IRQ** (like `Pokemon-Gen3-to-Gen-X`): the IRQ runs the
   byte-by-byte state machine. It is fast and already proven, but hard to debug
   inside `pokeemerald-expansion`.
2. **IRQ + queues + state machine in the main loop** (recommended for Emerald):
   the IRQ only moves bytes; the main loop advances the protocol. For bulk data
   (party block, patch list), the main loop **pre-fills** the entire TX buffer
   and the IRQ streams it. For the trade menu (request/response), the main loop
   can reply calmly because the GB repeats its byte.

Option 2 is safer for not breaking the game and allows unit-testing the state
machine. The only critical phase is streaming the block: the buffer must be
complete **before** the GB starts sending its block.

---

## 3. Link protocol

> All sequences are verified against `pokecrystal`/`pokered` and
> `Pokemon-Gen3-to-Gen-X`. Bytes are those travelling over the cable.

### 3.1 Initial connection

The GB game alternates between `0x01` and `0x02`:

- `0x01` = “I want internal clock” (the GB would be master),
- `0x02` = “I want external clock” (the GB would be slave).

The GBA always replies **`0x02`** (it wants to be slave). The GB eventually
sends `0x01` and becomes master.

Typical Gen II sequence (`WaitForLinkedFriend`):

```
GB   -> 0x02 (slave) ... timeout ...
GB   -> 0x01 (master)
GBA  -> 0x02
GB   -> 0x00, 0x00   (LinkDataReceived x2)
```

Typical Gen I sequence (`CableClubNPC`):

```
GB   -> 0x02 (slave) ... timeout ...
GB   -> 0x01 (master)
GBA  -> 0x02
GB   -> 0x00, 0x00   (Serial_SendZeroByte x2)
```

The GBA sends `0xFE` (“no data”) when it has nothing to say. The GB ignores
`0xFE` in the sync routines.

### 3.2 Generation detection

After connection and the `0x00` bytes:

| Game | Byte sent | What it expects |
|---|---|---|
| Gen I (RBY) | `0x60` (`Serial_SyncAndExchangeNybble`) | high nibble `0x60` |
| Gen II (GSC) | `0x61` (`CheckLinkTimeout_Receptionist`) | high nibble `0x60` |

The GBA:

- if it receives `0x60` → **Gen I**, reply `0x60`;
- if it receives `0x61` → **Gen II**, reply `0x61`;
- if it receives `0x61` but we want to force GSC's Time Capsule mode (only for
  the two-NPC alternative), reply `0x00`. GSC interprets
  `wOtherPlayerLinkMode == 0` as “linked to a Gen I game” and, if the player is
  at the Time Capsule receptionist, enters Time Capsule mode; at the normal
  Trade Center it shows “You can't link to the past here”.

**For the normal case (Crystal from any Pokémon Center) always reply `0x61`.**
Crystal sees a normal Gen II partner.

### 3.3 Room selection

#### Gen II

`CheckBothSelectedSameRoom` → `Link_EnsureSync(wChosenCableClubRoom)`:

| Room | `wChosenCableClubRoom` | Byte sent/expected |
|---|---|---|
| Trade Center | 1 | `0xD1` |
| Colosseum | 2 | `0xD2` |
| None | 0 | `0xD0` |

The GBA replies with the same room byte. GSC then sends two `0x00`
(`LinkDataReceived` x2); the GBA replies `0x00`.

If the rooms differ, GSC shows “Incompatible rooms were chosen.” and closes.

#### Gen I

`LinkMenu` sends `0xD0 + menuItem + (A ? 4 : 8)`:

| Room | Button | Byte |
|---|---|---|
| Trade Center (item 0) | A | `0xD4` |
| Colosseum (item 1) | A | `0xD5` |
| Cancel (item 2) | A | `0xD6` |

The GBA replies `0xD4` or `0xD5`. If the GB is master, its choice wins.

### 3.4 Timeout / “are you ready?” check before the exchange

#### Gen II (Trade Center and Colosseum)

`CheckLinkTimeout_Gen2` (called from `LinkCommunications`):

- `wPlayerLinkAction = 5` → sends `base + 5`
- `wPlayerLinkAction = 6` → sends `base + 6`

`base` depends on `wLinkMode`:

| Mode | Base | Bytes |
|---|---|---|
| Trade Center (`LINK_TRADECENTER`) | `0x70` | `0x75`, `0x76` |
| Colosseum (`LINK_COLOSSEUM`) | `0x80` | `0x85`, `0x86` |

The GBA must reply `0x75`/`0x76` (trade) or `0x85`/`0x86` (Colosseum) in the
same order.

> `Pokemon-Gen3-to-Gen-X` only implements trade; its tables use
> `{0x75, 0x75, 0x76}`. Colosseum would need `{0x85, 0x85, 0x86}`.

#### Gen I

`Serial_SyncAndExchangeNybble` (base `0x60`):

- the GBA sends `0x60`, expects `0x60`;
- then `0x60`, expects `0xFD` (start of the random-number block);
- then the bulk exchange begins.

### 3.5 Bulk data exchange

After the check, the GB game calls `Serial_ExchangeBytes` for:

1. **Random numbers**: `7 × 0xFD` + **10 random bytes** (< `0xFD`).
2. **Party block** (see §3.6).
3. **Patch list**: `3 × 0xFD` + offsets + `0xFF` + padding up to **200 bytes**.
4. **Mail block** (Gen II Trade Center only): `5 × 0x20` + messages + metadata +
   patch set (see §3.8).

`Serial_ExchangeBytes` ignores received bytes until it sees `0xFD` (preamble).
Therefore the GBA must start each block with the correct `0xFD` bytes.
`0xFE` means “no data”: **it must never appear as real data**. That is what the
**patch list** is for.

#### Sizes and timings

| Block | Gen II | Gen I |
|---|---:|---:|
| RN (7 + 10) | 17 | 17 |
| Party (6 preamble + data) | 450 | 424 |
| Patch list | 200 | 200 |
| Mail (Gen II Trade Center only) | 390 | — |
| **Total Trade Center** | **1057 B** | **641 B** |
| **Total Colosseum** | **667 B** | **641 B** |
| Approx. time at 8192 bps | ~1.0 s | ~0.6 s |

(Cable time only; the GB adds pauses and `DelayFrame`.)

### 3.6 Party block: exact layouts

#### Gen II (444 bytes + 6 preamble)

```
6 × 0xFD
trainer name        11 bytes  (Gen II charset, terminator 0x50)
party count          1 byte
species list         6 bytes  (Gen II index; 0xFD = Egg; 0xFF = empty)
terminator           1 byte   (0xFF)
trainer OT ID        2 bytes  (big-endian)
party mons           6 × 48 bytes
OT names             6 × 11 bytes
nicknames            6 × 11 bytes
padding              3 bytes
```

`struct gen2_mon_data` (48 bytes, big-endian on the wire):

| Offset | Field |
|---:|---|
| 0 | species |
| 1 | item |
| 2-5 | moves[4] |
| 6-7 | OT ID |
| 8-10 | exp[3] (big-endian) |
| 11-20 | stat exp[5] (5 × u16 big-endian) |
| 21-22 | DVs (u16 big-endian) |
| 23-26 | PP[4] |
| 27 | happiness |
| 28 | Pokérus |
| 29-30 | caught data (byte0 = time<<6 \| level; byte1 = gender<<7 \| location) |
| 31 | level |
| 32 | status |
| 33 | unused |
| 34-35 | current HP |
| 36-37 | max HP |
| 38-39 | attack |
| 40-41 | defense |
| 42-43 | speed |
| 44-45 | Sp. Atk |
| 46-47 | Sp. Def |

#### Gen I (418 bytes + 6 preamble)

```
6 × 0xFD
trainer name        11 bytes  (Gen I charset, terminator 0x50)
party count          1 byte
species list         6 bytes  (Gen I internal index; 0xFF = empty)
terminator           1 byte   (0xFF)
party mons           6 × 44 bytes
OT names             6 × 11 bytes
nicknames            6 × 11 bytes
padding              3 bytes
```

`struct gen1_mon_data` (44 bytes, big-endian on the wire):

| Offset | Field |
|---:|---|
| 0 | species (Gen I internal index) |
| 1-2 | current HP |
| 3 | box level |
| 4 | status |
| 5 | type 1 |
| 6 | type 2 |
| 7 | catch rate (reused as “item” for the Gen II↔Gen I trade) |
| 8-11 | moves[4] |
| 12-13 | OT ID |
| 14-16 | exp[3] |
| 17-26 | stat exp[5] |
| 27-28 | DVs |
| 29-32 | PP[4] |
| 33 | level |
| 34-43 | stats[5] (HP, Atk, Def, Spe, Special) |

> **Gen I note:** byte 7 is “catch rate”, not an item. When GSC trades with
> RBY, it copies the Gen II item byte into that field unchanged; on the way
> back, that byte is interpreted as the item again. This is the trick that lets
> items “survive” the Time Capsule. Our converter can do the same.

### 3.7 Patch list

The byte `0xFE` cannot be sent as data (it means “no data”). The GB game:

1. walks the data block;
2. replaces every `0xFE` with `0xFF` and records its offset in the patch list;
3. sends the patch list after the block.

The GBA must:

- **on send**: do the same with its block (replace `0xFE` → `0xFF` and build the
  patch list);
- **on receive**: apply the GB's patch list to restore the `0xFE` bytes.

Patch list format (200 bytes):

```
3 × 0xFD
part 1 offsets (offsets 1..0xFC)
0xFF
part 2 offsets (offsets > 0xFC)
0xFF
padding up to 200 bytes
```

Offsets are 1-based from the start of the data block (after the preamble).
`Pokemon-Gen3-to-Gen-X` has `prepare_patch_set()` and `apply_patch_set()` in
`source/sio_buffers.c`; they can be ported directly.

### 3.8 Mail block (Gen II Trade Center only)

GSC sends mail **only** in `LINK_TRADECENTER` (not Colosseum, not Gen I):

```
5 × 0x20
messages:   6 × 33 bytes  (MAIL_MSG_LENGTH 32 + 1)
metadata:   6 × 14 bytes  (MAIL_STRUCT_LENGTH 0x2F - 33)
patch set:  (6 × 17 + 1) bytes = 103
```

Total: `5 + 198 + 84 + 103 = 390` bytes.

If no Pokémon is holding mail, a valid “empty” block can be sent: messages and
metadata zeroed, patch set starting with `0xFF`. GSC accepts it.

Gen I and Colosseum have **no mail block**.

### 3.9 Trade menu

After the party exchange, the GB game enters the trade menu. It already has the
other side's party; only **indices** are exchanged.

#### Gen II (base `0x70`)

| Action | Byte |
|---|---|
| Offer Pokémon `i` (0-5) | `0x70 + i` |
| No trade / decline | `0x71` |
| Accept trade | `0x72` |
| Exit / cancel session | `0x7F` |
| “No data” / idle | `0xFE` |

#### Gen I (base `0x60`)

| Action | Byte |
|---|---|
| Offer Pokémon `i` (0-5) | `0x60 + i` |
| No trade / decline | `0x61` |
| Accept trade | `0x62` |
| Exit / cancel session | `0x6F` |
| “No data” / idle | `0xFE` |

The GBA must send `0xFE` while the player has not chosen anything. The GB
repeats its byte until it receives a byte with the correct high nibble, so the
menu is not time-critical.

### 3.10 Post-trade confirmation and anti-cloning

#### Gen II

After the trade animation, GSC sends a check byte:

- `0x70` if neither Pokémon is Mew/Celebi,
- `0x71` if either Pokémon is Mew,
- `0x72` if either is Celebi.

If the value is not `0`, GSC **repeats until it receives exactly the same
byte**. The GBA must do the same: send `0x70 + n` and wait for `0x70 + n`
(if `n != 0`). This is the Mew/Celebi anti-cloning check.

#### Gen I

There is no check byte. After the animation, GSC does another
`Serial_SyncAndExchangeNybble` with `0x60`, then re-exchanges the whole party.

### 3.11 Re-exchange after every trade

**Very important:** after every trade, both games **re-exchange the whole
party** (RN + party + patch list [+ mail]) to refresh the data.

- Gen II: `SaveAfterLinkTrade` → `jp Gen2ToGen2LinkComms`.
- Gen I: `SavePartyAndDexData` → `jp CableClub_DoBattleOrTradeAgain`.

The GBA must:

1. rebuild its party block from the updated party;
2. pre-fill the TX buffer;
3. stream it when the GB asks for it;
4. return to the trade menu.

### 3.12 Exit

- Gen II: the menu sends `0x7F`; when both send `0x7F`, GSC leaves
  `LinkCommunications` and returns to the Cable Club room.
- Gen I: `0x6F`; when both send it, it exits.

The GBA must wait for the echo, then **stop the GB session**:
disable `INTR_FLAG_SERIAL`, set `REG_SIOCNT = 0`, restore `gIntrTable[1]`
(`SerialIntr`), call `RestoreSerialTimer3IntrHandlers()` if applicable, and
return to the map with the normal warp.

---

## 4. Detection and generation profiles

### 4.1 Gen II profile (what Crystal sees)

- Gen II party format (48 bytes/mon).
- Species 1..251; Egg = `0xFD`.
- Moves 1..251 (Struggle = 165 cannot be offered).
- Items: Gen3 → Gen2 table; Mail forbidden.
- Eggs allowed.
- Mail block present in Trade Center.
- Restrictions:
  - no Bad Egg;
  - no Mail;
  - no species > 251;
  - no moves > 251;
  - Unown `?` and `!` (letters 26/27) do not exist in Gen II;
  - shiny Unown only I and V;
  - female shiny with a 7:1 ratio is not representable;
  - Deoxys, Spinda, Shedinja, etc. (>251) blocked.

### 4.2 Gen I profile (what RBY sees)

- Gen I party format (44 bytes/mon).
- Species: Gen I internal index (inverse `gen1_to_3_conv_table.bin`); only
  1..151.
- Moves 1..165; Struggle (165) not allowed.
- No Eggs.
- No Mail.
- “Items”: byte 7 is catch rate; the mapped Gen II item can be used for the
  round-trip, or 0 / base catch rate.
- Types: Gen I has no Dark/Steel. Only Magnemite/Magneton change
  (Electric/Steel in Gen II → Electric in Gen I). The rest of species ≤151 have
  no new types.
- GSC Time Capsule restrictions (`CheckTimeCapsuleCompatibility`):
  1. species ≥ 152 → “Sorry--[MON] can't be taken.”
  2. move > 165 → “You can't take the [MON] with a [MOVE].”
  3. mail → “You can't take the [MON] that has MAIL with you.”
  - Additionally: no Eggs, no Bad Egg, no female shiny 7:1, no Unown (does not
    exist).

### 4.3 Recommended messages

Crystal's lines, adapted to the Emerald attendant tone (Emerald uses `Sorry,`
and `undergoing adjustments`; it never uses Crystal's `--` in Cable Club text):

| Situation | Text |
|---|---|
| No Pokédex (2F still closed) | `I'm terribly sorry. The TIME CAPSULE is undergoing adjustments now.` |
| Colosseum selected but unavailable | `It appears to be undergoing adjustments…` |
| Species does not exist in the target gen | `Sorry, {MON} can't be taken to the past. It didn't exist back then.` |
| Move does not exist in the target gen | `You can't take the {MON} with a {MOVE}. It didn't exist back then.` |
| Mail | `You can't take the {MON} that has MAIL with you. Please take the MAIL off first.` |
| Egg (Gen I only) | `Sorry, an EGG can't be taken to the past. Please leave it in the PC first.` |
| Impossible shiny (female 7:1, other Unown) | `Sorry, this POKéMON can't be taken to the past. It couldn't exist back then.` |

The Crystal originals are `Text_TimeCapsuleClosed`, `Text_RejectNewMon`,
`Text_RejectMonWithNewMove` and `Text_RejectMonWithMail` in
`pokecrystal/maps/Pokecenter2F.asm`.

The Pokédex gate is not new: Emerald's Union Room attendant and Direct Corner
attendant also require `FLAG_SYS_POKEDEX_GET` and answer with
`gText_SorryWirelessClubAdjustments` (`The POKéMON WIRELESS CLUB is undergoing
adjustments now.`). The Time Capsule simply uses the same rule with its own
facility name.

---

## 5. Conversion

### 5.1 General strategy

Conversion must be **round-trip** (there and back), not just “legal in Gen 3”.
Anything not representable is lost, but as much as possible must be preserved:

- species, level, exp, moves, PP, PP Ups;
- nature (exp trick, see §5.4);
- gender, shininess, Unown letter;
- IVs/DVs (1-bit loss);
- EVs/stat exp (precision loss);
- item (if it exists in the target; in Gen I it goes in the catch-rate byte);
- friendship, Pokérus;
- name, OT, language.

Lost: exact PID, ribbons, contest stats, mail, ball, detailed met location,
non-representable forms.

### 5.2 Gen3 → Gen2

1. **Validate** (see §4.1).
2. **Species**: `species` 1..251. If > 251 → error.
3. **Moves**: for each move, if `move <= 251` and `move != STRUGGLE`, keep it;
   otherwise drop it. If none remain → error (“no valid moves”).
   PP = base PP + PP Ups (max 40 in Gen II), encoded in the PP byte (bits 6-7 =
   PP Ups).
4. **Item**: Gen3 → Gen2 table. Mail → error. No equivalent → 0.
5. **OT ID**: the low 16 bits of the Gen3 OT ID (TID), big-endian.
6. **Level/exp**: level = Gen3 level (2..100). Exp = Gen3 exp adjusted so the
   calculated level is the same. The **nature is stored in exp** (see §5.4).
7. **EVs → stat exp**: `statExp = EV^2` per stat (max 65025, fits in u16).
   For Special: `statExp = max(SpAtkEV, SpDefEV)^2`.
8. **IVs → DVs**: `DV = IV >> 1` (0..15), with adjustments for:
   - **Gen II shininess**: atk DV bit 1 = 1, def/spa/spe = 10;
   - **gender**: the Gen II threshold is based on the atk DV;
   - **Unown**: the Gen II letter is based on the DVs.
   If not representable (female shiny 7:1, shiny Unown not I/V), error.
9. **Pokérus**: convert and sanitize (strain 0..8, valid days).
10. **Friendship**: 70 by default; for Eggs, use the hatch cycles.
11. **Met data**: Gen II `caught data` with location/level/time/gender. For
    normal Pokémon GSC uses defaults; location=1, level=1, time=1, cleared if
    the OT is male.
12. **Text**: OT name and nickname with Gen3 → Gen1/2 tables. Truncate to 10
    (INT) or 5 (JP). Unsupported characters → `?` or the default name.
13. **Egg**: if Egg, species list = `0xFD`, `is_egg = 1`, current HP = 0,
    level = 5, friendship = hatch cycles.

### 5.3 Gen3 → Gen1

Same as Gen2, with:

- species → Gen I internal index (inverse table);
- moves ≤ 165;
- no Eggs (error);
- no Mail (error);
- Gen I types (Magnemite/Magneton → Electric);
- Gen I stats (single Special = Gen3 SpAtk);
- byte 7 = mapped Gen2 item (or catch rate).
- Names/OT converted the same way.

### 5.4 Nature in exp

Gen I/II have no nature. The solution used by PCCS and the official VC
conversion is to **encode the nature in the exp**:

```
nature = exp % 25
adjusted_exp = exp + (desired_nature - nature)   // +/- up to 24
```

On the way back to Gen3, `nature = exp % 25`, then the exp is normalised to the
corresponding level. It is invisible to the player and enables round-tripping.

`Pokemon-Gen3-to-Gen-X` does this in `convert_exp_nature_of_gen3()` and
`get_exp_nature()`.

### 5.5 Gen2/Gen1 → Gen3

1. **Validate** (species index, moves, Gen I types).
2. **Create the Pokémon** with `CreateMon`/`CreateBoxMon` using:
   - national species (Gen2) or Gen1 → national table;
   - level;
   - the GB OT ID;
   - a PID generated so that **nature, gender, shininess and Unown** match;
   - IVs = DV × 2 (+1 if needed for PID consistency).
3. **Exp**: read the Gen2 exp, extract the nature (`exp % 25`) and normalise the
   exp to the level. If level 100, clamp to the valid range.
4. **EVs**: `EV = floor(sqrt(statExp) / 4) * 4` (PCCS formula). SpDef = SpAtk.
   Then cap at 510 total and 255 per stat (lowering the largest).
   - Round-trip alternative: if we generate stat exp as `EV^2`, this formula
     recovers the original EV (multiple of 4).
5. **Moves/PP**: copy, sanitize PP Ups.
6. **Item**: Gen2 → Gen3 table. In Gen I, byte 7 is interpreted as a Gen2 item;
   if invalid, 0.
7. **Friendship**: 70 (or the Egg value).
8. **Pokérus**: convert/sanitize.
9. **OT/nickname**: Gen1/2 → Gen3 tables; truncate to 7/10 (Gen3 INT) or 5 (JP).
   If the name is empty or has too many `?`, use the species default name.
10. **Met data**: `METLOC_IN_GAME_TRADE` (0xFE) or a new “Time Capsule”
    location; `met level` = current level; `met game` = FRLG/HeartGold (design
    choice); ball = Poké Ball.
11. **Egg**: `isEgg = 1`, `friendship` = cycles, level 5, no invalid moves.

### 5.6 PID/IVs: options

**Option A (fast, recommended for v1):**
use `GetMonPersonality(species, gender, nature, unownLetter)` from
`pokeemerald-expansion` and loop until `IsMonShiny` matches the desired
shininess. For non-shiny, just ensure it is not shiny. It is simple and enough
for play; the PID will not be “legal” in the PCCS sense, but the game does not
validate it.

**Option B (PCCS-style legality):**
port `source/pid_iv_tid.c` from `Pokemon-Gen3-to-Gen-X` (MIT). It generates
PID/IVs that reproduce nature, gender, shininess and Unown with Gen3 origin
structure (Egg/static/roamer/Unown). It is much more code (1300 lines) and
depends on encounter tables; only needed if the Pokémon must be
indistinguishable from a legal one.

For the Time Capsule, Option A is enough; Option B can be added later behind a
flag.

### 5.7 Required data tables

| Table | Source / generation |
|---|---|
| Gen I internal species ↔ national | `Pokemon-Gen3-to-Gen-X/data/gen1_to_3_conv_table.bin` (256 B) and its inverse |
| Gen II species | national 1..251; Egg `0xFD`; empty `0xFF` |
| Gen3 ↔ Gen2 moves | IDs 1..354 are the same as Gen3; only filter ≤251 |
| Gen1 moves | IDs 1..165; Struggle 165 invalid |
| Gen3 → Gen2 items | **generate by name** (expansion item IDs are not Gen3 IDs) |
| Gen2 → Gen3 items | inverse of the above |
| Invalid items | bitmap of items that cannot be held |
| Gen3 ↔ Gen1/2 text INT | `text_gen3_to_gen12_int.bin`, `text_gen12_to_gen3_int.bin` |
| Gen3 ↔ Gen1/2 text JP | `text_gen3_to_gen12_jp.bin`, `text_gen12_to_gen3_jp.bin` |
| Gen II gender | `pokemon_gender_bin` (per-species thresholds) |
| Gen I/II base stats | `pokemon_stats_bin`, `pokemon_stats_gen1_bin` (or `gSpeciesInfo`) |
| Exp | `gExperienceTables` from the expansion |
| Unown | `get_unown_letter_gen2()` / `GET_UNOWN_LETTER()` |
| Gen2 special encounters | `special_encounters_gen2_bin` (optional, met data) |

**Items:** the expansion uses its own IDs (`ITEM_POKE_BALL = 1`,
`ITEM_POTION = 28`, etc.), not Gen3 IDs. A table must be generated by
**name** (`gItemsInfo[].name`) against the Gen2 item list (about 0..0xAD) and
Gen1 (catch rate). A build script can generate the `.h`.

**Moves:** in the expansion, IDs 1..354 match Gen3 (`MOVE_PSYCHIC = 94`,
`MOVE_SHADOW_BALL = 247`, `MOVE_DRAGON_CLAW = 337`), so no Gen3 → Gen2 move
table is needed; only the range filter. Verify that no move in 1..354 has been
renumbered in the expansion.

**Species:** in the expansion, IDs 1..251 match the national dex
(`SPECIES_BULBASAUR = 1`, `SPECIES_MEW = 151`, `SPECIES_CELEBI = 251`). Regional
forms/megas have IDs >251 and are blocked automatically.

---

## 6. Integration into `pokeemerald-expansion`

### 6.1 Proposed new files

```
include/config/time_capsule.h          // build flags
include/time_capsule.h                 // public API
include/time_capsule/gb_link.h         // SIO driver
include/time_capsule/gb_protocol.h     // state machine
include/time_capsule/gb_convert.h      // conversion
include/time_capsule/gb_trade_link.h   // trade backend

src/time_capsule/gb_link.c             // SIO + IRQ + queues (IWRAM)
src/time_capsule/gb_protocol.c         // handshake, room, party, trade menu
src/time_capsule/gb_convert.c          // Gen3 ↔ Gen1/2
src/time_capsule/gb_trade_link.c       // trade.c adapter
src/time_capsule/time_capsule.c        // NPC, connection task, session
src/time_capsule/time_capsule_trade.c  // GB trade menu (or hooks)
src/time_capsule/time_capsule_battle.c // Colosseum (phase 2)

src/data/time_capsule/*.h              // generated tables
data/scripts/time_capsule.inc          // NPC and room scripts
data/text/time_capsule.inc             // texts
test/time_capsule.c                    // conversion/protocol tests
```

The `Makefile` already globs `src/*.c`, `src/*/*.c`, `test/*.c`, so no source
list changes are needed.

### 6.2 New NPC and maps

- **16 Hoenn** `*_PokemonCenter_2F` maps (Mauville, Dewford, etc.).
- Recommended coordinate: **`(11, 2)`**, elevation **0**, facing down.
  The current Cable Club attendant is at `(10, 2)`; `(11, 2)` is the free tile
  to its right (verified in `data/layouts/PokemonCenter_2F/map.bin`: metatile
  719, collision 0, elevation 0). `(12, 2)` is a wall; `(13, 2)` is free but
  separated by the wall, so it is not suitable for a second adjacent NPC.
- Sprite: `OBJ_EVENT_GFX_TEALA`, matching the other Cable Club attendants
  (Bill's field sprite is not visible in this context).
- Shared script: `Common_EventScript_TimeCapsuleAttendant`.
- Add the object to each `map.json`:

```json
{
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
  "flag": "0"
}
```

- The door is the same as the Cable Club: `opendoor 9, 1` and
  `warp MAP_TRADE_CENTER, 5, 8` (or `MAP_BATTLE_COLOSSEUM_2P, 6, 8`).
- The NPC is **independent** from the Cable Club attendant: its script does not
  call `TryTradeLinkup` or `OpenLink`.

### 6.3 Map states

Add to `include/constants/cable_club.h`:

```c
#define USING_TIME_CAPSULE_TRADE  10
#define USING_TIME_CAPSULE_BATTLE 11
```

And in `data/scripts/cable_club.inc`:

- `CableClub_OnWarp`: add `map_script_2 VAR_CABLE_CLUB_STATE,
  USING_TIME_CAPSULE_TRADE, CableClub_EventScript_CheckTurnAttendant` and the
  same for battle.
- `CableClub_OnLoad`: add cases calling
  `CableClub_EventScript_OpenDirectCornerBarrier`.
- `CableClub_OnFrame`: add cases calling
  `TimeCapsule_EventScript_ExitLinkRoom` (new), **not**
  `CableClub_EventScript_ExitLinkRoom` (which calls `CloseLink` and would reset
  the GBA SIO, killing the GB session).

`TimeCapsule_EventScript_ExitLinkRoom` must:

1. `special TimeCapsule_CloseLink` (send `0x7F`/`0x6F`, wait for echo, turn off
   SIO, restore handlers);
2. `setvar VAR_CABLE_CLUB_STATE, 0`;
3. reproduce the exit movement and close the barrier, as the Cable Club does.

### 6.4 NPC script

```asm
Common_EventScript_TimeCapsuleAttendant::
    lock
    faceplayer
    goto_if_unset FLAG_SYS_POKEDEX_GET, TimeCapsule_EventScript_NeedPokedex
    msgbox TimeCapsule_Text_Intro, MSGBOX_DEFAULT
    multichoice 0, 0, MULTI_CABLE_CLUB_NO_RECORD_MIX, FALSE
    switch VAR_RESULT
    case 0, TimeCapsule_EventScript_Trade
    case 1, TimeCapsule_EventScript_Battle
    case 2, TimeCapsule_EventScript_Exit
    case MULTI_B_PRESSED, TimeCapsule_EventScript_Exit
    end

TimeCapsule_EventScript_Trade::
    setvar VAR_0x8004, TIME_CAPSULE_MODE_TRADE
    goto TimeCapsule_EventScript_Connect

TimeCapsule_EventScript_Battle::
    setvar VAR_0x8004, TIME_CAPSULE_MODE_BATTLE
    goto TimeCapsule_EventScript_Connect

TimeCapsule_EventScript_Connect::
    call Common_EventScript_SaveGame
    goto_if_eq VAR_RESULT, 0, TimeCapsule_EventScript_Abort
    message gText_PleaseWaitForLink
    waitmessage
    special TimeCapsule_TryConnect          @ waitstate
    switch VAR_RESULT
    case TIME_CAPSULE_OK, TimeCapsule_EventScript_EnterRoom
    case TIME_CAPSULE_ERR_NO_LINK, TimeCapsule_EventScript_NoLink
    case TIME_CAPSULE_ERR_TIMEOUT, TimeCapsule_EventScript_Timeout
    case TIME_CAPSULE_ERR_WRONG_ROOM, TimeCapsule_EventScript_WrongRoom
    case TIME_CAPSULE_ERR_SPECIES, TimeCapsule_EventScript_BadSpecies
    case TIME_CAPSULE_ERR_MOVE, TimeCapsule_EventScript_BadMove
    case TIME_CAPSULE_ERR_MAIL, TimeCapsule_EventScript_BadMail
    case TIME_CAPSULE_ERR_EGG, TimeCapsule_EventScript_BadEgg
    goto TimeCapsule_EventScript_Abort
    end

TimeCapsule_EventScript_EnterRoom::
    special HealPlayerParty
    setvar VAR_0x8004, USING_TIME_CAPSULE_TRADE
    copyvar VAR_CABLE_CLUB_STATE, VAR_0x8004
    call CableClub_EventScript_OpenDirectCornerBarrier
    special DrawWholeMapView
    playse SE_CLICK
    delay 60
    applymovement LOCALID_PLAYER, Movement_PlayerApproachLinkRoomLeft
    waitmovement 0
    opendoor 9, 1
    waitdooranim
    applymovement LOCALID_PLAYER, Movement_PlayerEnterLinkRoom
    waitmovement 0
    hideplayer
    closedoor 9, 1
    waitdooranim
    special SetCableClubWarp
    warp MAP_TRADE_CENTER, 5, 8
    special DoTimeCapsuleWarp
    end
```

> `MULTI_CABLE_CLUB_NO_RECORD_MIX` already offers `TRADE CENTER / COLOSSEUM /
> EXIT`. No new multichoice is needed.

**Dialogue flow.** The NPC mirrors the Cable Club attendants, in the same order:

1. welcome + service selection (`message`/`waitmessage`/`delay 28`, like the
   Cable Club welcome);
2. party requirement (`CableClub_Text_NeedTwoMonsToTrade`);
3. save;
4. `gText_PleaseWaitForLink` (“Please wait. … … B Button: Cancel”) while the
   generation is detected and the GB picks its room;
5. **after the link is established** (room byte `0xD1/0xD2` or `0xD4/0xD5`
   received), `gText_ConfirmLinkWhenPlayersReady` (`A Button: Confirm` /
   `B Button: Cancel`);
6. `gText_ConfirmStartLinkWithXPlayers` with 2 players (the GBA + the other
   game);
7. `gText_AwaitingLinkup` while the party block is received (`B Button:
   Cancel`);
8. on success, `CableClub_Text_PleaseEnter`;
9. on no link, `CableClub_Text_OtherTrainerNotReady`;
10. on a link error, `Text_LinkErrorPleaseReset`;
11. on a room mismatch, `Text_PlayersMadeDifferentSelections`;
12. on cancel/other aborts, `CableClub_Text_PleaseVisitAgain`;
13. when leaving the room, `CableClub_Text_HopeToSeeYouAgain`.

The two confirmation prompts only appear once the GB link is really up, which is
the same point at which the GBA Cable Club shows them.  The protocol keeps
running while they are on screen, so the GB is answered and neither side times
out; the prompt task aborts if the link dies meanwhile.

`TimeCapsule_TryConnect` must be a `special` with `waitstate=1`: it creates a
task that advances the state machine and, when done, writes `gSpecialVar_Result`
and calls `ScriptContext_Enable()` (the `Task_LinkupStart` pattern).

### 6.5 `DoTimeCapsuleWarp`

`DoCableClubWarp` fades and loads the map, but on return it uses
`CB2_ReturnToFieldCableClub`, which resets the multiplayer state. For GB, use a
dedicated warp:

- `LockPlayerFieldControls()`;
- `TryFadeOutOldMapMusic()`;
- `WarpFadeOutScreen()`;
- `CreateTask(Task_DoTimeCapsuleWarp, 10)`;
- in the task: `WarpIntoMap(); SetMainCallback2(CB2_ReturnToField);`
  (or a minimal callback that does not touch SIO).

### 6.6 Starting the trade from the room

`PlayerEnteredTradeSeat()` in `src/cable_club.c` must check
`gTimeCapsuleActive`:

```c
void PlayerEnteredTradeSeat(void)
{
    if (gTimeCapsuleActive)
        CreateTask_EnterCableClubSeat(Task_StartTimeCapsuleTrade);
    else if (gWirelessCommType != 0)
        CreateTask_EnterCableClubSeat(Task_StartWirelessTrade);
    else
        CreateTask_EnterCableClubSeat(Task_StartWiredTrade);
}
```

`Task_StartTimeCapsuleTrade`:

1. freezes controls and fades;
2. does **not** call `OpenLink`/`SetCloseLinkCallback`;
3. `SetMainCallback2(CB2_StartTimeCapsuleTradeMenu)`.

### 6.7 Trade backend

To avoid duplicating `trade.c`, abstract the link operations:

```c
// include/time_capsule/gb_trade_link.h
void TradeLink_Init(void);
void TradeLink_Send(u16 cmd, u16 arg);
u32  TradeLink_GetReceivedStatus(void);
u16 *TradeLink_GetReceivedData(u8 player);
void TradeLink_ResetReceivedFlags(void);
void TradeLink_RequestBlock(u8 size);
bool32 TradeLink_IsSendFinished(void);
```

- GBA implementation: wraps `SendBlock`, `GetBlockReceivedStatus`,
  `gBlockRecvBuffer`, `ResetBlockReceivedFlags`, `SendBlockRequest`.
- GB implementation: translates `LINKCMD_*` to `0x70+`/`0x60+` bytes and fills
  `gBlockRecvBuffer` with the GB's commands.

Concrete points to touch in `src/trade.c`:

| Location | Change |
|---|---|
| `CB2_CreateTradeMenu` cases 1/2/3/4 | In GB mode, skip `OpenLink`/`Task_WaitForLinkPlayerConnection`; the link is already established. |
| `BufferTradeParties` | In GB mode, fill `gParties[B_TRAINER_OPPONENT_A]` from the GB party block and return TRUE. |
| `CB1_UpdateLink` | In GB mode, call `GB_TradeLink_Update()`. |
| `Leader_ReadLinkBuffer`/`Follower_ReadLinkBuffer` | In GB mode, translate `0x70+idx`/`0x71`/`0x72`/`0x7F` to the existing `LINKCMD_*`. |
| `HandleLinkDataSend`/`HandleLinkDataReceive` (animation) | In GB mode, use the success byte `0x70+n` instead of `LINKCMD_READY_FINISH_TRADE`/`CONFIRM_FINISH_TRADE`. |
| `CB2_SaveAndEndTrade` | In GB mode, do not call `SetCloseLinkCallback`; save, then complete the GB handshake and return to the GB menu. |
| `CB2_FreeTradeAnim` | Restore `gMain.savedCallback` to the GB menu or exit. |

**Less invasive alternative:** a `time_capsule_trade.c` file duplicating the
needed parts of the menu. Faster to prototype, but duplicates ~1000 lines and
complicates maintenance. The abstraction is better.

### 6.8 `gLinkPlayers` and partner data

Before entering the menu, fill:

```c
gLinkPlayers[1].name          = GB trainer name (converted to Gen3)
gLinkPlayers[1].trainerId     = GB OT ID
gLinkPlayers[1].language      = detected language / GBA
gLinkPlayers[1].version       = VERSION_EMERALD
gLinkPlayers[1].progressFlags = 0
```

`GetMultiplayerId()` must return 0 so the menu uses `gLinkPlayers[1]` as the
partner. No real GBA session is needed.

### 6.9 Saving and session order

Recommended order to prevent the GB from streaming while the GBA writes flash:

1. handshake + detection + room selection;
2. check party compatibility;
3. save the GBA game (if not already done);
4. warp to the room;
5. the player sits → initial RN + party + patch list + mail exchange;
6. trade menu (offers/accept);
7. animation;
8. **save the GBA before sending the success byte**;
9. send `0x70+n` / `0x60` and wait for the GB's;
10. re-exchange the party;
11. return to the menu or exit.

This way, when the GB returns to `Gen2ToGen2LinkComms` /
`CableClub_DoBattleOrTradeAgain`, the GBA already has the TX buffer ready and
is not writing flash.

### 6.10 Configuration

`include/config/time_capsule.h`:

```c
#define TIME_CAPSULE_ENABLED             TRUE
#define TIME_CAPSULE_SUPPORT_GEN1        TRUE
#define TIME_CAPSULE_SUPPORT_GEN2        TRUE
#define TIME_CAPSULE_COLOSSEUM_ENABLED   FALSE   // phase 2
#define TIME_CAPSULE_ALLOW_KOREAN        FALSE
#define TIME_CAPSULE_DEBUG_LOG           FALSE
#define TIME_CAPSULE_SAVE_BEFORE_TRADE   TRUE
#define TIME_CAPSULE_DEFAULT_MET_LOCATION METLOC_IN_GAME_TRADE
```

### 6.11 Specials

Add to `data/specials.inc`:

```asm
def_special TimeCapsule_TryConnect, waitstate=1
def_special TimeCapsule_CloseLink
def_special TimeCapsule_CheckParty
def_special TimeCapsule_GetGeneration
def_special TimeCapsule_StartBattle
```

And declare the functions in `include/time_capsule.h`.

---

## 7. Colosseum: feasibility and approach

### 7.1 What the battle protocol does

In a Gen II link battle:

1. both games exchange RN + party + patch list (same as trade, without mail);
2. `StartBattle` starts the local battle engine;
3. each turn, each game sends an action:
   - `0x80-0x83` = move 1-4,
   - `0x84-0x89` = switch to Pokémon 1-6,
   - `0x8E` = Struggle,
   - `0x8F` = flee/end;
4. the RNGs are synchronised (`wLinkBattleRNs`), so both engines compute the
   same result;
5. no damage or HP is sent: each game simulates the battle from the same inputs.

In Gen I the actions use base `0x60` (according to the
`PokemonGB_Online_Trades_and_Battles` notes), not `0x80`.

### 7.2 Why it is a separate project

The GBA would have to run an **exact Gen II battle engine** (and another Gen I
one) so the two sides do not desynchronise:

- Gen II type chart (Dark/Steel, effectiveness changes),
- damage formula, criticals, STAB, weather, status,
- turn order, priorities, items, abilities (none in Gen II),
- moves with special effects (Metronome, Mimic, Beat Up, Counter, etc.),
- PP management, Struggle, switching, fleeing,
- shared RNG.

The Gen III battle engine cannot be reused: the mechanics do not match.
Porting the `pokecrystal` engine is possible but amounts to months of work and
a huge bug surface.

### 7.3 Recommendation

- **v1:** complete Trade Center for Gen I and Gen II.
- **v1.1:** Colosseum Gen II, if the Gen II engine is accepted.
- **v2:** Colosseum Gen I (separate Gen I engine).

In the meantime, the NPC's COLOSSEUM option can show a clear message or be
hidden by config. Do not promise it for the first release.

### 7.4 Note on battle save order

Colosseum has no trade and no per-battle save; the flow is different. It can be
postponed without affecting Trade Center.

---

## 8. Testing

> The emulation environment is assumed to be able to connect the emulated GBA
> with the GB/GBC emulator. The plan does not depend on real cables.

### 8.1 Unit tests (no link)

In `test/time_capsule.c`, using the `pokeemerald-expansion` harness:

- Gen3 → Gen2 → Gen3 for every species 1..251:
  - species, level, exp, moves, PP, gender, shininess, Unown, friendship,
    Pokérus, item;
- Gen3 → Gen1 → Gen3 for every species 1..151;
- edge cases:
  - Mew/Celebi,
  - Unown A-Z, ?/!,
  - female shiny 7:1,
  - Egg,
  - mail,
  - level 100,
  - 252 EVs,
  - 31 IVs,
  - JP/INT names,
  - unsupported characters;
- patch list: blocks with many `0xFE`, offsets in both parts;
- big-endian serialisation of every Gen I/II field;
- exact block sizes (424/450/200/390/17).

### 8.2 Protocol tests with a simulated peer

Without real SIO:

- feed the state machine with byte traces recorded from
  `pokecrystal`/`pokered`/`Pokemon-Gen3-to-Gen-X`;
- verify the GBA replies with the correct sequence at every phase:
  connection, detection, room, timeout, party, patch list, mail, trade menu,
  success, re-exchange, exit;
- simulate byte loss and timeouts.

### 8.3 Emulator tests

- GBA emulated with the modified `pokeemerald-expansion`;
- GB/GBC emulated with Crystal (Gen II) and Red (Gen I);
- GBA ↔ GB link bridge (the one in the `mgba` folder or whatever the
  environment uses);
- test:
  1. Crystal from any Pokémon Center → Trade Center;
  2. Crystal → Colosseum (when it exists);
  3. Red/Blue/Yellow → Cable Club Trade Center;
  4. wrong room;
  5. cancel in the menu;
  6. two trades in a row;
  7. incompatible Pokémon (species/move/mail/Egg);
  8. disconnect mid-session;
  9. save and re-enter;
  10. clean exit.

### 8.4 Traces and debugging

- Add an optional log (`TIME_CAPSULE_DEBUG_LOG`) that writes TX/RX bytes into a
  buffer and shows them on screen or via `mgba_printf`.
- Keep traces of good sessions for regression tests.
- Measure VBlank time during the session to confirm no bytes are lost.

---

## 9. Risks and open questions

1. **Long VBlank**: the main technical risk. Mitigate with a light VBlank and
   IWRAM buffers. Measure in the emulator and on hardware if possible.
2. **Serial IRQ latency**: 8192 Hz leaves margin, but the handler must be short
   and must not call heavy ROM functions.
3. **JP language**: JP games use 6-byte names and a different charset. Detect
   by the position of the party count (byte 6 vs byte 11) and use the JP
   tables. Korean can be left out of v1.
4. **Items**: the name-based table is essential; expansion IDs do not match
   Gen3. Decide what to do with items that have no equivalent (0 / warning).
5. **Met location/origin**: decide between `METLOC_IN_GAME_TRADE`, a new
   “Time Capsule” location, or FRLG/HeartGold as “game of origin”.
6. **PID legality**: Option A does not generate a PCCS-legal PID. If that
   matters, port `pid_iv_tid.c` or PCCS.
7. **Gen II Eggs**: allowed; Gen I no. Ensure the menu cannot select an Egg for
   Gen I.
8. **Mew/Celebi**: the anti-cloning check must be implemented exactly, or the
   trade hangs.
9. **Mail**: always block; the Gen II mail block is sent empty.
10. **Colosseum**: do not promise it for v1; the battle engine is the big cost.
11. **Exit**: do not use the GBA `CloseLink`; it would reset SIO and lose the GB
    session. Use a dedicated GB shutdown.
12. **Saving**: save the GBA before the success byte so the re-exchange does not
    catch flash busy.

---

## 10. Phased roadmap

### Phase 0 — Skeleton and data
- `include/config/time_capsule.h`.
- Slave SIO driver + queues + IRQ.
- Loopback / simulated-peer tests.
- Gen1 species, item and text tables.

### Phase 1 — Handshake and detection
- `0x02` connection.
- `0x60`/`0x61` detection.
- Room selection `0xD1/0xD2` and `0xD4/0xD5`.
- `0x75/0x76` and `0x60` checks.
- Trace-based tests.

### Phase 2 — Party block and patch list
- Gen II and Gen I serialisation.
- Patch list.
- Empty mail block.
- GB `BufferTradeParties`.
- Verify Crystal displays the GBA party.

### Phase 3 — Trade menu
- Offers `0x70+i`/`0x60+i`.
- Accept/decline.
- Animation and success byte.
- Re-exchange.
- Exit.

### Phase 4 — Full conversion
- Gen3 → Gen2/Gen1.
- Gen2/Gen1 → Gen3.
- Restrictions and messages.
- Round-trip tests.

### Phase 5 — NPC, maps and polish
- 16 maps, script, texts.
- Map states and exit.
- Save and re-enter.
- QA with Crystal and Red.

### Phase 6 — Colosseum (optional)
- Gen II engine.
- `0x85/0x86` protocol + `0x80-0x8F` actions.
- Battle UI.
- Gen I engine (even more optional).

---

## 11. Quick reference

### Sequences

```
CONNECTION
  GB  -> 0x02 / 0x01 (alternates)
  GBA -> 0x02 (always)
  GB  -> 0x00 0x00

DETECTION
  Gen I  -> 0x60   ; GBA -> 0x60
  Gen II -> 0x61   ; GBA -> 0x61
  (force GSC Time Capsule mode: GBA -> 0x00)

ROOM
  Gen II: 0xD1 trade / 0xD2 colosseum
  Gen I : 0xD4 trade / 0xD5 colosseum

PRE-TRADE
  Gen II trade: 0x75, 0x76
  Gen II battle: 0x85, 0x86
  Gen I: 0x60, 0x60

BLOCKS
  RN: 7×0xFD + 10 bytes
  Party: 6×0xFD + 444 (Gen II) / 418 (Gen I)
  Patch: 3×0xFD + offsets + 0xFF (200 B)
  Mail: 5×0x20 + 198 + 84 + 103 (390 B, Gen II Trade Center only)

TRADE MENU
  Gen II: 0x70+i offer, 0x71 decline, 0x72 accept, 0x7F exit
  Gen I : 0x60+i offer, 0x61 decline, 0x62 accept, 0x6F exit
  idle: 0xFE

SUCCESS
  Gen II: 0x70+n (n=0 normal, 1 Mew, 2 Celebi), wait for echo
  Gen I : 0x60

EXIT
  Gen II: 0x7F
  Gen I : 0x6F
```

### Reference files

- `Pokemon-Gen3-to-Gen-X/source/sio.c`
- `Pokemon-Gen3-to-Gen-X/source/communicator.c`
- `Pokemon-Gen3-to-Gen-X/source/sio_buffers.c`
- `Pokemon-Gen3-to-Gen-X/source/gen_converter.c`
- `Pokemon-Gen3-to-Gen-X/source/gen12_methods.c`
- `Pokemon-Gen3-to-Gen-X/source/pid_iv_tid.c`
- `Poke_Transporter_GB/source/link_handler.cpp`
- `Poke_Transporter_GB/include/libraries/gba-link-connection/LinkSPI.hpp`
- `pokecrystal/home/serial.asm`
- `pokecrystal/engine/link/link.asm`
- `pokecrystal/engine/link/link_trade.asm`
- `pokecrystal/maps/Pokecenter2F.asm`
- `pokered/engine/link/cable_club.asm`
- `pokered/home/serial.asm`
- `pokered/engine/menus/main_menu.asm`
- `PokemonGB_Online_Trades_and_Battles/notes/gsc_trading_notes.txt`
- `PokemonGB_Online_Trades_and_Battles/utilities/gsc_trading.py`
- `PokemonGB_Online_Trades_and_Battles/utilities/gsc_battling.py`
- PCCS: https://github.com/Striaton-Lab-Team/Pokemon-Community-Conversion-Standard
