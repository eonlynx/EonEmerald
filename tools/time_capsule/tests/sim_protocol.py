#!/usr/bin/env python3
"""
Byte-level validation of the Time Capsule Gen II protocol.

This does not need a Crystal ROM.  It models:

  * the GBA master state machine of src/time_capsule/gb_protocol.c
    (check_if_continue / communicate_buffer with the reference tables);
  * pokecrystal's CheckLinkTimeout_Gen2 (actions 5 and 6, including the
    acknowledge frames and the busy wait);
  * pokecrystal's Serial_ExchangeBytes for the three Gen II Trade Center
    chunks (RNG, party, patch list + mail).

Run from the repository root:

    python tools/time_capsule/tests/sim_protocol.py

Exit code 0 means the handshake and all three chunks completed with the exact
payload.  It is intentionally deterministic: a regression in the tables or in
the 0x00 -> 0x76 timing makes it fail.
"""

import sys

SEND_NO_INFO = 0xFE
SYN = 0xFD
START = 0x75

UNK, ENT, WAI, STA, SYN_ST, PAR, DON = range(7)


class Gba:
    def __init__(self):
        self.sub = UNK
        self.idx = 0
        self.tx = 0x01
        self.enter_send = [0x01, 0x61, 0xD1, 0x00]
        self.enter_recv = [0x61, 0xD1, 0x00, 0x00]
        self.start_send = [START, START, 0x76, SEND_NO_INFO, SEND_NO_INFO]
        self.start_recv = [START, 0x00, SYN, SEND_NO_INFO, SEND_NO_INFO]
        self.sizes = [10, 444, 587]
        self.size_idx = 0
        self.bc = 0
        self.base = 0
        self.syn = 0
        self.has_syn = False
        self.bcout = 0
        self.in_data = []
        self.out_data = [0x11] * 10 + [0x22] * 444 + [0x33] * 587
        self.block_done = False
        self.established = False

    def check_if_continue(self, data, sends, recvs, size, new_state, new_send):
        if data == recvs[self.idx]:
            self.idx += 1
            if self.idx >= size or sends[self.idx] == SEND_NO_INFO:
                self.idx = 0
                self.sub = new_state
                return new_send
        return sends[self.idx]

    def communicate(self, data):
        if not self.has_syn:
            if self.syn >= 30:
                self.syn = 30
            if self.syn < 3:
                if data != SEND_NO_INFO and data == SYN:
                    self.syn += 1
                return SYN
            if data != SEND_NO_INFO and data != SYN:
                self.has_syn = True
            else:
                if data != SEND_NO_INFO:
                    self.syn += 1
                return SYN

        if data != SEND_NO_INFO:
            self.in_data.append(data)
            self.bc += 1
            if self.bc == self.sizes[self.size_idx]:
                self.base += self.sizes[self.size_idx]
                self.size_idx += 1
                self.has_syn = False
                self.syn = 0
                self.bc = 0
                self.bcout = 0
                if self.size_idx >= 3:
                    self.block_done = True
                    return SEND_NO_INFO
                if self.bcout < self.sizes[self.size_idx - 1]:
                    return self.out_data[self.base - 1]
                return 0

        self.bcout += 1
        if self.bcout > self.sizes[self.size_idx]:
            self.bcout = self.sizes[self.size_idx]
        return self.out_data[self.bcout - 1 + self.base]

    def idle(self, data):
        if data in (0x61, 0x62, 0x71, 0x72):
            self.idx = 0
            self.sub = ENT
            self.tx = 0x01
            return
        if (data & 0xF0) == 0xD0:
            # Link_EnsureSync accepts the whole 0xD0..0xDF range; the GBA
            # answers with its own room byte and waits for the two 0x00 acks.
            if data in (0xD1, 0xD2):
                self.established = True
            self.idx = 2
            self.sub = ENT
            self.tx = 0xD1
            return
        if data == START:
            self.idx = 0
            self.sub = STA
            self.tx = START
            return
        self.tx = 0x01 if self.sub == UNK else START

    def process(self, data):
        if self.sub == ENT:
            if self.established and data == START:
                # A late room-sync retry returned us to ENT after the GB had
                # already started CheckLinkTimeout_Gen2.
                self.idx = 0
                self.sub = STA
                self.tx = START
                return
            if self.idx == 0 and (data & 0xF0) == 0xD0:
                # Link_EnsureSync arrived before the receptionist byte.
                if data in (0xD1, 0xD2):
                    self.established = True
                self.idx = 2
                self.tx = 0xD1
                return
            if self.idx == 0 and data in (0x61, 0x62, 0x71, 0x72):
                data = 0x61
            elif self.idx == 1 and (data & 0xF0) == 0xD0:
                if data in (0xD1, 0xD2):
                    self.established = True
                data = 0xD1
            self.tx = self.check_if_continue(data, self.enter_send,
                                             self.enter_recv, 4, WAI, START)
        elif self.sub == STA:
            self.tx = self.check_if_continue(data, self.start_send,
                                             self.start_recv, 5, SYN_ST,
                                             SEND_NO_INFO)
        elif self.sub == SYN_ST:
            self.base = 0
            self.has_syn = False
            self.syn = 0
            self.bc = 0
            self.bcout = 0
            self.size_idx = 0
            self.sub = PAR
            self.tx = SEND_NO_INFO
        elif self.sub == PAR:
            self.tx = self.communicate(data)
        else:
            self.idle(data)


