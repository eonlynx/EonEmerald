#ifndef GUARD_TIME_CAPSULE_GB_TRADE_LINK_H
#define GUARD_TIME_CAPSULE_GB_TRADE_LINK_H

#include "global.h"

void GbTrade_Update(void);
bool8 GbTrade_SendBlock(const void *src, u16 size);

// Post-trade sequence, matching Crystal's order:
//   animation -> success handshake -> save -> next LinkTrade
// The success handshake must start as soon as the animation ends, not after
// the GBA save, or Crystal is already in its next CheckLinkTimeout when our
// 0x70 arrives and reports "too much time has elapsed".
void GbTrade_StartSuccess(void);
bool8 GbTrade_IsSuccessDone(void);
void GbTrade_StartReexchange(void);
void GbTrade_RebuildOutBuffer(void);

void GbTrade_RequestExit(void);

#endif // GUARD_TIME_CAPSULE_GB_TRADE_LINK_H
