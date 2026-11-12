#ifndef GUARD_TIME_CAPSULE_GB_CONVERT_H
#define GUARD_TIME_CAPSULE_GB_CONVERT_H

#include "global.h"
#include "pokemon.h"

#define GB_PARTY_SIZE            6
#define GB_MON_SIZE_GEN1         44
#define GB_MON_SIZE_GEN2         48
#define GB_PARTY_DATA_GEN1       418
#define GB_PARTY_DATA_GEN2       444
#define GB_RN_PREAMBLE_SIZE      7
#define GB_RN_RANDOM_SIZE        10
#define GB_RN_BLOCK_SIZE         (GB_RN_PREAMBLE_SIZE + GB_RN_RANDOM_SIZE)
#define GB_PARTY_PREAMBLE_SIZE   6
#define GB_PATCH_PREAMBLE_SIZE   3
#define GB_PATCH_DATA_SIZE       197
#define GB_PATCH_LIST_SIZE       (GB_PATCH_PREAMBLE_SIZE + GB_PATCH_DATA_SIZE)
#define GB_MAIL_BLOCK_SIZE       390
#define GB_BLOCK_TRADE_GEN1      641
#define GB_BLOCK_TRADE_GEN2      1057
#define GB_BLOCK_COLOSSEUM_GEN1  641
#define GB_BLOCK_COLOSSEUM_GEN2  667

#define GB_LAST_GEN1_SPECIES     151
#define GB_LAST_GEN2_SPECIES     251
#define GB_LAST_GEN1_MOVE        165
#define GB_LAST_GEN2_MOVE        251
#define GB_STRUGGLE              165
#define GB_GEN2_EGG_INDEX        0xFD
#define GB_NO_MON                0xFF

// Builds the full outgoing block (RN + party + patch list + optional mail).
// `out` must hold at least GB_BLOCK_TRADE_GEN2 bytes. Returns the block size.
u16 GbConvert_BuildTradeBlock(u8 gen, u8 room, u8 *out);

// Parses the received full block, applies the patch list, converts the
// partner party into gParties[B_TRAINER_OPPONENT_A] and fills gLinkPlayers[1].
// Returns TRUE on success.
bool8 GbConvert_ParseTradeBlock(u8 gen, u8 room, const u8 *in, u16 size);

// Compatibility check for the player's party. Returns TIME_CAPSULE_OK or one
// of the TIME_CAPSULE_ERR_* codes. `arg1`/`arg2` receive the offending
// species/move for the message.
u8 GbConvert_CheckParty(u8 gen, u16 *arg1, u16 *arg2);

// Exposed for the trade-menu shim.
u16 GbConvert_GetPartnerTrainerId(void);
void GbConvert_GetPartnerName(u8 *dst);

#endif // GUARD_TIME_CAPSULE_GB_CONVERT_H
