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

LINKCMD_READY_TO_TRADE = 0xAABB
LINKCMD_INIT_BLOCK = 0xBBBB
LINKCMD_START_TRADE = 0xCCDD
LINKCMD_SET_MONS_TO_TRADE = 0xDDDD
LINKCMD_REQUEST_CANCEL = 0xEEAA
LINKCMD_READY_CANCEL_TRADE = 0xBBCC
LINKCMD_PLAYER_CANCEL_TRADE = 0xDDEE
LINKCMD_PARTNER_CANCEL_TRADE = 0xEECC


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
        self.trade_state = 0  # NO_INFO / HAVE_OFFER / HAVE_ACCEPT ...
        self.trade_offer = 0
        self.confirm_out = 1
        self.confirm_valid = False
        self.success_value = 0
        self.last_peer = 0xFF
        self.received_once = False
        self.syn_tx = 0
        self.last_cmd = None
        self.last_arg = None
        self.success_done = False
        self.accept_pending = False
        self.peer_confirmed = False
        self.ack_frames = 0
        self.selection_hold = 0

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

    def _phase_tx(self):
        """The byte this phase owns; never shared between phases."""
        if self.trade_state == 1:  # selection
            return 0x70 + self.trade_offer
        if self.trade_state == 3:  # selection acknowledge
            return SEND_NO_INFO
        if self.trade_state in (2, 4):  # confirmation
            return 0x70 + self.confirm_out if self.confirm_valid else SEND_NO_INFO
        if self.trade_state == 5:  # success
            return 0x70 | self.success_value
        return SEND_NO_INFO

    def _menu_tx(self):
        if self.trade_state == 1 and self.syn_tx < 3:
            return SEND_NO_INFO
        return self._phase_tx()

    def wire(self):
        """The byte on the wire right now, even if no new input arrived."""
        if self.selection_hold and self.trade_state in (1, 3):
            return 0x70 + self.trade_offer
        return self._menu_tx()

    def send_trade_command(self, cmd, arg):
        if cmd == LINKCMD_READY_TO_TRADE:
            self.received_once = False
            self.last_peer = 0xFF
            self.syn_tx = 0
            self.trade_offer = arg & 0xF
            self.confirm_out = 1
            self.confirm_valid = False
            self.trade_state = 1
            self.accept_pending = False
            self.peer_confirmed = False
        elif cmd == LINKCMD_INIT_BLOCK:
            if self.trade_state in (1, 3, 4):
                # The selection exchange or its acknowledgement is still
                # running: hold the accept and keep the phase byte on the
                # wire.
                self.accept_pending = True
            else:
                self.received_once = False
                self.last_peer = 0xFF
                self.syn_tx = 0
                self.confirm_out = 2
                self.confirm_valid = True
                self.trade_state = 2
                if self.peer_confirmed:
                    self.last_cmd = LINKCMD_INIT_BLOCK
                    self.last_arg = 0
        elif cmd in (LINKCMD_READY_CANCEL_TRADE, LINKCMD_PLAYER_CANCEL_TRADE,
                     LINKCMD_PARTNER_CANCEL_TRADE):
            self.received_once = False
            self.last_peer = 0xFF
            self.syn_tx = 0
            self.accept_pending = False
            self.peer_confirmed = False
            self.confirm_out = 1
            self.confirm_valid = True
            self.trade_state = 2
        elif cmd == LINKCMD_START_TRADE:
            self.trade_state = 5
            self.accept_pending = False
            self.peer_confirmed = False
        # SET_MONS_TO_TRADE and other menu-only commands are ignored.
        self.tx = self._phase_tx()

    def _report(self, cmd, arg, data):
        self.last_cmd = cmd
        self.last_arg = arg
        self.last_peer = data
        self.trade_state = 0

    def _get_offer(self, data):
        if (0x70 <= data < 0x76) or data == 0x7F:
            if data != self.last_peer:
                if not self.received_once:
                    self.received_once = True
                else:
                    self.received_once = False
                    self._report(LINKCMD_READY_TO_TRADE, data - 0x70, data)
                    self.last_peer = 0xFF
                    self.received_once = False
                    self.trade_state = 3  # WAIT_ACK
                    self.ack_frames = 0
                    self.selection_hold = 1
                    self.confirm_valid = False

    def _get_accept(self, data):
        if data in (0x71, 0x72):
            if data != self.last_peer:
                if not self.received_once:
                    self.received_once = True
                else:
                    self.received_once = False
                    if (data == 0x72 and self.confirm_valid
                            and self.confirm_out == 2):
                        self._report(LINKCMD_INIT_BLOCK, 0, data)
                    else:
                        self.confirm_out = 1
                        self.confirm_valid = True
                        self._report(LINKCMD_READY_CANCEL_TRADE, 0, data)

    def trade_menu(self, data):
        # One emulator frame passed since the previous exchange.
        if self.selection_hold:
            self.selection_hold -= 1

        # ProcessByte() drops SERIAL_NO_DATA_BYTE before the trade-menu
        # handler, so a 0xFE must not advance any counter here either.
        if data == SEND_NO_INFO:
            return

        # The byte for this exchange belongs to the phase active before the
        # handler runs; the handler only changes the phase.
        self.tx = self._menu_tx()

        if self.trade_state == 3:
            ack_over = False
            # Only 0x00 is meaningful here: Crystal's selection acknowledge.
            # A repeated 0x7X is its selection offer, not a confirmation.
            if data == 0x00:
                ack_over = True
            else:
                self.ack_frames += 1
                if self.ack_frames > 24:
                    ack_over = True
            if ack_over:
                self.trade_state = 4
                self.last_peer = 0xFF
                self.received_once = False
                if self.accept_pending:
                    self.accept_pending = False
                    self.confirm_out = 2
                    self.confirm_valid = True
                    self.trade_state = 2
                    if self.peer_confirmed:
                        self.last_cmd = LINKCMD_INIT_BLOCK
                        self.last_arg = 0
            return

        if self.trade_state == 4:
            if data == 0x71:
                self._report(LINKCMD_READY_CANCEL_TRADE, 0, data)
                self.accept_pending = False
                self.confirm_out = 1
                self.confirm_valid = True
                self.trade_state = 2
                return
            if self.accept_pending:
                self.accept_pending = False
                self.confirm_out = 2
                self.confirm_valid = True
                self.trade_state = 2
                if self.peer_confirmed:
                    self.last_cmd = LINKCMD_INIT_BLOCK
                    self.last_arg = 0
                return
            if data == 0x72:
                self.peer_confirmed = True
            return

        if self.trade_state == 1 and self.syn_tx < 3:
            self.syn_tx += 1
            return

        if self.trade_state == 1:
            self._get_offer(data)
        elif self.trade_state == 2:
            self._get_accept(data)
        elif self.trade_state == 5:
            if (data & 0xF0) == 0x70:
                if not self.received_once:
                    self.received_once = True
                else:
                    self.success_done = True

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
        elif self.sub == DON:
            self.trade_menu(data)
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


