#include "global.h"
#include "time_capsule.h"
#include "time_capsule/gb_protocol.h"
#include "time_capsule/gb_trade_link.h"
#include "link.h"

// Adapter between the GBA trade menu (trade.c) and the GB link protocol.
// trade.c still uses LINKCMD_* commands and gBlockRecvBuffer; this file
// translates them to/from the Gen I/II trade-menu bytes.

static EWRAM_DATA bool8 sSuccessStarted;

void GbTrade_Update(void)
{
    if (!gTimeCapsuleActive)
        return;

    if (GbProtocol_HasTradeCommand())
    {
        u16 cmd = GbProtocol_GetTradeCommand();
        u16 arg = GbProtocol_GetTradeArg();

        // The trade menu reads the partner's command from slot 1.
        gBlockRecvBuffer[1][0] = cmd;
        gBlockRecvBuffer[1][1] = arg;
        gBlockReceivedStatus[1] = TRUE;

        GbProtocol_ClearTradeCommand();
    }
}

bool8 GbTrade_SendBlock(const void *src, u16 size)
{
    const u16 *data = src;

    (void)size;
    if (!gTimeCapsuleActive)
        return FALSE;

    GbProtocol_SendTradeCommand(data[0], data[1]);
    return TRUE;
}

bool8 GbTrade_IsPartyReady(void)
{
    return GbProtocol_IsReady();
}

u8 GbTrade_GetGeneration(void)
{
    return GbProtocol_GetGeneration();
}

void GbTrade_ResetSuccess(void)
{
    sSuccessStarted = FALSE;
}

bool8 GbTrade_FinishStep(void)
{
    if (!sSuccessStarted)
    {
        GbProtocol_StartSuccess();
        sSuccessStarted = TRUE;
        return FALSE;
    }

    if (!GbProtocol_IsSuccessDone())
        return FALSE;

    GbProtocol_StartReexchange();
    sSuccessStarted = FALSE;
    return TRUE;
}

void GbTrade_RequestExit(void)
{
    if (gTimeCapsuleActive)
        GbProtocol_RequestExit();
}

bool8 GbTrade_IsFinished(void)
{
    return GbProtocol_IsFinished();
}
