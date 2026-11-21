#ifndef GUARD_TIME_CAPSULE_GB_TRADE_LINK_H
#define GUARD_TIME_CAPSULE_GB_TRADE_LINK_H

#include "global.h"

void GbTrade_Update(void);
bool8 GbTrade_SendBlock(const void *src, u16 size);

// Post-trade sequence:
//   animation -> success handshake -> next CheckLinkTimeout + block stream ->
//   evolution -> save
// The success phase is entered at LINKCMD_START_TRADE, before the animation,
// because Crystal's animation can be shorter: if we keep presenting 0x72 until
// our animation ends, Crystal reads that stale accept as its success byte and
// saves before we ever see its 0x70.
void GbTrade_StartSuccess(void);
bool8 GbTrade_IsSuccessDone(void);
void GbTrade_StartReexchange(void);
void GbTrade_RebuildOutBuffer(void);

void GbTrade_RequestExit(void);

#endif // GUARD_TIME_CAPSULE_GB_TRADE_LINK_H