class Gb:
    """The parts of pokecrystal that matter for the protocol."""

    def __init__(self):
        self.sb = SEND_NO_INFO
        self.rx = 0xFF
        self.action = 0xFF
        self.mode = 0xFF
        self.pending = 0x70

    def exchange(self, gba_tx):
        rx = self.sb
        self.rx = gba_tx
        self.sb = SEND_NO_INFO
        return rx

    def link_transfer(self):
        # .Receive (hSerialReceive is the GBA byte of the previous exchange)
        self.mode = self.rx
        if (self.rx & 0xF0) == 0x70:
            self.action = self.rx & 0x0F
        # send wPlayerLinkAction | 0x70
        self.sb = 0x70 | self.pending
        # .Receive again, without a new clock in between
        self.mode = self.rx
        if (self.rx & 0xF0) == 0x70:
            self.action = self.rx & 0x0F

    def pump(self, gba, n=8):
        for _ in range(n):
            gba.process(self.exchange(gba.tx))

    def wait_link_transfer(self, gba, action, timeout_frames=20):
        self.action = 0xFF
        self.pending = action
        frames = 0
        while self.action == 0xFF and frames < timeout_frames:
            self.link_transfer()
            self.pump(gba)
            frames += 1
        # 10 receive frames
        for _ in range(10):
            self.link_transfer()
            self.pump(gba)
        # 10 acknowledge frames (LinkDataReceived writes 0)
        for _ in range(10):
            self.sb = 0x00
            self.pump(gba)
        # WaitLinkTransfer ends with
        # wOtherPlayerLinkMode = wOtherPlayerLinkAction
        self.mode = self.action
        return self.mode

    def busy_wait(self, gba, exchanges=64):
        for _ in range(exchanges):
            gba.process(self.exchange(gba.tx))


def test_handshake():
    gba = Gba()
    gb = Gb()

    # Receptionist action 1 (0x61), then Link_EnsureSync (0xD1), then the two
    # LinkDataReceived acks (0x00).
    gb.pending = 0x61
    gb.link_transfer()
    gb.pump(gba)
    gb.pending = 0x61
    gb.link_transfer()
    gb.pump(gba)
    for _ in range(4):
        gb.sb = 0xD1
        gb.pump(gba)
    for _ in range(2):
        gb.sb = 0x00
        gb.pump(gba)

    ok = gba.sub == WAI and gba.tx == START

    mode5 = gb.wait_link_transfer(gba, 5)
    ok = ok and mode5 == 5
    gb.busy_wait(gba)
    mode6 = gb.wait_link_transfer(gba, 6)
    ok = ok and mode6 == 6
    ok = ok and gba.established
    print("handshake: check5=%02X check6=%02X tx=%02X established=%u %s" %
          (mode5, mode6, gba.tx, gba.established, "OK" if ok else "FAIL"))
    return ok