class GbTrade:
    """Crystal's LinkTrade selection and confirmation exchanges."""

    def __init__(self, slot=0):
        self.slot = slot
        self.phase = "select_wait"
        self.sb = 0x70 + slot
        self.peer_slot = 0xFF
        self.receive_left = 0
        self.ack_left = 0
        self.confirm_requested = False
        self.peer_confirm = 0xFF
        self.confirm_receive_left = 0
        self.confirm_ack_left = 0
        self.confirm_done = False
        self.accept = False

    def request_confirm(self):
        self.confirm_requested = True

    def step(self, gba_tx):
        # Read the GBA byte.  .Receive only updates the action when the high
        # nibble matches 0x70; 0xFE is ignored.
        if (gba_tx & 0xF0) == 0x70:
            if self.phase in ("select_wait", "select_receive"):
                self.peer_slot = gba_tx & 0x0F
                if self.phase == "select_wait":
                    self.phase = "select_receive"
                    self.receive_left = 10
            elif self.phase == "confirm_receive" or self.phase == "confirm_wait":
                self.peer_confirm = gba_tx & 0x0F
                if self.phase == "confirm_wait":
                    self.phase = "confirm_receive"
                    self.confirm_receive_left = 10

        # Advance the current phase's outgoing byte and frame counters.
        if self.phase == "select_wait":
            self.sb = 0x70 + self.slot
        elif self.phase == "select_receive":
            self.sb = 0x70 + self.slot
            self.receive_left -= 1
            if self.receive_left == 0:
                self.phase = "select_ack"
                self.ack_left = 10
        elif self.phase == "select_ack":
            self.sb = 0x00
            self.ack_left -= 1
            if self.ack_left == 0:
                self.phase = "menu"
        elif self.phase == "menu":
            self.sb = 0x00
            if self.confirm_requested:
                self.phase = "confirm_wait"
                self.peer_confirm = 0xFF
                self.sb = 0x72
        elif self.phase == "confirm_wait":
            self.sb = 0x72
        elif self.phase == "confirm_receive":
            self.sb = 0x72
            self.confirm_receive_left -= 1
            if self.confirm_receive_left == 0:
                self.phase = "confirm_ack"
                self.confirm_ack_left = 10
        elif self.phase == "confirm_ack":
            self.sb = 0x00
            self.confirm_ack_left -= 1
            if self.confirm_ack_left == 0:
                self.phase = "done"
                self.confirm_done = True
                self.accept = self.peer_confirm != 1


