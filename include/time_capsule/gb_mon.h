#ifndef GUARD_TIME_CAPSULE_GB_MON_H
#define GUARD_TIME_CAPSULE_GB_MON_H

#include "global.h"
#include "pokemon.h"

// Gen I/II <-> Gen III Pokémon conversion, matching gen_converter.c /
// gen12_methods.c / party_handler.c of Pokemon-Gen3-to-Gen-X.  The wire block
// itself is built and parsed by gb_convert.c (the sio_buffers.c analogue).

u8 GbMon_TextToGen12(const u8 *src, u8 *dst, u8 maxLen);
u8 GbMon_TextToGen3(const u8 *src, u8 *dst, u8 maxLen);

bool8 GbMon_BuildGen2(struct Pokemon *mon, u8 *dst);
bool8 GbMon_BuildGen1(struct Pokemon *mon, u8 *dst);

bool8 GbMon_CreateFromGen2(const u8 *src, const u8 *otNameSrc, const u8 *nicknameSrc, bool8 isEgg, struct Pokemon *mon);
bool8 GbMon_CreateFromGen1(const u8 *src, const u8 *otNameSrc, const u8 *nicknameSrc, struct Pokemon *mon);

#endif // GUARD_TIME_CAPSULE_GB_MON_H