def test_room_sync_retry():
    """Link_EnsureSync may retry with 0xD0/0xD4 before selecting 0xD1."""
    gba = Gba()
    gb = Gb()

    gb.pending = 0x61
    gb.link_transfer()
    gb.pump(gba)

    # Crystal retries the sync with the generic bytes (no room selected and
    # EnterTimeCapsule) while the GBA is already waiting for the 0x00 acks.
    for value in (0xD1, 0xD4, 0xD1):
        gb.sb = value
        gb.pump(gba)

    for _ in range(2):
        gb.sb = 0x00
        gb.pump(gba)

    ok = gba.sub == WAI and gba.tx == START and gba.established
    print("room sync retry: sub=%u tx=%02X established=%u %s" %
          (gba.sub, gba.tx, gba.established, "OK" if ok else "FAIL"))
    return ok


def test_late_sync_retry():
    """A late 0xD1 must not leave the GBA stuck in the enter table."""
    gba = Gba()
    gb = Gb()

    gb.pending = 0x61
    gb.link_transfer()
    gb.pump(gba)
    gb.sb = 0xD1
    gb.pump(gba)
    for _ in range(2):
        gb.sb = 0x00
        gb.pump(gba)

    # Crystal retries a room byte, then moves on to action 5.
    gb.sb = 0xD1
    gb.pump(gba)
    gb.pending = 5
    gb.link_transfer()
    gb.pump(gba)

    ok = gba.sub == STA and gba.tx == START
    print("late sync retry: sub=%u tx=%02X %s" %
          (gba.sub, gba.tx, "OK" if ok else "FAIL"))
    return ok


def test_chunks():
    gba = Gba()
    gba.sub = PAR
    gba.in_data = []
    gba.block_done = False

    preambles = [7, 6, 3]
    sizes = gba.sizes
    payload = []
    for size in sizes:
        chunk = []
        for i in range(size):
            v = (0x10 + i) & 0xFF
            if v in (0xFD, 0xFE):
                v = 0x55
            chunk.append(v)
        payload.append(chunk)

    total = 0
    for chunk in range(3):
        wire = [SYN] * preambles[chunk] + payload[chunk]
        send_idx = 0
        ignore = True
        done = False
        while not done and total < 100000:
            if ignore:
                gb_out = wire[0]
            elif send_idx < len(wire):
                gb_out = wire[send_idx]
            else:
                gb_out = SEND_NO_INFO

            gba.process(gb_out)
            total += 1

            if ignore:
                if gba.tx == SYN:
                    ignore = False
            else:
                send_idx += 1

            if not ignore and send_idx >= len(wire) and gba.size_idx > chunk:
                done = True

    expected = payload[0] + payload[1] + payload[2]
    ok = gba.block_done and gba.in_data == expected
    print("chunks: done=%s got=%d expected=%d %s" %
          (gba.block_done, len(gba.in_data), len(expected),
           "OK" if ok else "FAIL"))
    if not ok:
        for i, (a, b) in enumerate(zip(gba.in_data, expected)):
            if a != b:
                print("  first difference at %d: %02X != %02X" % (i, a, b))
                break
    return ok


def main():
    ok = test_handshake()
    ok = test_room_sync_retry() and ok
    ok = test_late_sync_retry() and ok
    ok = test_chunks() and ok
    if not ok:
        print("PROTOCOL SIMULATION FAILED")
        return 1
    print("PROTOCOL SIMULATION PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
