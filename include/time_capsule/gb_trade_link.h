#ifndef GUARD_TIME_CAPSULE_GB_TRADE_LINK_H
#define GUARD_TIME_CAPSULE_GB_TRADE_LINK_H

#include "global.h"

void GbTrade_Update(void);
bool8 GbTrade_SendBlock(const void *src, u16 size);
bool8 GbTrade_IsPartyReady(void);
u8 GbTrade_GetGeneration(void);

// Post-trade: save first, then call this every frame until it returns TRUE.
// It sends the success byte, waits for the GB echo, then queues the
// re-exchange block.
void GbTrade_ResetSuccess(void);
bool8 GbTrade_FinishStep(void);

void GbTrade_RequestExit(void);
bool8 GbTrade_IsFinished(void);

#endif // GUARD_TIME_CAPSULE_GB_TRADE_LINK_H
