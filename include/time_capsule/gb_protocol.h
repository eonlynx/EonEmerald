#ifndef GUARD_TIME_CAPSULE_GB_PROTOCOL_H
#define GUARD_TIME_CAPSULE_GB_PROTOCOL_H

#include "global.h"
#include "time_capsule.h"

#define GB_PROTO_RESULT_ONGOING  0xFF

void GbProtocol_Init(u8 room);
void GbProtocol_Update(void);
void GbProtocol_Shutdown(void);

bool8 GbProtocol_IsActive(void);
bool8 GbProtocol_IsReady(void);       // handshake + party exchange complete
bool8 GbProtocol_IsLinkEstablished(void); // generation + room selected
bool8 GbProtocol_IsError(void);
bool8 GbProtocol_IsFinished(void);    // clean exit complete
bool8 GbProtocol_IsExiting(void);    // the exit byte is being exchanged
bool8 GbProtocol_PeerLeft(void);     // the other game left the trade room
u8 GbProtocol_GetResult(void);
u8 GbProtocol_GetGeneration(void);

// Trade-menu interface (the GB side presents LINKCMD_* commands to trade.c).
bool8 GbProtocol_HasTradeCommand(void);
u16 GbProtocol_GetTradeCommand(void);
u16 GbProtocol_GetTradeArg(void);
void GbProtocol_ClearTradeCommand(void);
void GbProtocol_SendTradeCommand(u16 cmd, u16 arg);

// Post-trade handshake / loop.
void GbProtocol_StartSuccess(void);
bool8 GbProtocol_IsSuccessDone(void);
void GbProtocol_StartReexchange(void);
void GbProtocol_RequestExit(void);

// Stop presenting a trade action while the GBA writes its save: the GB then
// keeps waiting in its anti-cloning loop instead of mistaking the accept byte
// (0x72) for its own success byte (2).
void GbProtocol_PauseForSave(void);

#endif // GUARD_TIME_CAPSULE_GB_PROTOCOL_H