def run_trade_order(gba_slot, gba_select_frame, gba_confirm_frame,
                    gb_slot, gb_confirm_frame, frames=100):
    gba = Gba()
    gba.sub = DON
    gb = GbTrade(gb_slot)
    gba_reported = False
    gba_started = False

    for f in range(frames):
        if f == gba_select_frame:
            gba.send_trade_command(LINKCMD_READY_TO_TRADE, gba_slot)
        if f == gba_confirm_frame:
            gba.send_trade_command(LINKCMD_INIT_BLOCK, 0)
        if f == gb_confirm_frame:
            gb.request_confirm()

        gba_tx = gba.tx
        gba.process(gb.sb)
        gb.step(gba_tx)

        if gba.last_cmd == LINKCMD_READY_TO_TRADE and gba.last_arg == gb_slot:
            gba_reported = True
        if gba.last_cmd == LINKCMD_INIT_BLOCK:
            gba_started = True

    ok = (gb.confirm_done and gb.accept
          and gb.peer_slot == gba_slot
          and gba_reported and gba_started)
    print("order: gba@%d/%d gb@%d slot=%d -> gb_slot=%d accept=%s "
          "reported=%s started=%s %s" %
          (gba_select_frame, gba_confirm_frame, gb_confirm_frame, gba_slot,
           gb.peer_slot, gb.accept, gba_reported, gba_started,
           "OK" if ok else "FAIL"))
    return ok


def test_trade_orderings():
    """Every ordering of who selects/confirms first must agree."""
    ok = True
    # slot 1 is the dangerous one: 0x71 is also Crystal's cancel value.
    ok = run_trade_order(1, 2, 4, 0, 50) and ok    # Emerald first
    ok = run_trade_order(1, 2, 50, 0, 4) and ok    # Crystal confirms first
    ok = run_trade_order(1, 20, 22, 0, 50) and ok  # Crystal selects first
    ok = run_trade_order(1, 20, 50, 0, 4) and ok   # Crystal selects+confirms first
    ok = run_trade_order(1, 2, 4, 1, 50) and ok    # both slot 1
    ok = run_trade_order(1, 20, 50, 1, 4) and ok   # both slot 1, Crystal first
    ok = run_trade_order(1, 2, 4, 2, 50) and ok    # Crystal slot 2
    ok = run_trade_order(1, 2, 50, 2, 4) and ok    # Crystal slot 2 confirms first
    ok = run_trade_order(2, 20, 50, 2, 4) and ok   # both slot 2, Crystal first
    return ok


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


