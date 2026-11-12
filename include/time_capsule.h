#ifndef GUARD_TIME_CAPSULE_H
#define GUARD_TIME_CAPSULE_H

#include "global.h"
#include "config/time_capsule.h"
#include "constants/time_capsule.h"

#define TIME_CAPSULE_GEN_NONE     0
#define TIME_CAPSULE_GEN_1        1
#define TIME_CAPSULE_GEN_2        2

// The Time Capsule session is active. trade.c/link.c use this to route the
// link calls through the GB backend instead of the GBA link.
extern bool8 gTimeCapsuleActive;
extern u8 gTimeCapsuleGeneration;
extern u8 gTimeCapsuleRoom;
extern u8 gTimeCapsuleMode;

void TimeCapsule_Confirm(void);
void TimeCapsule_TryConnect(void);
void TimeCapsule_StartExchange(void);
void TimeCapsule_CloseLink(void);
void TimeCapsule_CheckParty(void);
void TimeCapsule_GetGeneration(void);
void TimeCapsule_StartBattle(void);
void TimeCapsule_DoWarp(void);

#endif // GUARD_TIME_CAPSULE_H
