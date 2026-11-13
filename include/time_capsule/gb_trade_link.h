#ifndef GUARD_TIME_CAPSULE_GB_TRADE_LINK_H
#define GUARD_TIME_CAPSULE_GB_TRADE_LINK_H

#include "global.h"

void GbTrade_Update(void);
bool8 GbTrade_SendBlock(const void *src, u16 size);

// Post-trade: pause the stream and save first, then call this every frame
// until it returns TRUE.  It sends the success byte after the save, waits for
// the GB to start its next block exchange, then queues the re-exchange block.
void GbTrade_ResetSuccess(void);
void GbTrade_PrepareSave(void);
bool8 GbTrade_FinishStep(void);

void GbTrade_RequestExit(void);

#endif // GUARD_TIME_CAPSULE_GB_TRADE_LINK_H