def test_trade_menu():
    """Leader offer and accept translation (the GB waits for the leader)."""
    gba = Gba()
    gba.sub = DON

    gba.send_trade_command(LINKCMD_READY_TO_TRADE, 2)
    ok = gba.tx == 0x72 and gba.trade_state == 1

    # The GB offers its Pokémon 1.  Three bytes are used by the sync delay,
    # then the two debounce sightings report the offer.
    for _ in range(8):
        gba.process(0x71)
    offer_ok = gba.last_cmd == LINKCMD_READY_TO_TRADE and gba.last_arg == 1

    gba.send_trade_command(LINKCMD_INIT_BLOCK, 0)
    pending_ok = gba.tx == SEND_NO_INFO and gba.trade_state == 3
    gba.process(0x00)  # Crystal's selection acknowledge
    ack_ok = gba.trade_state == 2
    gba.process(0x72)  # first exchange of the new phase presents 0x72
    ack_ok = ack_ok and gba.tx == 0x72

    for _ in range(8):
        gba.process(0x72)
    accept_ok = gba.last_cmd == LINKCMD_INIT_BLOCK
    ok = ok and offer_ok and pending_ok and ack_ok and accept_ok

    print("trade menu: offer=%s pending=%s ack=%s accept=%s %s" %
          (offer_ok, pending_ok, ack_ok, accept_ok, "OK" if ok else "FAIL"))
    return ok


def test_confirm_first():
    """Crystal confirms first; the GBA must remember it and start."""
    gba = Gba()
    gba.sub = DON

    # Emerald selects party slot 1: our offer is 0x71, the same value
    # Crystal uses for "cancel" on its confirmation screen.
    gba.send_trade_command(LINKCMD_READY_TO_TRADE, 1)
    for _ in range(8):
        gba.process(0x70)
        if gba.last_cmd == LINKCMD_READY_TO_TRADE:
            break
    offer_ok = gba.last_cmd == LINKCMD_READY_TO_TRADE and gba.trade_state == 3

    # Crystal first acknowledges the selection, then confirms before Emerald.
    # The 0x72 is only meaningful in WAIT_CONFIRM; in WAIT_ACK it is still
    # Crystal's repeated selection byte (slot 2).
    gba.process(0x00)
    gba.process(0x72)
    safe = (gba.peer_confirmed and gba.trade_state == 4
            and gba.tx == SEND_NO_INFO)

    # When Emerald confirms, the menu must be told immediately even if
    # Crystal has stopped repeating 0x72.  WAIT_CONFIRM presents the accept on
    # the next received byte.
    gba.send_trade_command(LINKCMD_INIT_BLOCK, 0)
    gba.process(0x72)
    started = (gba.last_cmd == LINKCMD_INIT_BLOCK
               and gba.trade_state == 2 and gba._phase_tx() == 0x72)

    ok = offer_ok and safe and started
    print("confirm first: offer=%s safe=%s started=%s %s" %
          (offer_ok, safe, started, "OK" if ok else "FAIL"))
    return ok


def test_success_handshake():
    """The repeated success byte completes the handshake (not 0x75)."""
    gba = Gba()
    gba.sub = DON
    gba.send_trade_command(LINKCMD_START_TRADE, 0)

    # StartSuccess resets the menu sync counter: the first three bytes are
    # consumed by the sync delay, then two 0x70 sightings complete.
    for _ in range(3):
        gba.process(0x00)
    gba.process(0x70)
    first_ok = not gba.success_done
    gba.process(0x70)
    ok = first_ok and gba.success_done and gba.tx == 0x70
    print("success: first=%s done=%s %s" %
          (first_ok, gba.success_done, "OK" if ok else "FAIL"))
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


