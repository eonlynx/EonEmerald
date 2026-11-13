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

## Implementation status (2026-11-13)

The Trade Center is implemented and matches the reference projects:

| Area | Implementation | Reference analogue |
|---|---|---|
| Link/state machine | `src/time_capsule/gb_protocol.c` | `communicator.c`, `sio.c` |
| Wire block / patch list / parser | `src/time_capsule/gb_convert.c` | `sio_buffers.c` |
| Pokémon conversion / validation | `src/time_capsule/gb_mon.c` | `gen_converter.c`, `gen12_methods.c`, `party_handler.c` |
| Low-level GBA SIO | `src/time_capsule/gb_link.c` | `sio.c`, Poke_Transporter_GB `link_handler.cpp` |
| Trade menu ↔ GBA `trade.c` | `src/time_capsule/gb_trade_link.c` | `main.c` trade menu states |
| Generated tables | `src/data/time_capsule/tables.h`, `src/time_capsule/gb_tables.c`, `item_map.h` | `data/*.bin` |
| Table generators | `tools/time_capsule/generate_tables.py`, `generate_item_map.py` | — |
| Protocol simulator | `tools/time_capsule/tests/sim_protocol.py` | — |

The link layer now waits for the GB's slave-ready line before every transfer
(`GbLink_WaitPeerReady`, SI low) and the mGBA bridge clears the armed flag after
a GB master probe.  Together these keep the GBA from clocking an unarmed GB,
which was the source of the `0xFE` storm and of the two consoles running many
exchanges apart.  The GBA also has **no handshake timeout**: Crystal may spend
time saving, walking to the console or waiting for its player, and the GBA must
not show its link error before the GB's own timeout.  Room-sync retries with
`0xD0`/`0xD4` are accepted and answered with the GBA's room byte.

The GBA is the **clock master** (normal 8-bit SIO, internal clock) and the GB
is the **external-clock slave**.  This is the configuration supported by the
mGBA `gblink` bridge in this repository, and the reference projects expose the
same role (`Pokemon-Gen3-to-Gen-X` starts its master routine with
`ENTER_TRADE_MASTER = 0x01`).  The byte cadence is paced by software at one
exchange per ~12 scanlines (~0.9 ms, 8192 bps).  The GBA also waits for the
GB's slave-ready signal (SI low, `GbLink_WaitPeerReady`) before each transfer,
so it never shifts bytes into an unarmed GB or runs ahead of the GB's serial
interrupt when the two emulator cores are scheduled independently.

The start-trade handshake is the reference master table:

```
send 0x01 0x61 0xD1 0x00      recv 0x61 0xD1 0x00 0x00   (enter room)
send 0x75 0x75 0x76 0xFE 0xFE recv 0x75 0x00 0xFD 0xFE 0xFE (start trade)
```

The critical detail is that the GBA starts presenting `0x76` after receiving
the GB's `0x00` acknowledgement of action 5, not after receiving the GB's own
`0x76`.  `CheckLinkTimeout_Gen2` reads the previous exchange's byte, so the
latter loses the race and Crystal reports "Too much time has elapsed.".

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

The forced-generation parameter was removed from the final API: the current
`GbProtocol_Init(room)` auto-detects Gen I vs Gen II from the first unambiguous
byte (`0x60`/`0xD4`/`0xD5` vs `0x61`/`0xD1`/`0x75`).  If a second NPC is wanted
later, it can pass a generation flag and restrict the profile; the protocol
itself does not need to change.


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

### 2.2 Clock roles: the implementation uses the GBA as master

The GBA normal SIO can run at **8192, 16384, 262144 or 524288 bps** with the
internal clock (SIOCNT bits 1:0), or it can accept an external clock.  The
GB/CGB Gen I/II games use **8192 bps** when they are the clock master.

On real hardware both roles work:

- **GBA slave / GB master**: the GB clocks one byte every ~0.98 ms; this is the
  default role of `Pokemon-Gen3-to-Gen-X` (`master = 0`).
- **GBA master / GB slave**: normal 8-bit mode with the internal clock runs at
  8192 bps (bits 1:0 = 0), i.e. one byte per ~1 ms.  The game software presents
  one byte per ~12 scanlines (~0.9 ms), matching that rate, so the GB has the
  rest of the byte time to service its serial IRQ.  The GB accepts the external
  clock in its normal 8192 bps mode.  This is the `master = 1` role of
  `Pokemon-Gen3-to-Gen-X` and the only role implemented by the mGBA `gblink`
  bridge in this repository.

`pokeemerald-expansion` uses **GBA master** because that is what the mGBA bridge
supports and because it keeps the GBA in control of the byte cadence.  The
software waits for the GB's slave-ready line and paces the transfers with
`GbLink_WaitLines(12)` (~0.9 ms), so the GB sees the same effective 8192 bps as
with a GB master.  The GB is told to use the external clock by sending `0x01`
as the first establishment byte (see the discovery sequence in §3.1).

### 2.3 Registers and configuration

For the GBA:

```c
REG_RCNT   = 0;                                  // Normal mode
REG_SIOCNT = SIO_8BIT_MODE | 0x0001;             // 8-bit, internal clock (master)
REG_SIODATA8 = tx;
REG_SIOCNT |= SIO_START;                         // one exchange, polled
```

- `SIO_8BIT_MODE` = `0x0000`.
- Bit 0 = internal clock (master).
- Bit 2 (`SI`) is driven by the GB: it is low while the GB has a byte armed in
  `SB`.  The game waits for it (`GbLink_WaitPeerReady`) before starting a
  transfer, like the reference's `sio_normal_inner_master` `SIO_RDY` check.
  The mGBA bridge refreshes this bit whenever `SIOCNT` is written.
- Bit 7 (`SIO_START`) is set to start the transfer; it clears itself after
  8 bits.  The game polls it (`GbLink_Poll`), so no serial IRQ is used and the
  VBlank handler is untouched.
- The transfer is started only after the GB is ready and
  `GbLink_WaitLines(12)` has elapsed, giving the GB time to arm its `SB` byte
  in its serial IRQ and enforcing the 8192 bps byte time.

`GbLink_Poll` returns `0xFE` (SERIAL_NO_DATA_BYTE) when the GB had not armed a
byte for that exchange; the protocol ignores it exactly like the reference.
With the SI check this should only happen if the GB disarms between the check
and the completion event.

### 2.4 Real-time behaviour and VBlank risk

