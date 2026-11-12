#include "global.h"
#include "config/time_capsule.h"
#include "time_capsule/gb_link.h"

#if TIME_CAPSULE_DEBUG_LOG
#include "gba/isagbprint.h"
#endif

// GBA side of the Gen I/II link.
//
// The GBA is the clock master: normal 8-bit SIO with the internal clock.
// Both reference implementations do the same (Pokemon-Gen3-to-GenX's
// `init_sio_normal(SIO_MASTER, SIO_8)` and Poke_Transporter_GB's
// `LinkSPI::Mode::MASTER_256KBPS`), and it is what keeps the sides in step:
// the GB cannot shift until the GBA clocks it.
//
// A Gen I/II byte takes ~1 ms (8192 bps), which is 14 scanlines.  The caller
// therefore paces itself with GbLink_WaitLines() and performs many exchanges
// per frame instead of one, which is what makes the GB's short-lived outgoing
// byte visible at all.

// Normal mode: bit 0 selects the clock source (1 = internal = master).
#define GB_SIO_INTERNAL_CLOCK 0x0001

static IWRAM_DATA bool8 sActive;
static IWRAM_DATA bool8 sPending;
static IWRAM_DATA u8 sLastTx;

#if TIME_CAPSULE_DEBUG_LOG
static EWRAM_DATA u32 sDbgExchanges;
static EWRAM_DATA u8 sDbgLastRx;
#endif

void GbLink_Init(void)
{
    if (sActive)
        return;

    sActive = TRUE;
    sPending = FALSE;
    sLastTx = 0x01;

    REG_RCNT = 0;
    // Normal 8-bit mode, internal clock, no interrupt (the start bit is polled).
    REG_SIOCNT = SIO_8BIT_MODE | GB_SIO_INTERNAL_CLOCK;
    REG_SIODATA8 = 0x01;

#if TIME_CAPSULE_DEBUG_LOG
    sDbgExchanges = 0;
    sDbgLastRx = 0;
    DebugPrintfLevel(MGBA_LOG_DEBUG, "TC init: siocnt=%04X rcnt=%04X",
                     REG_SIOCNT, REG_RCNT);
#endif
}

void GbLink_Shutdown(void)
{
    if (!sActive)
        return;

    REG_SIOCNT = 0;
    REG_RCNT = 0;
    sActive = FALSE;
    sPending = FALSE;
}

bool8 GbLink_IsBusy(void)
{
    return sPending;
}

void GbLink_Start(u8 tx)
{
    if (!sActive || sPending)
        return;

    sLastTx = tx;
    REG_SIODATA8 = tx;
    REG_SIOCNT |= SIO_START;
    sPending = TRUE;
}

bool8 GbLink_Poll(u8 *rx)
{
    if (!sActive || !sPending)
        return FALSE;

    if (REG_SIOCNT & SIO_START)
        return FALSE;

    *rx = REG_SIODATA8;
    sPending = FALSE;
#if TIME_CAPSULE_DEBUG_LOG
    sDbgExchanges++;
    sDbgLastRx = *rx;
#endif
    return TRUE;
}

void GbLink_WaitLines(u16 lines)
{
    u16 start = REG_VCOUNT;

    // The delta keeps growing when VCOUNT wraps around, so this always ends
    // after at most one frame even if we start near the end of a frame.
    while ((u16)(REG_VCOUNT - start) < lines)
        ;
}

#if TIME_CAPSULE_DEBUG_LOG
void GbLink_LogDebug(void)
{
    DebugPrintfLevel(MGBA_LOG_DEBUG,
        "TC link: x=%lu tx=%02X rx=%02X siocnt=%04X",
        sDbgExchanges, sLastTx, sDbgLastRx, REG_SIOCNT);
}
#endif
