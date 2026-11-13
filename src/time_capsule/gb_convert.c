#include "global.h"
#include "time_capsule.h"
#include "time_capsule/gb_convert.h"
#include "time_capsule/gb_mon.h"
#include "pokemon.h"
#include "link.h"
#include "main.h"
#include "random.h"
#include "../data/time_capsule/tables.h"

#if TIME_CAPSULE_DEBUG_LOG
#include "gba/isagbprint.h"
#endif

#define GEN12_EOS 0x50

static u16 ReadBe16(const u8 *p)
{
    return (p[0] << 8) | p[1];
}

static void WriteBe16(u8 *p, u16 value)
{
    p[0] = value >> 8;
    p[1] = value;
}

// Gen I/II wire block: trainer/party serialisation, patch list and parser.
// This is the sio_buffers.c analogue of Pokemon-Gen3-to-Gen-X.  The per-mon
// conversion and validation live in gb_mon.c.

// ---------------------------------------------------------------------------
// Patch list
// ---------------------------------------------------------------------------

// The patch list only covers the bytes after the name, party count, species
// list and its terminator (19 bytes).  Offsets are 1-based inside each part,
// and the list is split in two parts of SERIAL_PATCH_DATA_SIZE (0xFC) bytes.
#define GB_PATCH_BASE      19
#define GB_PATCH_PART_SIZE 0xFC

static void BuildPatchList(u8 *data, u16 size, u8 *patch)
{
    u16 i;
    u16 pos = 0;
    u8 part = 0;

    memset(patch, 0, GB_PATCH_LIST_SIZE);
    patch[pos++] = 0xFD;
    patch[pos++] = 0xFD;
    patch[pos++] = 0xFD;

    for (i = GB_PATCH_BASE; i < size && pos < GB_PATCH_LIST_SIZE - 1; i++)
    {
        if (part == 0 && (i - GB_PATCH_BASE) == GB_PATCH_PART_SIZE)
        {
            patch[pos++] = 0xFF;
            part = 1;
        }
        if (data[i] == 0xFE)
        {
            data[i] = 0xFF;
            patch[pos++] = (u8)((i - GB_PATCH_BASE) - (part ? GB_PATCH_PART_SIZE : 0) + 1);
        }
    }
    // The patch list is made of two parts (the patchable area is split in
    // SERIAL_PATCH_DATA_SIZE bytes) and each one has to end with
    // SERIAL_PATCH_LIST_PART_TERMINATOR (0xFF).  The GB keeps reading past the
    // buffer when it does not find the second terminator, and then "patches"
    // whatever it finds, which corrupts the party data it has just received
    // (the nicknames are the last thing it reads).
    while (part < 2 && pos < GB_PATCH_LIST_SIZE)
    {
        patch[pos++] = 0xFF;
        part++;
    }
    while (pos < GB_PATCH_LIST_SIZE)
        patch[pos++] = 0;
}

static void ApplyPatchList(u8 *data, u16 size, const u8 *patch)
{
    u16 i;
    u16 offset = GB_PATCH_BASE;
    u8 part = 0;

    for (i = 0; i < GB_PATCH_LIST_SIZE; i++)
    {
        u8 value = patch[i];

        if (value == 0 || value == 0xFD || value == 0xFE)
            continue;
        if (value == 0xFF)
        {
            part++;
            if (part > 1)
                break;
            offset = GB_PATCH_BASE + GB_PATCH_PART_SIZE;
            continue;
        }
        {
            u16 target = offset + value - 1;

            if (target < size)
                data[target] = 0xFE;
        }
    }
}

// ---------------------------------------------------------------------------
// Party block building
// ---------------------------------------------------------------------------