At 8192 bps, one byte takes **≈0.98 ms**.  With the GBA as master the hardware
shift is ~30 µs, but the software intentionally spaces the bytes to ~1 ms so the
GB's game code (which uses `DelayFrame` between exchanges and has a
`wLinkByteTimeout` counter) behaves as on a GB-to-GB link.

Design measures actually used:

- No serial IRQ: `GbLink_Poll` polls `SIO_START` from the task.
- A dedicated task (`Task_TimeCapsuleLink`) pumps the protocol every frame, so
  it survives the confirmation prompts, the warp and the trade menu.
- The GBA save is performed while the protocol is paused (`0xFE`), so the GB is
  never streamed during flash writes.
- Lost bytes are retried: `0xFE` is ignored, and the protocol tables keep
  presenting the current byte until the peer answers.
- The GBA only clocks the bus after the GB has armed a byte (SI low).  This
  keeps the two emulator cores in lockstep even when they run in separate
  threads: the GBA cannot run several exchanges ahead and consume a stale
  `SB` value.  Without this check the GBA reads `0xFE` almost every time when
  the GB core is scheduled a whole frame at a time.
- The mGBA GB driver clears the armed flag when a master probe completes, so
  the probe byte (`0x01`/`0x02`) is never mistaken for a real slave answer.
- The initial discovery, receptionist, room sync and start-trade phases have
  no GBA-side timeout.  The GB controls those phases (it may be saving, walking
  to the console or waiting for its player) and has its own link timeout; the
  GBA player can always cancel with B.  Only the streaming block exchange is
  guarded for liveness.

### 2.5 Streaming vs. state machine