def test_no_early_accept():
    """The wire must be 0xFE while the GBA waits for its player.

    This is the regression from the real log: Crystal reached its
    confirmation screen while the GBA was still in WAIT_ACK.  Any 0x7X on
    the wire there (including the selection offer) makes Crystal accept on
    its own.
    """
    gba = Gba()
    gba.sub = DON

    # Emerald offers slot 0; Crystal offers slot 2 (0x72).
    gba.send_trade_command(LINKCMD_READY_TO_TRADE, 0)
    for _ in range(8):
        gba.process(0x72)
        if gba.last_cmd == LINKCMD_READY_TO_TRADE:
            break
    offer_ok = (gba.last_cmd == LINKCMD_READY_TO_TRADE
                and gba.last_arg == 2 and gba.trade_state == 3)

    # The phase changed on the detection exchange.  The offer is held for one
    # more full frame so Crystal can sample it; after that the wire must be
    # 0xFE even if Crystal goes silent.
    hold_ok = gba.wire() == 0x70
    gba.process(0x72)
    wire_ok = (gba.wire() == SEND_NO_INFO and gba.tx == SEND_NO_INFO)

    # The trade menu sends SET_MONS_TO_TRADE once both selections are shown.
    # It is a menu-only command and must not reset the Gen I/II phase.
    gba.send_trade_command(LINKCMD_SET_MONS_TO_TRADE, 0)
    menu_ok = gba.trade_state == 3 and gba.tx == SEND_NO_INFO

    # Crystal's confirmation exchange starts while the GBA is still waiting
    # for the selection acknowledge.  0xFE must not make it accept.
    gb = GbTrade(2)
    for _ in range(25):
        gb.step(0x70)  # finish Crystal's selection exchange first
    gb.request_confirm()
    for _ in range(10):
        gb.step(gba.tx)
    no_early_accept = gb.phase == "confirm_wait" and not gb.confirm_done

    # Selection acknowledge, then Emerald confirms.
    gba.process(0x00)
    gba.send_trade_command(LINKCMD_INIT_BLOCK, 0)
    for _ in range(4):
        gba.process(0x72)
    accept_ok = (gba.last_cmd == LINKCMD_INIT_BLOCK
                 and gba.confirm_valid and gba.confirm_out == 2)

    # The 0x72 now releases Crystal's confirmation exchange.
    for _ in range(40):
        gb.step(0x72)
    crystal_ok = gb.confirm_done and gb.accept

    ok = (offer_ok and hold_ok and wire_ok and menu_ok and no_early_accept
          and accept_ok and crystal_ok)
    print("no early accept: offer=%s hold=%s wire=%s menu=%s safe=%s accept=%s crystal=%s %s" %
          (offer_ok, hold_ok, wire_ok, menu_ok, no_early_accept, accept_ok,
           crystal_ok, "OK" if ok else "FAIL"))
    return ok


def test_cancel_wins():
    """A Crystal cancel must not be overridden by a simultaneous GBA yes."""
    gba = Gba()
    gba.sub = DON
    gba.send_trade_command(LINKCMD_READY_TO_TRADE, 0)
    for _ in range(8):
        gba.process(0x72)
        if gba.last_cmd == LINKCMD_READY_TO_TRADE:
            break
    gba.process(0x00)  # selection acknowledge -> WAIT_CONFIRM
    gba.send_trade_command(LINKCMD_INIT_BLOCK, 0)  # local player pressed YES
    gba.process(0x71)  # Crystal pressed NO in the same window
    ok = (gba.last_cmd == LINKCMD_READY_CANCEL_TRADE
          and gba.trade_state == 2
          and gba.confirm_valid and gba.confirm_out == 1
          and gba._phase_tx() == 0x71)
    print("cancel wins: %s %s" % ("OK" if ok else "FAIL",
                                 "" if ok else "state=%u tx=%02X" %
                                 (gba.trade_state, gba._phase_tx())))
    return ok


def main():
    ok = test_handshake()
    ok = test_room_sync_retry() and ok
    ok = test_late_sync_retry() and ok
    ok = test_trade_menu() and ok
    ok = test_confirm_first() and ok
    ok = test_no_early_accept() and ok
    ok = test_cancel_wins() and ok
    ok = test_trade_orderings() and ok
    ok = test_success_handshake() and ok
    ok = test_chunks() and ok
    if not ok:
        print("PROTOCOL SIMULATION FAILED")
        return 1
    print("PROTOCOL SIMULATION PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