static u16 BuildPartyData(u8 gen, u8 *party, struct Pokemon *mons, u8 count)
{
    u16 partySize = (gen == 1) ? GB_PARTY_DATA_GEN1 : GB_PARTY_DATA_GEN2;
    u16 monSize = (gen == 1) ? GB_MON_SIZE_GEN1 : GB_MON_SIZE_GEN2;
    u16 monOffset = (gen == 1) ? 19 : 21;
    u16 otOffset = (gen == 1) ? 283 : 309;
    u16 nickOffset = (gen == 1) ? 349 : 375;
    u8 i;
    u16 playerId = READ_OTID_FROM_SAVE & 0xFFFF;
    u8 name[16];

    memset(party, 0, partySize);

    GbMon_TextToGen12(gSaveBlock2Ptr->playerName, party, 10);
    if (count > GB_PARTY_SIZE)
        count = GB_PARTY_SIZE;
    party[11] = count;

    for (i = 0; i < GB_PARTY_SIZE; i++)
    {
        if (i >= count)
        {
            party[12 + i] = GB_NO_MON;
            continue;
        }

        {
            struct Pokemon *mon = &mons[i];
            // The Gen I/II games use the National Dex number, the expansion's
            // MON_DATA_SPECIES is the internal SPECIES_* constant.
            u16 species = SpeciesToNationalPokedexNum(GetMonData(mon, MON_DATA_SPECIES));
            bool8 isEgg = GetMonData(mon, MON_DATA_IS_EGG);

            if (gen == 1)
                party[12 + i] = gNatToGen1Species[species];
            else
                party[12 + i] = isEgg ? GB_GEN2_EGG_INDEX : species;
        }
    }
    party[18] = 0xFF;

    if (gen == 2)
        WriteBe16(party + 19, playerId);

    for (i = 0; i < GB_PARTY_SIZE; i++)
    {
        u8 *monData = party + monOffset + i * monSize;

        if (i < count)
        {
            struct Pokemon *mon = &mons[i];
            bool8 ok;

            if (gen == 1)
                ok = GbMon_BuildGen1(mon, monData);
            else
                ok = GbMon_BuildGen2(mon, monData);
            if (!ok)
                memset(monData, 0, monSize);
        }

        if (i < count)
        {
            GetMonData(&mons[i], MON_DATA_OT_NAME, name);
            GbMon_TextToGen12(name, party + otOffset + i * 11, 10);
            GetMonData(&mons[i], MON_DATA_NICKNAME, name);
            GbMon_TextToGen12(name, party + nickOffset + i * 11, 10);
        }
        else
        {
            memset(party + otOffset + i * 11, GEN12_EOS, 11);
            memset(party + nickOffset + i * 11, GEN12_EOS, 11);
        }
    }

    // 3 safety bytes are already zero.
    return partySize;
}

static u16 BuildTradeBlock(u8 gen, u8 room, struct Pokemon *mons, u8 count, u8 *out)
{
    u16 partySize;
    u16 partyDataOffset = GB_RN_BLOCK_SIZE + GB_PARTY_PREAMBLE_SIZE;
    u16 patchOffset;
    u16 total;
    u16 i;
    u8 *party;
    u8 *patch;

    memset(out, 0, GB_BLOCK_TRADE_GEN2);

    for (i = 0; i < GB_RN_PREAMBLE_SIZE; i++)
        out[i] = 0xFD;
    for (i = 0; i < GB_RN_RANDOM_SIZE; i++)
    {
        u8 value;
        do
        {
            value = Random();
        } while (value >= 0xFD);
        out[GB_RN_PREAMBLE_SIZE + i] = value;
    }

    for (i = 0; i < GB_PARTY_PREAMBLE_SIZE; i++)
        out[GB_RN_BLOCK_SIZE + i] = 0xFD;

    party = out + partyDataOffset;
    partySize = BuildPartyData(gen, party, mons, count);
    patch = party + partySize;
    BuildPatchList(party, partySize, patch);
    patchOffset = partyDataOffset + partySize;
    total = patchOffset + GB_PATCH_LIST_SIZE;

    if (gen == 2 && room == TIME_CAPSULE_ROOM_TRADE)
    {
        u8 *mail = out + total;

        memset(mail, 0, GB_MAIL_BLOCK_SIZE);
        for (i = 0; i < 5; i++)
            mail[i] = 0x20;
        // Empty mail patch set: a single 0xFF terminator.
        mail[5 + 198 + 84] = 0xFF;
        total += GB_MAIL_BLOCK_SIZE;
    }

    return total;
}

u16 GbConvert_BuildTradeBlock(u8 gen, u8 room, u8 *out)
{
    CalculatePlayerPartyCount();
    return BuildTradeBlock(gen, room, gParties[B_TRAINER_PLAYER],
                           gPartiesCount[B_TRAINER_PLAYER], out);
}

// ---------------------------------------------------------------------------
// Party block parsing
// ---------------------------------------------------------------------------

static u16 sPartnerTrainerId;