The implementation follows option 2 of the original plan: the exchange is
polled from the main loop, not from a serial IRQ.  `GbProtocol_Update()` performs
up to fourteen exchanges per frame (`GB_LINK_BYTES_FRAME`), each one paced by 12
scanlines (~0.9 ms, matching the reference's `VCOUNT_WAIT_TIME_NS`).  The state
machine and the stream buffers are in EWRAM; the low-level SIO driver is in
`gb_link.c`.


---

## 3. Link protocol

> All sequences are verified against `pokecrystal`/`pokered` and
> `Pokemon-Gen3-to-Gen-X`. Bytes are those travelling over the cable.

### 3.1 Initial connection

The GB game alternates between `0x01` and `0x02` during discovery:

- `0x01` = “use the external clock” (the sender is the clock master),
- `0x02` = “use the internal clock” (the sender asks the other side to be the
  master / the sender is the slave).

The implementation uses the **GBA as clock master**, so the GBA sends **`0x01`**
continuously (the value that tells the GB to use the external clock).  When the
GB is in its internal-clock probe phase, the mGBA GB driver clocks that `0x01`
into the GB, and Crystal sets `hSerialConnectionStatus = USING_EXTERNAL_CLOCK`.
The GBA may receive the GB's `0x02`/`0x01` probes; it ignores them in
`GB_STATE_CONNECT`.

Typical Gen II sequence (`WaitForLinkedFriend`):

```
GB   -> 0x02 (slave probe) ... timeout ...
GB   -> 0x01 (master probe)
GBA  -> 0x01
GB   -> hSerialConnectionStatus = USING_EXTERNAL_CLOCK
GB   -> 0x00, 0x00   (LinkDataReceived x2)
```

Typical Gen I sequence (`CableClubNPC`):

```
GB   -> 0x02 (slave probe) ... timeout ...
GB   -> 0x01 (master probe)
GBA  -> 0x01
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

`Link_EnsureSync` only checks the **high nibble** of the two received bytes
(`and $f0; cp $d0`), and Crystal retries the same call with `0xD0` (no room
selected) and, in `EnterTimeCapsule`, with `0xD4`.  The GBA therefore accepts
any `0xD0..0xDF` byte as a room-sync byte and answers with its own room byte
(`0xD0 | room`).  It only reports `TIME_CAPSULE_ERR_WRONG_ROOM` when a real
`0xD1`/`0xD2` selects a different trade room.

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
- `wLinkTimeoutFrames = 3` frames → `Link_CheckCommunicationError` →
  `WaitLinkTransfer` (send action 5, receive the peer's action 5)
- busy-wait ~`0xFFFF` cycles
- check `wOtherPlayerLinkMode == 5`
- `wPlayerLinkAction = 6` → sends `base + 6`
- `wLinkTimeoutFrames = 0x0132` frames → `WaitLinkTransfer` (send action 6,
  receive the peer's action 6)
- check `wOtherPlayerLinkMode == 6`

`base` depends on `wLinkMode`:

| Mode | Base | Bytes |
|---|---|---|
| Trade Center (`LINK_TRADECENTER`) | `0x70` | `0x75`, `0x76` |
| Colosseum (`LINK_COLOSSEUM`) | `0x80` | `0x85`, `0x86` |

The master table from `communicator.c` (the GBA is master) is:

```
send 0x75 0x75 0x76 0xFE 0xFE
recv 0x75 0x00 0xFD 0xFE 0xFE
```

The key timing detail: the GB's `WaitLinkTransfer` returns as soon as
`wOtherPlayerLinkAction != 0xFF`, and it reads `hSerialReceive` (the byte of the
**previous** exchange, because the GB is the external-clock slave).  If the GBA
waits for the GB's `0x76` before presenting its own, the GB reads the old
`0x75`, fails the action-6 check and restarts `CheckLinkTimeout_Gen2`; Crystal
then reports “Too much time has elapsed.”.

Therefore the GBA must start sending `0x76` when it receives the GB's `0x00`
acknowledgement of action 5 (the `sends[2]` entry), during the acknowledge
phase of the first `WaitLinkTransfer`.  By the time the GB checks action 6,
`hSerialReceive` already contains `0x76`.

#### Gen I

`Serial_SyncAndExchangeNybble` (base `0x60`):

- the GBA sends `0x60`, expects `0x60`;
- then `0x60`, expects `0xFD` (start of the random-number block);
- then the bulk exchange begins.

Gen I never verifies the anti-cloning byte; its re-exchange after a trade jumps
straight back into `Gen2ToGen1LinkComms`, which sends `0x60` and then the `0xFD`
preamble.


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

#### Confirmation order and the 0x71/0x72 ambiguity

Crystal records the last `0x7X` byte it reads during its selection exchange as
the partner's party slot, and on its confirmation screen it treats `0x71` as
cancel and every other `0x7X` as accept.  The GBA therefore uses two waiting
states:

- The detection exchange is the **last** one that presents our offer.  As soon
  as the partner's offer has been seen twice, switch the wire to `0xFE`.  If
  the offer (`0x70+slot`) stayed on the wire, Crystal's confirmation exchange
  would read it as an accept and start the trade before the GBA player has
  confirmed.  This was the cause of the "Crystal trades alone" bug: the log
  showed `0x70` being clocked while the GBA was still in `WAIT_ACK`.
- `TC_TRADE_WAIT_ACK`: both offers are known, but Crystal is still in its ten
  receive + ten acknowledge selection frames.  Send `0xFE` and hold any early
  GBA confirmation (`sAcceptPending`) until the first `0x00` acknowledge,
  then switch to `TC_TRADE_WAIT_CONFIRM`.  In this phase **only `0x00` is
  meaningful**: the repeated `0x7X` is still Crystal's selection byte and is
  ignored, so a `0x72` here cannot be mistaken for an accept.  If the `0x00`
  is lost, the bounded byte counter moves on to the confirmation phase, where
  the repeated answer is picked up normally.
- `TC_TRADE_WAIT_CONFIRM`: send `0xFE`, which `WaitLinkTransfer` ignores.
  Crystal may confirm first (`0x72`); remember it (`sPeerConfirmed`) and keep
  waiting.  When our player confirms, send `0x72` and report the partner
  confirmation immediately.  A Crystal cancel (`0x71`) is answered with
  `0x71`.  Only `0x71`/`0x72` are valid answers in this phase.

`sLastPeerAction`/`sReceivedOnce` must be reset when the phase changes: a
Crystal offer for slot 2 is `0x72`, the same value as its accept, so a stale
debounce would hide the confirmation forever.

The receive side is phase-scoped as strictly as the send side:

| Phase | Accepted bytes |
|---|---|
| Selection | `0x70..0x75` (slot), `0x7F` (exit) |
| Selection acknowledge | `0x00` only |
| Confirmation | `0x71` (cancel), `0x72` (accept) |
| Success | any `0x7X` (anti-cloning byte) |

`TcPhaseAccepts()` is the executable version of this table: it is the single
gate before a byte reaches the phase handler.  `TcTradeTxByte()` is its send
counterpart.

`GbTrade_RebuildOutBuffer()` is called after a local trade evolution; it
rebuilds the next party block from the evolved party, but only while the block
stream has not started (`TC_START_STA`/`TC_START_UNK`).

`SET_MONS_TO_TRADE` (and the other menu-only link commands) must not touch the
Gen I/II trade state: it is not a wire action.  Resetting the debounce or
clearing `sPeerConfirmed` there can lose an accept that Crystal has already
sent.

The output values are also separated by phase, not stored in one shared
"trade offer" variable:

| Phase | State variable | TX byte |
|---|---|---|
| Selection | `sSelectionOut` (party slot) | `0x70+slot` |
| Selection acknowledge | — | `0xFE` |
| Confirmation | `sConfirmOut` (accepted/declined) | `0x70+answer` or `0xFE` |
| Success | `sSuccessValue` (anti-cloning nybble) | `0x70+nybble` |

`TcTradeTxByte()` is the only place that turns a phase into a wire byte; the
inbound handlers only change the phase.  It is also re-evaluated before **every**
SIO transfer (`TcRefreshTradeTxByte()`), not only when a non-`0xFE` byte is
received.  This matters because Crystal's `WaitLinkTransfer` inspects the last
received byte when it starts the confirmation exchange: if the GB goes silent
after the selection, a stale `0x7X` left on the wire would be read as the
answer and Crystal would accept without the GBA player confirming.

This is what guarantees that the selection slot cannot be emitted as a
confirmation answer, and it mirrors the one-byte delay of the reference's
`get_offer`/`get_accept` functions.

This covers all four orderings (GBA/Crystal select first, GBA/Crystal confirm
first) and both dangerous slots (1 and 2); the protocol simulator tests them
explicitly, including Crystal entering its confirmation screen while the GBA
is still in `WAIT_ACK`.

The same value has different meanings depending on the phase, so the handlers
are selected by `sStartState` (enter/start/block/menu) and `sTradingState`
(menu sub-state), never by a global byte map:

| Phase | `0x70..0x75` | `0x75/0x76` | `0xFD` | `0xFE` |
|---|---|---|---|---|
| Receptionist / enter | — | `0x75` starts CheckLinkTimeout | — | idle |
| Start-trade table | — | `0x75/0x76` handshake | — | idle |
| Block exchange | party data | party data | chunk preamble | escaped data (patch list) |
| Trade menu selection | `0x70+slot` offer | — | — | idle |
| Trade menu confirm | `0x71` decline, `0x72` accept | — | — | idle |
| Success | `0x70..0x72` anti-cloning | `0x75` next CheckLinkTimeout | — | idle |

In particular `0x71` is a valid party slot during selection but a cancel on
the confirmation screen, and `0x75` is both the start-trade handshake and the
post-trade re-exchange.  The GBA never sends an offer while waiting for the
player's confirmation: `TcGetOffer` reports the partner's offer and moves to
`TC_TRADE_NO_INFO`, which presents `0xFE`; `TcGetAccept` only interprets
`0x71/0x72` after our player has confirmed.

The GB is allowed to answer with delay: `0xFE` is ignored by both
`WaitLinkTransfer` and `CheckLinkTimeout_Gen2`, and the GBA has no timeout
during discovery, the room handshake or the trade menu.  The GB link task is
kept alive through the animation and flash save exactly like the normal Cable
Club keeps its link task alive in `trade.c` (`CB2_LinkTrade` /
`CB2_SaveAndEndTrade`); the only bounded phase is the streaming block.

### 3.10 Post-trade confirmation and anti-cloning

#### Gen II

After the trade animation, GSC sends a check byte:

- `0x70` if neither Pokémon is Mew/Celebi,
- `0x71` if either Pokémon is Mew,
- `0x72` if either is Celebi.

If the value is not `0`, GSC **repeats until it receives exactly the same
byte**. The GBA must do the same: send `0x70 + n` and treat **two sightings of
a `0x7X` byte** as the success handshake being complete, exactly like the
reference's `get_success()`.

`0x75` is **not** part of that wait: it is the next `CheckLinkTimeout_Gen2`,
i.e. the start of the post-trade re-exchange.

**Order matters.** GSC does, in this order:

1. animation ends;
2. anti-cloning success exchange;
3. `SaveAfterLinkTrade`;
4. `jp Gen2ToGen2LinkComms` → `CheckLinkTimeout_Gen2` (0x75/0x76) and the next
   party block exchange.

The GBA must match that order.  Starting the success exchange only after the
GBA save (the previous implementation) makes our `0x70` arrive when GSC is
already in `CheckLinkTimeout_Gen2`; its action-5 check reads `0x76`/`0x70`
instead of `0x75` and reports **“Too much time has elapsed.”**  The current
flow is:

- `CB2_UpdateLinkTrade` starts the success exchange immediately after
  `TradeMons()` (`GbTrade_StartSuccess`);
- `CB2_TryLinkTradeEvolution` waits for two sightings (`GbTrade_IsSuccessDone`)
  before evolving;
- then it starts the re-exchange (`GbTrade_StartReexchange`) **before** the
  GBA save.  The link task keeps answering `0x75`/`0x76` and streaming the next
  party block during the evolution and the flash save, so GSC never times out.

The GB link task must stay alive through the animation, the evolution and the
save.  `CB2_UpdateLinkTrade`, `CB2_TryLinkTradeEvolution` and
`CB2_SaveAndEndTrade` call `TimeCapsule_EnsureLinkTask()`, because
`CB2_LinkTrade()`/`ResetTasks()` and the menu rebuilds destroy it.

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

The GBA waits for the echo and then stops the GB session
(`GbLink_Shutdown`), returning to the map with the normal warp.

### 3.13 Timeouts

The **protocol-critical** timeouts are the GB's, because it owns the timeout
counters in its link routines:

| Phase | GB timeout | GBA guard |
|---|---|---|
| Initial discovery / player menus | — | none (player-controlled; B cancels) |
| `CheckLinkTimeout_Gen2` action 5 | 3 frames | none (retried until the GB answers or the player cancels) |
| `CheckLinkTimeout_Gen2` action 6 | `0x0132` = 306 frames | none (same) |
| Room sync / `EnterTimeCapsule` | `Serial_ExchangeByte` byte timeout | none (the GB retries) |
| `Serial_ExchangeBytes` block stream | `SERIAL_LINK_BYTE_TIMEOUT = 0x5000` loop iterations | `GB_LINK_ALIVE_TIMEOUT` (3600 frames) |
| `WaitLinkTransfer` in the trade menu | 0 frames (waits forever) | none (`TC_START_DON` has no timer) |
| GB save after the trade | — | `GB_SUCCESS_TIMEOUT` (3600 frames) |
| Exit echo | — | `GB_EXIT_TIMEOUT` (180 frames) |

The GBA guards are liveness checks (console off, cable pulled), not protocol
timers.  Poke Transporter GB uses 2048 transfers for discovery and 180 for the
clock-confirm, but the real GSC games keep their link routines running while
the player saves, walks to the console or reads the receptionist text.  A timer
there aborts valid sessions: the previous 600-frame CONNECT guard fired while
Crystal was still discovering/saving and Emerald showed its link error before
the room was reached, and the 3600-frame room-wait guard fired while the GB
player had not pressed A yet.  The GBA must therefore wait in those phases.
Only the block stream, where both sides are actively sending, is bounded.

The mGBA GB driver also has a watchdog for the armed `SB` byte.  It is **60 s**
rather than the initial 2 s: the 2 s timeout fired while the GBA was still
initialising the trade menu and cleared an armed byte, losing one exchange and
leaving the two counters out of phase.  The watchdog is only a last-resort
cable check; the games' own link timeouts remain the protocol timers.


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
  - no Mail data (the mail block is sent empty);
  - no species > 251 / regional or alternative forms;
  - moves > 251 are **dropped**; if no valid move remains, the Pokémon is
    rejected;
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
2. **Species**: national 1..251; regional/alternative forms are rejected. If >
   251 → error.
3. **Moves**: for each move, if `move <= 251` and `move != STRUGGLE`, keep it;
   otherwise drop it. If none remain → error (“no valid moves”).  PP = base PP
   + 20% per PP Up, capped at `40 + 7*bonus` so the six-bit Gen II PP field and
   the two PP-Up bits cannot overflow.
4. **Item**: full `item_gen3_to_12` table by item name. Invalid held items,
   items above Gen III item 348, and items with no Gen II equivalent → 0.
5. **OT ID**: the low 16 bits of the Gen3 OT ID (TID), big-endian.
6. **Level/exp**: level = Gen3 level (2..100). Exp = the Gen3 exp adjusted so
   the calculated level is the same; the **nature is stored in the exp**
   remainder (see §5.4). Eggs use the level-5 minimum.
7. **EVs → stat exp**: sum the six Gen3 EVs; if ≥ 510 use `65535`, else
   `(total²) >> 2`.  The same value is written to all five Gen I/II stat-exp
   fields (the reference’s `convert_evs_of_gen3`).
8. **IVs → DVs**: `DV = IV >> 1` (0..15), with adjustments for:
   - **Gen II shininess**: atk DV bit 1 = 1, def/spa/spe = 10;
   - **gender**: the Gen II threshold is based on the atk DV;
   - **Unown**: the Gen II letter is based on the DVs.
   If not representable (female shiny 7:1, shiny Unown not I/V), error.
9. **Pokérus**: convert and sanitize (strain 0..8, valid days).
10. **Friendship**: 70 by default; for Eggs, use the hatch cycles.
11. **Met data**: if the OT is female, Gen II caught data with location 1,
    level 1, time 1, OT gender female; if male, all caught data is cleared
    (the reference’s `force_crystal` logic).
12. **Text**: OT name and nickname with the Gen3 → Gen1/2 **international**
    table. Truncate to 10. Japanese is not implemented.
13. **Egg**: if Egg, species list = `0xFD`, `is_egg = 1`, current HP = 0,
    level = 5, friendship = hatch cycles, exp = level-5 minimum.

### 5.3 Gen3 → Gen1

Same as Gen2, with:

- species → Gen I internal index (inverse table);
- moves ≤ 165;
- no Eggs (error);
- no Mail (error);
- Gen I types from `pokemon_types_gen1.bin` (Magnemite/Magneton → Electric);
- Gen I base stats from `pokemon_stats_gen1.bin` (single Special);
- byte 7 = mapped Gen2 item (the reference calls this field `item`; in the Gen I
  party struct it is the catch-rate byte);
- names/OT converted the same way.


### 5.4 Nature in exp

Gen I/II have no nature. The solution used by PCCS and the official VC
conversion is to **encode the nature in the exp**:

```
nature = exp % 25
adjusted_exp = exp + (desired_nature - nature)   // +/- up to 24
```

On the way back to Gen3, `nature = exp % 25`, then the exp is clamped to its
level range (`ClampGen12Exp`, the `get_proper_exp_pure` analogue). It is
invisible to the player and enables round-tripping.  The reference may adjust
the level slightly at levels < 5 so the remainder fits; the implementation
does the same (`GetGen12ExpWithNature` updates the level pointer).  At level 100
there is no room for the nature (the reference also loses it).

`Pokemon-Gen3-to-Gen-X` does this in `convert_exp_nature_of_gen3()` and
`get_exp_nature()`.

### 5.5 Gen2/Gen1 → Gen3

1. **Validate** (species index, moves, Gen I types, forms, mail, eggs for Gen I).
2. **Create the Pokémon** with `CreateMon` using:
   - national species (Gen2) or the Gen1 → national table;
   - level;
   - the GB OT ID (visible TID, SID = 0);
   - a PID generated so that **nature, gender, shininess and Unown** match;
   - IVs = DV × 2.
3. **Exp**: read the Gen2 exp, extract the nature (`exp % 25`) for the PID, and
   keep the exp clamped to its level range (`ClampGen12Exp`, the
   `get_proper_exp_pure` analogue). The nature is not recalculated from the
   level, so the original exp remainder survives.
4. **EVs**: the reference deliberately sets **0 EVs** when transferring from
   Gen I/II to Gen III (`convert_evs_to_gen3`): a direct stat-exp conversion
   would be atrocious for competitive play.
5. **Moves/PP**: compact valid moves, set the Gen3 base PP, copy the PP-Up bits
   into `pp_bonuses`, and drop moves that do not exist in the target.
6. **Item**: `item_gen12_to_3` via the generated name map.
7. **Friendship**: 70, or the Egg cycles for an Egg.
8. **Pokérus**: sanitize the Gen I/II strain/days.
9. **OT/nickname**: Gen1/2 → Gen3 tables; truncate to 7/10.  Japanese text is
   **not implemented**; the international tables are used.
10. **Met data**: `METLOC_IN_GAME_TRADE` (0xFE), Poke Ball, met level = current
    level, met game = the running game.
11. **Egg**: `isEgg = 1`, friendship = cycles, level 5, exp = level-5 minimum.

### 5.6 PID/IVs: options

**Option A (implemented):** `GetMonPersonality(species, gender, nature,
unownLetter)` from `pokeemerald-expansion`, looping until the Gen II shiny
condition matches.  It reproduces nature, gender, Unown letter and shininess;
the IVs are the DVs × 2, which is legal for a trade-origin Pokémon.

**Option B (not implemented):** port `source/pid_iv_tid.c` from
`Pokemon-Gen3-to-Gen-X` for a fully fabricated Gen3 origin (Egg/static/roamer/
Unown encounter, met location/ball).  It is 1300+ lines and depends on the
encounter tables.  Not needed for a trade whose met location is
`METLOC_IN_GAME_TRADE`.

### 5.7 Required data tables (all generated/committed)

| Table | Source / generation |
|---|---|
| Gen I internal species ↔ national | `gen1_to_3_conv_table.bin` and its inverse |
| Gen II species | national 1..251; Egg `0xFD`; empty `0xFF` |
| Gen3 ↔ Gen2 moves | IDs 1..354 match Gen3; only filter ≤251 |
| Gen1 moves | IDs 1..165; Struggle 165 invalid |
| Gen3 → Gen2 items | `item_gen3_to_12.bin` matched by item name against the expansion's item list; invalid held items stripped |
| Gen2 → Gen3 items | `item_gen12_to_3.bin` matched the same way |
| Invalid items | `invalid_held_items.bin` and `LAST_VALID_GEN_3_ITEM = 348` |
| Gen3 ↔ Gen1/2 text INT | `text_gen3_to_gen12_int.bin`, `text_gen12_to_gen3_int.bin` |
| Gen II gender | `pokemon_gender.bin` (per-species thresholds) |
| Gen I/II base stats | `pokemon_stats.bin` (445 × 6), `pokemon_stats_gen1.bin` (256 × 5) |
| Gen I/II/III move PP | `pokemon_moves_pp.bin` (355) |
| Exp | `gExperienceTables` from the expansion |
| Unown | `get_unown_letter_gen2()` / `GET_UNOWN_LETTER()` |

The generators are `tools/time_capsule/generate_tables.py` and
`tools/time_capsule/generate_item_map.py`; both are deterministic and record the
SHA-1 of every source `.bin` in the generated header.

**Moves:** the expansion keeps the Gen3 move IDs for 1..354, so no move table is
needed; only the range filter.

**Species:** the expansion keeps the national dex for 1..251
(`SPECIES_BULBASAUR = 1`, `SPECIES_CELEBI = 251`).  Regional/alternative forms
have different internal IDs and are rejected by the form check.


---

## 6. Integration into `pokeemerald-expansion`

### 6.1 Actual files

```
include/config/time_capsule.h          // build flags
include/time_capsule.h                 // public API
include/time_capsule/gb_link.h         // SIO driver
include/time_capsule/gb_protocol.h     // state machine
include/time_capsule/gb_convert.h      // wire block / parser
include/time_capsule/gb_mon.h          // mon conversion (internal)
include/time_capsule/gb_trade_link.h   // trade backend

src/time_capsule/gb_link.c             // SIO driver (polled)
src/time_capsule/gb_protocol.c         // handshake, room, block stream, trade menu
src/time_capsule/gb_convert.c          // wire block, patch list, parser
src/time_capsule/gb_mon.c              // Gen3 ↔ Gen1/2 mon conversion + validation
src/time_capsule/gb_trade_link.c       // trade.c adapter
src/time_capsule/time_capsule.c        // NPC, connection task, session

src/data/time_capsule/tables.h         // generated character/gender/stats/species tables
src/data/time_capsule/item_map.h       // generated item map
tools/time_capsule/generate_tables.py  // table generator
tools/time_capsule/generate_item_map.py// item-map generator
tools/time_capsule/tests/sim_protocol.py // byte-level protocol simulator
data/scripts/time_capsule.inc          // NPC and room scripts
data/text/time_capsule.inc             // texts
```

The `Makefile` already globs `src/*.c` and `src/*/*.c`, so adding
`gb_mon.c` needs no source-list change (it is picked up automatically).


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
    setvar VAR_0x8005, TIME_CAPSULE_ROOM_TRADE
    goto TimeCapsule_EventScript_Connect

TimeCapsule_EventScript_Battle::
    special TimeCapsule_StartBattle
    msgbox gText_UndergoingAdjustments, MSGBOX_DEFAULT
    goto TimeCapsule_EventScript_Abort

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
7. `CableClub_Text_PleaseEnter` and the warp into the trade room (both games
   are in their trade room from this point on);
8. in the room, at a chair, `gText_PleaseWaitForLink` (the same text the
   vanilla Cable Club shows while the player takes a seat) while the party
   block is received (`B Button: Cancel`); the sprite turns towards the
   machine; on success the trade menu opens;
9. the friend of the other game is already sitting at the other console when
   the player enters the room (`CableClub_EventScript_TooBusyToNotice`);
10. on no link, `CableClub_Text_OtherTrainerNotReady`;
11. on a link error, `Text_LinkErrorPleaseReset`;
12. on a room mismatch, `Text_PlayersMadeDifferentSelections`;
13. on cancel/other aborts, `CableClub_Text_PleaseVisitAgain`;
14. when the link is terminated (the other game left the room or the player
    asked for it), `Text_TerminateLinkPleaseWait` and the player is escorted
    out of the room (`EventScript_DoLinkRoomExit`);
15. when the other console is turned off or the cable is pulled, the vanilla
    black "Communication error" screen (`CB2_LinkError`);
16. when leaving the room, `CableClub_Text_HopeToSeeYouAgain`.

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
- in the task: `WarpIntoMap(); SetMainCallback2(CB2_LoadMap);`
  (or a minimal callback that does not touch SIO).

`CB2_ReturnToField` must **not** be used here: it only calls `ResumeMap()`, so
the tilesets and the tilemap of the new map are never loaded and the trade room
shows random tiles. `CB2_LoadMap` runs the full map load (`CB2_DoChangeMap` ->
`CB2_LoadMap2` -> `DoMapLoadLoop` -> `LoadMapInStepsLocal`) and still runs
`gFieldCallback`, so the link task can be recreated there.

### 6.6 Sitting down at the trade console

The Gen I/II trade room (pokecrystal's `maps/TradeCenter.asm` and
`maps/TimeCapsule.asm`) has two consoles in the middle of the room with a seat
on each side of them, and it draws the friend sitting at the other console
(`SPRITE_CHRIS` at (3,4) and (6,4), facing the machine). The GBA trade room
has the same layout: the seats are at (4,5) and (7,5) and the consoles at
(5,5) and (6,5).

The vanilla Cable Club forces the direction the player looks at when it takes
a seat (`GetDirectionForEventScript` in `src/overworld.c`:
`FACING_FORCED_RIGHT` for `EventScript_TradeCenter_Chair0` and
`FACING_FORCED_LEFT` for `EventScript_TradeCenter_Chair1`), so the sprite
faces the machine.

The GBA link can show the other player walking around the room, but the GB
cannot, so the friend is spawned already seated at the other console, as
soon as the player enters the room:

- the GBA is always the clock master, so the friend sits at the right
  console (7,5) facing left, like pokecrystal does for the player that uses
  the internal clock;
- the sprite is `OBJ_EVENT_GFX_RED` / `OBJ_EVENT_GFX_LEAF`, the ones the
  expansion already uses for a Gen I/II link partner
  (`GetFRLGAvatarGraphicsIdByGender`). The Gen I/II link protocol does not
  send the trainer's gender, so the sprite is a compile time option
  (`TIME_CAPSULE_PARTNER_IS_FEMALE`);
- it is a normal object event with the vanilla script
  `CableClub_EventScript_TooBusyToNotice` ("This TRAINER is too busy to
  notice…"), which is what the vanilla Cable Club answers when the other
  player is already seated in a trade.

### 6.6 Starting the trade from the room

Both games must be inside their trade room, including in the Time Capsule, so
the GBA warps into `MAP_TRADE_CENTER` right after the confirmation prompts (as
soon as the room byte `0xD1/0xD2` was acknowledged), exactly like the normal
Cable Club. The `0x75/0x76` handshake and the party block are exchanged from
the chairs, which is the GBA equivalent of Crystal pressing A at the console.

The `TimeCapsule_EventScript_AwaitLinkup` script (also used by
`EventScript_TradeCenter_Chair0..3` when `VAR_CABLE_CLUB_STATE ==
USING_TIME_CAPSULE_TRADE`) shows `gText_AwaitingLinkup`, waits for the block and
validates the GB party. On success it runs `special PlayerEnteredTradeSeat`.

`PlayerEnteredTradeSeat()` in `src/cable_club.c` must check
`gTimeCapsuleActive`:

```c
void PlayerEnteredTradeSeat(void)
{
    if (gTimeCapsuleActive)
        CreateTask(Task_StartTimeCapsuleTrade, 80);
    else if (gWirelessCommType != 0)
        CreateTask_EnterCableClubSeat(Task_StartWirelessTrade);
    else
        CreateTask_EnterCableClubSeat(Task_StartWiredTrade);
}
```

The time capsule path must **not** call `CreateTask_EnterCableClubSeat`:
`Task_EnterCableClubSeat` waits for `GetCableClubPartnersReady()`, which needs
`sPlayerLinkStates[local] == PLAYER_LINK_STATE_READY` and therefore a real Gen 3
link. There is none here, so the task would hang in `CABLE_SEAT_WAITING`.

`Task_StartTimeCapsuleTrade`:

1. freezes controls and fades;
2. does **not** call `OpenLink`/`SetCloseLinkCallback`;
3. `SetMainCallback2(CB2_StartCreateTradeMenu)` (the normal trade menu, with
   `BufferTradeParties` filling `gParties[B_TRAINER_OPPONENT_A]` from the GB
   block).

### 6.7 Trade backend

The implementation uses a small adapter instead of duplicating `trade.c`:

```c
// include/time_capsule/gb_trade_link.h
void GbTrade_Update(void);
bool8 GbTrade_SendBlock(const void *src, u16 size);
void GbTrade_StartSuccess(void);
bool8 GbTrade_IsSuccessDone(void);
void GbTrade_StartReexchange(void);
void GbTrade_RequestExit(void);
```

- `GbTrade_Update` translates the queued GB command into
  `gBlockRecvBuffer[1]` + `gBlockReceivedStatus[1]`, so the vanilla
  `Leader_ReadLinkBuffer` sees a normal partner block.
- `GbTrade_SendBlock` translates `LINKCMD_*` into `0x70+idx` / `0x71` / `0x72` /
  `0x7F` (or `0x60+` for Gen I), and fills `gBlockRecvBuffer[0]` with the
  player's own command (the leader reads its own slot back).
- `GbTrade_StartSuccess` starts the anti-cloning exchange right after the trade
  animation, matching GSC's order.
- `GbTrade_IsSuccessDone` is polled before evolving/saving.
- `GbTrade_StartReexchange` starts `CheckLinkTimeout_Gen2` and rebuilds the next
  party block **before** the GBA flash save; the link task keeps servicing it
  while the save runs.

Concrete points touched in `src/trade.c`:

| Location | Change |
|---|---|
| `CB2_CreateTradeMenu` | In GB mode, skip `OpenLink`/`Task_WaitForLinkPlayerConnection`; `gMain.state = 6`. |
| `BufferTradeParties` | In GB mode, wait for `GbProtocol_IsReady()` and use the party already parsed by `GbConvert_ParseTradeBlock`. |
| `CB1_UpdateLink` | In GB mode, call `GbTrade_Update()`; close the menu when `GbProtocol_IsExiting()`. |
| `SetReadyToTrade` | In GB mode, send `LINKCMD_READY_TO_TRADE` with the cursor (the normal Cable Club leader never sends it, so the GB would wait forever for the leader's offer). |
| `Leader_ReadLinkBuffer` | Unchanged: the adapter fills `gBlockRecvBuffer[1]` with `LINKCMD_*`. |
| `CB2_UpdateLinkTrade` | In GB mode, start the success exchange immediately after `TradeMons()`. |
| `CB2_TryLinkTradeEvolution` | In GB mode, wait for `GbTrade_IsSuccessDone()`, then start the re-exchange before the evolution/save. |
| `CB2_SaveAndEndTrade` | In GB mode, keep the link task alive during the flash save; the success/re-exchange were already started. |
| `CB2_FreeTradeAnim` | Returns to the normal trade menu; the re-exchange completes there or during the save. |


#### Trade menu bytes (Gen I/II)

The Gen I/II trade menu exchanges two bytes per round with the base `0x70`
(Gen II) or `0x60` (Gen I):

- the index of the selected Pokémon (0-5);
- the answer of the confirm screen: `1` = the trade was canceled, `2` = the
  trade was confirmed.

The index of the second Pokémon is `1` as well and the index of the third
Pokémon is `2` as well, so the screen the partner is on has to be tracked to
tell them apart.  The implementation keeps the reference's `trading_state`
(`HAVE_OFFER` / `HAVE_ACCEPT` / `HAVE_SUCCESS`), `received_once` and
`sLastPeerAction` debounce instead of a separate `sPartnerState`.

| `LINKCMD_*` (Gen 3)              | Gen I/II byte | Meaning |
|---|---|---|
| `LINKCMD_READY_TO_TRADE` (idx)  | `base + idx`  | the selected Pokémon |
| `LINKCMD_INIT_BLOCK`            | `base + 2`    | the trade was confirmed |
| `LINKCMD_READY_CANCEL_TRADE`    | `base + 1`    | the trade was declined, both stay in the room |
| `LINKCMD_PLAYER_CANCEL_TRADE`   | `base + 1`    | idem |
| `LINKCMD_PARTNER_CANCEL_TRADE`  | `base + 1`    | idem |
| `LINKCMD_REQUEST_CANCEL`        | `base + 0xF`  | the player leaves the room (the Gen I/II "leave the link" byte) |
| `LINKCMD_BOTH_CANCEL_TRADE`     | `base + 0xF`  | both of them leave the room |

`0xF` is the byte pokecrystal sends in `ExitLinkCommunications` and in the
CANCEL row of its party menu (`LinkTradePartymonMenuCheckCancel`): the partner
answers it with the same byte and both games leave the cable club room.

#### The room negotiation

Before the room is selected, the GB's `wLinkMode` is `LINK_NULL`, so its
receptionist action uses the **0x60** high nibble (`0x61` for action 1).  Once
the GB enters the Trade Center, `wLinkMode = LINK_TRADECENTER` and every trade
byte uses the **0x70** high nibble (`0x75`, `0x76`, `0x70+i`, ...).

The GB's `.Receive` only accepts a byte (and only then updates
`wOtherPlayerLinkAction`) when its high nibble matches the mode.  The GBA
therefore answers the receptionist with `0x61`, then the room sync with
`0xD1`:

```
GB: 0x61 (receptionist action 1) -> GBA: 0x61
GB: 0xD1 (Link_EnsureSync, room 1) -> GBA: 0xD1
GB: 0x00 0x00 (LinkDataReceived acks) -> GBA: 0x00 0x00
```

The GBA's enter-room table is the reference master table:

```
send 0x01 0x61 0xD1 0x00
recv 0x61 0xD1 0x00 0x00
```

`0x01` is the establishment byte that tells the GB to use the external clock;
it doubles as the first entry of the table.


#### The trade start handshake (0x75 / 0x76)

The Gen I/II trade does not start with the block directly.  pokecrystal's
`CheckLinkTimeout_Gen2` does two checks and the slave reads the byte of the
**previous** exchange (the slave is one exchange behind, because it queues its
byte in the exchange that follows the one it just received):

1. it sends its action `5` (`0x75`) and waits until the master sends `0x75`;
2. it sends its action `6` (`0x76`) and waits until the master sends `0x76`.

So the master has to send `0x76` **before** the slave reads it, which means it
must not wait for the slave's `0x76`.  The sequence is the one of
Pokemon-Gen3-to-GenX's master table
(`gen2_start_trade_start_trade_procedure`):

```
master: 0x75  ->  waits for 0x75
master: 0x75  ->  waits for 0x00 (the acknowledgement of the slave)
master: 0x76  ->  waits for 0xFD (the preamble of the block)
```

The GBA therefore sends `0x75` when the room sync is acknowledged, and `0x76`
when the GB acknowledges the start-trade byte (the `0x00` that follows its
`0x75`).  A master that only sends `0x76` after receiving the slave's `0x76`
makes pokecrystal read a `0x75` in its second check and report a timeout, so
the trade never starts.

Like a real Gen 3 link, the leader reads its own command back from slot 0
(`gBlockRecvBuffer[0]`), so `GbTrade_SendBlock` fills it in. Without it the
leader would never see that the player canceled the trade.

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

Required order to keep GSC's post-trade `CheckLinkTimeout_Gen2` alive:

1. handshake + detection + room selection;
2. check party compatibility;
3. save the GBA game (if not already done);
4. warp to the room;
5. the player sits → initial RN + party + patch list + mail exchange;
6. trade menu (offers/accept);
7. animation;
8. **success exchange immediately after the animation**, before the GBA save;
9. start the re-exchange (`CheckLinkTimeout_Gen2` + next party block) **before**
   the evolution/save;
10. evolve and save while the link task answers `0x75`/`0x76` and streams the
    next block;
11. return to the menu.

GSC saves and then immediately starts the next `CheckLinkTimeout_Gen2`.
Saving before our success byte makes that handshake late and produces “Too
much time has elapsed.” on the GB.  Starting the re-exchange before the save
also means the GB's next party block is already being serviced while the GBA
writes flash.

### 6.10 Configuration

`include/config/time_capsule.h`:

```c
// TRUE in development builds, FALSE under `make release` (NDEBUG).
#define TIME_CAPSULE_DEBUG_LOG           (!defined(NDEBUG))
#define TIME_CAPSULE_DEFAULT_MET_LOCATION METLOC_IN_GAME_TRADE
#define TIME_CAPSULE_PARTNER_IS_FEMALE   FALSE
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

The committed simulator is:

```
python tools/time_capsule/tests/sim_protocol.py
```

It models the GBA state machine, `CheckLinkTimeout_Gen2` (action 5 and 6,
including the acknowledge frames and the busy wait) and the three
`Serial_ExchangeBytes` chunks.  It asserts:

- `wOtherPlayerLinkMode == 5` after check 5;
- `wOtherPlayerLinkMode == 6` after check 6 (the 0x00 -> 0x76 timing);
- the 1041 payload bytes of the three chunks arrive exactly.

The script exits with a non-zero code on failure, so it can be wired into CI.

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

Implemented and no longer open:

- ~~GBA slave/VBlank bit loss~~: the implementation polls the SIO transfer from
  the main loop (no serial IRQ) and paces one byte per ~12 scanlines, gated by
  the GB's slave-ready line.
- ~~Item mapping~~: generated by name from `item_gen3_to_12.bin` /
  `item_gen12_to_3.bin`, with `invalid_held_items.bin` applied.
- ~~EV/stat exp~~: total-EV collapse (`convert_evs_of_gen3`) and zero EVs in
  the reverse direction, exactly like the reference.
- ~~PP~~: base PP + PP Ups with the six-bit cap.
- ~~Base stats~~: Gen I/II tables from the reference, not the expansion's
  modern values.
- ~~PID legality~~: Option A is in use; it reproduces nature, gender, Unown
  and shininess.  Fully fabricated Gen3 origins remain optional.
- ~~Mew/Celebi~~: the anti-cloning byte is sent after the GBA save and the
  re-handshake is caught from the GB's `0x75`.
- ~~Mail~~: blocked; the outgoing mail block is empty.
- ~~Save order~~: the stream is paused (`0xFE`) while the GBA writes flash.
- ~~Exit~~: dedicated `0x7F` exchange, not the GBA `CloseLink`.

Still open / intentionally out of scope:

1. **Japanese text**: the JP charset/name length differ (6 bytes); the
   international tables are always used.  A JP mode needs separate party
   struct sizes and language selection.
2. **Regional/alternative forms**: rejected; no Gen I/II equivalent.
3. **Nature at level 100**: there is no room to encode it in the exp (the
   reference also loses it at level 100).  Levels 2-4 with an exp range < 25
   are the other edge case.
4. **Special met data** (`special_encounters_gen2_bin`): not used; it is the
   reference's table of event-Pokémon met data (met location, level and a
   `force_crystal` flag).  Normal trades use the in-game-trade met data
   (location/level/time 1 for a female OT, cleared for a male OT), like the
   reference's `force_crystal` logic.
