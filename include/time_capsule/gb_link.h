#ifndef GUARD_TIME_CAPSULE_GB_LINK_H
#define GUARD_TIME_CAPSULE_GB_LINK_H

#include "global.h"
#include "config/time_capsule.h"

// The GBA drives the link clock (normal 8-bit SIO, internal clock), exactly
// like Pokemon-Gen3-to-GenX's VCOUNT-driven master routine and
// Poke_Transporter_GB's LinkSPI in MASTER mode.
//
// The Gen I/II cable runs at 8192 bps, i.e. about one byte per millisecond
// (14 scanlines).  That is why a transfer is started and collected inside a
// scanline window instead of once per frame: the GB only keeps its outgoing
// byte in SB for a fraction of a frame, so clocking once per frame would
// almost always read the "no data" byte (0xFE) and the handshake would never
// make progress.
//
// Transfers are still asynchronous: a transfer that the GB has not answered
// stays pending, so nothing is ever lost when the GB is a little late.
void GbLink_Init(void);
void GbLink_Shutdown(void);

// TRUE while a started transfer is still waiting for the GB to clock back.
bool8 GbLink_IsBusy(void);

// Starts a transfer.  Does nothing if one is already pending.
void GbLink_Start(u8 tx);

// Waits up to `lines` scanlines for the GB to arm a byte.  Returns TRUE when
// SI is low (the GB is ready to be clocked), like the reference's SIO_RDY
// wait in `sio_normal_inner_master`.
bool8 GbLink_WaitPeerReady(u16 lines);

// Finishes a pending transfer.  Returns TRUE and stores the received byte
// when the GB clocked it back.
bool8 GbLink_Poll(u8 *rx);

// Spins until `lines` scanlines have elapsed.  Used to pace the master at the
// real 8192 bps byte time (14 scanlines), like `timed_wait_master`.
void GbLink_WaitLines(u16 lines);

#if TIME_CAPSULE_DEBUG_LOG
void GbLink_LogDebug(void);
#endif

#endif // GUARD_TIME_CAPSULE_GB_LINK_H