bool8 GbConvert_ParseTradeBlock(u8 gen, u8 room, const u8 *in, u16 size)
{
    u16 partySize = (gen == 1) ? GB_PARTY_DATA_GEN1 : GB_PARTY_DATA_GEN2;
    u16 monSize = (gen == 1) ? GB_MON_SIZE_GEN1 : GB_MON_SIZE_GEN2;
    u16 monOffset = (gen == 1) ? 19 : 21;
    u16 otOffset = (gen == 1) ? 283 : 309;
    u16 nickOffset = (gen == 1) ? 349 : 375;
    u16 partyOffset = GB_RN_BLOCK_SIZE + GB_PARTY_PREAMBLE_SIZE;
    u16 patchOffset = partyOffset + partySize;
    u8 partyCopy[GB_PARTY_DATA_GEN2];
    u8 count, i;

    (void)room;
    if (size < patchOffset + GB_PATCH_LIST_SIZE)
        return FALSE;

    memcpy(partyCopy, in + partyOffset, partySize);
    ApplyPatchList(partyCopy, partySize, in + patchOffset);

    count = partyCopy[11];
    if (count > GB_PARTY_SIZE)
        count = GB_PARTY_SIZE;

    // The species list is authoritative (the GB games mostly ignore the party
    // count), so stop at its terminator, like read_gen12_trade_data does.
    for (i = 0; i < count; i++)
    {
        if (partyCopy[12 + i] == GB_NO_MON)
            break;
    }
    if (count > i)
        count = i;

    if (count == 0)
        return FALSE;

#if TIME_CAPSULE_DEBUG_LOG
    DebugPrintfLevel(MGBA_LOG_DEBUG, "TC parse: count=%u sp=%u,%u,%u data=%02X,%02X,%02X",
                     count, partyCopy[12], partyCopy[13], partyCopy[14],
                     partyCopy[21], partyCopy[22], partyCopy[23]);
#endif

    // Trainer name for the trade UI.
    GbMon_TextToGen3(partyCopy, gLinkPlayers[1].name, PLAYER_NAME_LENGTH);
    gLinkPlayers[1].language = LANGUAGE_ENGLISH;
    gLinkPlayers[1].version = VERSION_EMERALD;
    // Pretend the GB side has the National Dex so trade.c does not reject
    // Kanto/Johto species that are missing from the Hoenn dex.
    gLinkPlayers[1].progressFlags = 0xF;
    gLinkPlayers[1].progressFlagsCopy = 0xF;
    gLinkPlayers[1].gender = 0;

    if (gen == 1)
        sPartnerTrainerId = count ? ReadBe16(partyCopy + monOffset + 12) : 0;
    else
        sPartnerTrainerId = ReadBe16(partyCopy + 19);
    gLinkPlayers[1].trainerId = sPartnerTrainerId;

    memset(gParties[B_TRAINER_OPPONENT_A], 0, sizeof(gParties[B_TRAINER_OPPONENT_A]));
    gPartiesCount[B_TRAINER_OPPONENT_A] = 0;

    for (i = 0; i < count; i++)
    {
        u8 speciesIndex = partyCopy[12 + i];
        const u8 *monData = partyCopy + monOffset + i * monSize;
        const u8 *otName = partyCopy + otOffset + i * 11;
        const u8 *nickname = partyCopy + nickOffset + i * 11;
        bool8 isEgg = FALSE;
        bool8 ok;

        if (speciesIndex == GB_NO_MON)
            break;
        if (gen == 2 && speciesIndex == GB_GEN2_EGG_INDEX)
            isEgg = TRUE;

        if (gen == 1)
            ok = GbMon_CreateFromGen1(monData, otName, nickname, &gParties[B_TRAINER_OPPONENT_A][i]);
        else
            ok = GbMon_CreateFromGen2(monData, otName, nickname, isEgg, &gParties[B_TRAINER_OPPONENT_A][i]);

        if (!ok)
        {
#if TIME_CAPSULE_DEBUG_LOG
            DebugPrintfLevel(MGBA_LOG_DEBUG, "TC parse: mon%u sp=%u lvl=%u mv=%u,%u,%u,%u failed",
                             i, speciesIndex, monData[31], monData[2], monData[3],
                             monData[4], monData[5]);
#endif
            return FALSE;
        }
        gPartiesCount[B_TRAINER_OPPONENT_A]++;
    }

    return gPartiesCount[B_TRAINER_OPPONENT_A] != 0;
}