5. **Colosseum**: still a separate battle-engine project.
6. **GBA-master on real hardware**: the mGBA bridge is GBA-master; the
   reference's default is GBA-slave.  Normal 8-bit mode at 8192 bps is
   ~1 ms/byte, matching the GB, and the software waits for the GB's ready line
   before each transfer; a real-hardware test with the GBA as master is still
   recommended.


---

## 10. Phased roadmap (status)

| Phase | Status |
|---|---|
| 0 — Skeleton and data | **Done** (polled SIO, generated tables, simulator) |
| 1 — Handshake and detection | **Done** (0x01/0x61/0xD1/0x00, 0x75/0x76, Gen I) |
| 2 — Party block and patch list | **Done** (444/418 payloads, patch list, empty mail) |
| 3 — Trade menu | **Done** (offers 0x70+i/0x60+i, accept, success, re-exchange, exit) |
| 4 — Full conversion | **Done** (items, EVs, PP, stats, exp, validation) |
| 5 — NPC, maps and polish | **Done** (attendant, Trade Center warp, saves, messages) |
| 6 — Colosseum | **Pending** (battle engine + 0x85/0x86 protocol) |

Remaining work is concentrated in the “Still open” list of §9.  Colosseum is
explicitly not part of this implementation; the NPC shows the “under
adjustments” message.


---

## 11. Quick reference

### Sequences

```
CONNECTION
  GBA -> 0x01 (internal-clock master request, repeated until 0x61)
  GB  -> 0x02 / 0x01 (discovery probes, alternates)
  GB  -> 0x61 (receptionist action 1 for the Trade Center)

DETECTION
  Gen I  -> 0x60   ; GBA -> 0x60
  Gen II -> 0x61   ; GBA -> 0x01 until the next 0x61, then 0x61
  (force GSC Time Capsule mode: GBA -> 0x00)

ROOM
  Gen II: 0xD1 trade / 0xD2 colosseum
  Gen I : 0xD4 trade / 0xD5 colosseum

PRE-TRADE
  Gen II trade: 0x75 until 0x75, 0x75 until 0x00, then 0x76 until 0xFD
  Gen II battle: 0x85, 0x86 (not implemented)
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
  Gen II: 0x70+n (n=0 normal, 1 Mew, 2 Celebi); the trade is complete when the
          GB starts its next CheckLinkTimeout (0x75)
  Gen I : 0x60+n; then the GB returns with 0x60/0xFD

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
- `Pokemon-Gen3-to-Gen-X/source/party_handler.c`
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

Implementation files: `gb_protocol.c` (communicator.c), `gb_convert.c`
(sio_buffers.c), `gb_mon.c` (gen_converter.c/gen12_methods.c/party_handler.c),
`gb_link.c` (sio.c), `gb_trade_link.c` (main.c trade states).

