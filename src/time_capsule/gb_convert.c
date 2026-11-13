#include "global.h"
#include "time_capsule.h"
#include "time_capsule/gb_convert.h"
#include "pokemon.h"
#include "pokeball.h"
#include "move.h"
#include "link.h"
#include "main.h"
#include "random.h"
#include "../data/time_capsule/tables.h"
#include "../data/time_capsule/item_map.h"

#if TIME_CAPSULE_DEBUG_LOG
#include "gba/isagbprint.h"
#endif

// Gen I/II <-> Gen III conversion for the Time Capsule.
//
// The wire formats are documented in docs/time_capsule_research.md.
// Gen II party struct: 48 bytes, big-endian on the wire.
// Gen I party struct:  44 bytes, big-endian on the wire.
//
// This is a pragmatic v1 conversion: it preserves species, level, moves, PP,
// item (where a mapping exists), nature (stored in EXP), gender, shininess,
// Unown letter, IVs/DVs, EVs/stat exp, friendship, Pokerus and names.
// Exact PID, ribbons, contest stats, mail, ball and detailed met data are lost.

#define GEN2_TEXT_FALLBACK 0xE6 // '?' in the Gen I/II charset
#define GEN3_TEXT_FALLBACK 0xAC // '?' in the Gen III charset
#define GEN12_EOS          0x50
#define GEN3_EOS           0xFF
#define GEN12_SPACE        0x7F

static const u8 sGen2GenderThresholds[10] = {8, 0, 2, 4, 12, 14, 16, 17, 0, 16};

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

static u16 ReadBe16(const u8 *p)
{
    return (p[0] << 8) | p[1];
}

static void WriteBe16(u8 *p, u16 value)
{
    p[0] = value >> 8;
    p[1] = value;
}

static u32 ReadBe24(const u8 *p)
{
    return ((u32)p[0] << 16) | ((u32)p[1] << 8) | p[2];
}

static void WriteBe24(u8 *p, u32 value)
{
    p[0] = value >> 16;
    p[1] = value >> 8;
    p[2] = value;
}

static u16 Isqrt(u32 value)
{
    u32 root = 0;

    while ((root + 1) * (root + 1) <= value)
        root++;
    return root;
}

static u8 GetGen2GenderKind(u16 species)
{
    if (species >= ARRAY_COUNT(gGen2GenderKinds))
        return 0;
    return gGen2GenderKinds[species];
}

static u8 GetGen2GenderThreshold(u16 species)
{
    u8 kind = GetGen2GenderKind(species);

    if (kind >= ARRAY_COUNT(sGen2GenderThresholds))
        return 0;
    return sGen2GenderThresholds[kind];
}

static u8 GetGen2Gender(u16 species, u8 atkDv)
{
    u8 kind = GetGen2GenderKind(species);
    u8 threshold = GetGen2GenderThreshold(species);

    if (kind == 1 || kind == 8)
        return MON_MALE;
    if (kind == 6 || kind == 9)
        return MON_FEMALE;
    if (kind == 7)
        return MON_GENDERLESS;
    return atkDv >= threshold ? MON_MALE : MON_FEMALE;
}

static bool8 IsShinyGen2(u8 atk, u8 def, u8 spa, u8 spe)
{
    return (atk & 2) && def == 10 && spa == 10 && spe == 10;
}

static u8 GetGen2UnownLetter(u8 atk, u8 def, u8 spa, u8 spe)
{
    u16 value = (((atk >> 1) & 3) << 6)
              | (((def >> 1) & 3) << 4)
              | (((spe >> 1) & 3) << 2)
              | (((spa >> 1) & 3) << 0);

    return value / 10;
}

static u8 GetGen2HpDv(u8 atk, u8 def, u8 spa, u8 spe)
{
    return ((atk & 1) << 3) | ((def & 1) << 2) | ((spe & 1) << 1) | (spa & 1);
}

// ---------------------------------------------------------------------------
// Text conversion (international charset; Japanese is a later phase)
// ---------------------------------------------------------------------------

static u8 ConvertTextGen3ToGen12(const u8 *src, u8 *dst, u8 maxLen)
{
    u8 i;

    for (i = 0; i < maxLen; i++)
    {
        if (src[i] == GEN3_EOS)
        {
            break;
        }
        else if (src[i] >= 0xFC)
        {
            dst[i] = GEN2_TEXT_FALLBACK;
        }
        else
        {
            dst[i] = gGen3ToGen12TextInt[src[i]];
        }
    }
    dst[i] = GEN12_EOS;
    return i;
}

static u8 ConvertTextGen12ToGen3(const u8 *src, u8 *dst, u8 maxLen)
{
    u8 i;

    for (i = 0; i < maxLen; i++)
    {
        if (src[i] == GEN12_EOS)
            break;
        dst[i] = gGen12ToGen3TextInt[src[i]];
    }
    dst[i] = GEN3_EOS;
    return i;
}

// ---------------------------------------------------------------------------
// Item mapping
// ---------------------------------------------------------------------------
// The expansion renumbers its items, so tools/time_capsule/generate_item_map.py
// matches the expansion item names against the official Gen III names and
// pre-applies the Pokemon-Gen3-to-Gen-X item tables (item_gen3_to_12.bin and
// item_gen12_to_3.bin).  0xFE/0xFF mean "cannot be held in Gen I/II".

static u8 ItemExpansionToGen2(u16 item)
{
    if (item >= ITEMS_COUNT)
        return 0;

    item = gTimeCapsuleItemToGen2[item];
    if (item == 0xFE || item == 0xFF)
        return 0;
    return item;
}

static u16 ItemGen2ToExpansion(u8 item)
{
    return gTimeCapsuleItemFromGen2[item];
}

// ---------------------------------------------------------------------------
// Stats
// ---------------------------------------------------------------------------

static u16 GetGen12StatExpContribution(u16 statExp)
{
    // Pokemon-Gen3-to-Gen-X uses ceil(sqrt(stat_exp)), clamped to 255.
    u16 root = Isqrt(statExp);

    if ((u32)root * root < statExp)
        root++;
    if (root >= 0x100)
        root = 0xFF;
    return root >> 2;
}

static u16 CalcGen12Stat(u16 base, u8 dv, u16 statExp, u8 level, bool8 isHp)
{
    u16 stat = isHp ? (level + 10) : 5;

    stat += (((base + dv) * 2 + GetGen12StatExpContribution(statExp)) * level) / 100;
    return stat;
}

// Gen I/II base stats, straight from Pokemon-Gen3-to-Gen-X's tables.  The
// expansion's base stats are the current-generation values; the GB games use
// their own tables, so the transferred Pokémon must be calculated with those.
static u16 GetGen2BaseStat(u16 national, u8 index)
{
    // Party stat order: HP, Atk, Def, Spe, SpA, SpD.
    // Table order:       HP, Atk, Def, SpA, SpD, Spe.
    static const u8 map[6] = {0, 1, 2, 5, 3, 4};

    if (national > GB_LAST_GEN2_SPECIES)
        national = 0;
    return gGen2BaseStats[national * 6 + map[index]];
}

static u16 GetGen1BaseStat(u16 internal, u8 index)
{
    // Table order: HP, Atk, Def, Spe, Special.
    if (internal > GB_LAST_GEN1_SPECIES)
        internal = 0;
    return gGen1BaseStats[internal * 5 + index];
}

// Gen III -> Gen I/II effort values.  Pokemon-Gen3-to-Gen-X collapses the
// total effort into the same stat experience for every Gen I/II stat:
//   total >= 510 -> 65535, else total * total / 4
// It also intentionally zeroes the EVs in the other direction (Gen2 -> Gen3),
// because a direct stat-exp conversion is atrocious for competitive play.
static u16 GetGen2StatExpFromEvs(struct Pokemon *mon)
{
    u32 total = 0;
    u8 i;

    for (i = 0; i < 6; i++)
        total += GetMonData(mon, MON_DATA_HP_EV + i);

    if (total >= 510)
        return 65535;
    return (total * total) >> 2;
}

// Gen III -> Gen I/II PP: base PP plus 20% per PP Up, capped so the six-bit
// Gen I/II PP field and the two PP-Up bits cannot overflow.
static u8 GetGen12Pp(u16 move, u8 bonus)
{
    u16 base = GetMovePP(move);
    u16 pp = base + (base / 5) * bonus;
    u16 cap = 40 + 7 * bonus;

    if (pp > cap)
        pp = cap;
    if (pp > 63)
        pp = 63;
    return pp;
}

// ---------------------------------------------------------------------------
// EXP / nature helpers
// ---------------------------------------------------------------------------

static u32 GetExpForLevel(u16 species, u8 level)
{
    if (level < 2)
        level = 2;
    if (level > MAX_LEVEL)
        level = MAX_LEVEL;
    return gExperienceTables[gSpeciesInfo[species].growthRate][level];
}

// Store the nature in the EXP (the same trick used by PCCS / VC transfers).
static u32 GetGen12ExpWithNature(u16 species, u8 level, u8 nature)
{
    u32 base = GetExpForLevel(species, level);
    u32 value = base + ((nature + 25 - (base % 25)) % 25);

    if (level < MAX_LEVEL && value >= GetExpForLevel(species, level + 1))
        value = base;
    if (level == MAX_LEVEL)
        value = base;
    return value;
}

// Clamp the Gen I/II experience to the range of its level, exactly like
// get_proper_exp_pure() in Pokemon-Gen3-to-Gen-X.  The remainder modulo 25 is
// the nature the GB game stores there.
static u32 ClampGen12Exp(u16 species, u8 level, u32 exp)
{
    u8 growth = gSpeciesInfo[species].growthRate;
    u32 minExp = gExperienceTables[growth][level];
    u32 maxExp = minExp;

    if (level < MAX_LEVEL)
        maxExp = gExperienceTables[growth][level + 1] - 1;
    if (exp < minExp)
        exp = minExp;
    if (exp > maxExp)
        exp = maxExp;
    return exp;
}

static u8 GetNatureFromExp(u32 exp)
{
    return exp % 25;
}

// ---------------------------------------------------------------------------
// PID generation
// ---------------------------------------------------------------------------

static u32 GenerateGen3Pid(u16 species, u8 gender, u8 nature, u8 unownLetter, u32 otId, bool8 shiny)
{
    u32 pid = 0;
    u32 tries;

    for (tries = 0; tries < 200000; tries++)
    {
        pid = GetMonPersonality(species, gender, nature, unownLetter);
        if ((GET_SHINY_VALUE(otId, pid) < SHINY_ODDS) == shiny)
            return pid;
    }
    return pid;
}

// ---------------------------------------------------------------------------
// Gen III -> Gen I/II
// ---------------------------------------------------------------------------

static bool8 BuildGen2Mon(struct Pokemon *mon, u8 *dst)
{
    u16 species = GetMonData(mon, MON_DATA_SPECIES);
    // The wire format uses the National Dex number; the internal SPECIES_*
    // constant is only used for the tables below.
    u16 national = SpeciesToNationalPokedexNum(species);
    u16 moves[4] = {0};
    u8 pps[4] = {0};
    u16 statExpValue;
    u8 atkDv, defDv, spaDv, speDv;
    u8 level, i, usedMoves = 0, ppBonuses;
    u32 otId, exp;
    u32 hp;
    u8 status;
    bool8 isEgg = GetMonData(mon, MON_DATA_IS_EGG);
    bool8 isShiny = IsMonShiny(mon);
    u8 gender = GetMonGender(mon);
    u8 threshold;
    u8 pokerus;

    if (national == 0 || national > GB_LAST_GEN2_SPECIES)
        return FALSE;
    if (isEgg && national > GB_LAST_GEN2_SPECIES)
        return FALSE;

    memset(dst, 0, GB_MON_SIZE_GEN2);

    // Species and item.
    dst[0] = national;
    dst[1] = ItemExpansionToGen2(GetMonData(mon, MON_DATA_HELD_ITEM));

    // Moves / PP.
    ppBonuses = GetMonData(mon, MON_DATA_PP_BONUSES);
    for (i = 0; i < 4; i++)
    {
        u16 move = GetMonData(mon, MON_DATA_MOVE1 + i);
        u8 j, duplicate = FALSE, bonus;

        if (move == MOVE_NONE || move > GB_LAST_GEN2_MOVE || move == GB_STRUGGLE)
            continue;
        for (j = 0; j < usedMoves; j++)
        {
            if (moves[j] == move)
            {
                duplicate = TRUE;
                break;
            }
        }
        if (duplicate)
            continue;
        moves[usedMoves] = move;
        bonus = (ppBonuses >> (2 * i)) & 3;
        pps[usedMoves] = GetGen12Pp(move, bonus) | (bonus << 6);
        usedMoves++;
    }
    if (usedMoves == 0)
        return FALSE;

    for (i = 0; i < usedMoves; i++)
    {
        dst[2 + i] = moves[i];
        dst[23 + i] = pps[i];
    }

    otId = GetMonData(mon, MON_DATA_OT_ID);
    WriteBe16(dst + 6, otId & 0xFFFF);

    level = GetMonData(mon, MON_DATA_LEVEL);
    if (level < 2)
        level = 2;
    if (level > MAX_LEVEL)
        level = MAX_LEVEL;
    if (isEgg)
        level = 5;

    exp = isEgg ? GetExpForLevel(species, 5)
                : GetGen12ExpWithNature(species, level, GetNature(mon));
    WriteBe24(dst + 8, exp);

    // Stat exp: the reference collapses the total effort into every stat.
    statExpValue = GetGen2StatExpFromEvs(mon);
    for (i = 0; i < 5; i++)
        WriteBe16(dst + 11 + i * 2, statExpValue);

    // DVs.
    atkDv = GetMonData(mon, MON_DATA_ATK_IV) >> 1;
    defDv = GetMonData(mon, MON_DATA_DEF_IV) >> 1;
    spaDv = (GetMonData(mon, MON_DATA_SPATK_IV) + GetMonData(mon, MON_DATA_SPDEF_IV)) >> 2;
    speDv = GetMonData(mon, MON_DATA_SPEED_IV) >> 1;

    threshold = GetGen2GenderThreshold(species);

    if (isShiny)
    {
        // A female shiny with a 7:1 ratio cannot exist in Gen II.
        if (gender == MON_FEMALE && threshold > 0 && threshold <= 2)
            return FALSE;
        atkDv |= 2;
        defDv = 10;
        spaDv = 10;
        speDv = 10;
    }
    else if (IsShinyGen2(atkDv, defDv, spaDv, speDv))
    {
        speDv ^= 1; // avoid an accidental shiny
    }

    // Gender.
    if (threshold > 0 && threshold < 16)
    {
        if (gender == MON_FEMALE)
        {
            if (atkDv >= threshold)
                atkDv = threshold - 1;
        }
        else
        {
            if (atkDv < threshold)
                atkDv = threshold;
        }
    }

    // Unown letter.
    if (species == SPECIES_UNOWN)
    {
        u8 letter = GET_UNOWN_LETTER(GetMonData(mon, MON_DATA_PERSONALITY));
        u8 min = letter * 10;
        u16 max = (letter + 1) * 10 - 1;
        u8 sum;

        if (isShiny && letter != 8 && letter != 21)
            return FALSE;
        if (max > 0xFF)
            max = 0xFF;
        sum = ((spaDv & 6) >> 1) | ((speDv & 6) << 1) | ((defDv & 6) << 3) | ((atkDv & 6) << 5);
        if (sum < min)
            sum = min;
        if (sum > max)
            sum = max;
        atkDv = (atkDv & 9) | ((sum >> 5) & 6);
        defDv = (defDv & 9) | ((sum >> 3) & 6);
        speDv = (speDv & 9) | ((sum >> 1) & 6);
        spaDv = (spaDv & 9) | ((sum << 1) & 6);
        if (GetGen2UnownLetter(atkDv, defDv, spaDv, speDv) != letter)
            return FALSE;
    }

    WriteBe16(dst + 21, (speDv << 12) | (spaDv << 8) | (atkDv << 4) | defDv);

    dst[27] = isEgg ? GetMonData(mon, MON_DATA_FRIENDSHIP) : 70;
    if (dst[27] == 0)
        dst[27] = 1;

    pokerus = GetMonData(mon, MON_DATA_POKERUS);
    if (pokerus)
    {
        u8 strain = (pokerus >> 4) & 0xF;
        u8 days = pokerus & 0xF;

        if (strain > 8)
            strain = 8;
        if (strain == 0)
            strain = 1;
        if (days > ((strain & 3) + 1))
            days = (strain & 3) + 1;
        pokerus = (strain << 4) | days;
    }
    dst[28] = pokerus;

    dst[29] = 0x41; // caught time = 1, caught level = 1
    dst[30] = ((GetMonData(mon, MON_DATA_OT_GENDER) & 1) << 7) | 1; // location 1

    dst[31] = level;
    status = GetMonData(mon, MON_DATA_STATUS);
    dst[32] = status;
    dst[33] = 0;

    hp = isEgg ? 0 : GetMonData(mon, MON_DATA_HP);
    WriteBe16(dst + 34, hp);

    // Gen II stats, computed from the Gen II base stats + DVs + stat exp.
    WriteBe16(dst + 36, CalcGen12Stat(GetGen2BaseStat(national, 0), GetGen2HpDv(atkDv, defDv, spaDv, speDv), statExpValue, level, TRUE));
    WriteBe16(dst + 38, CalcGen12Stat(GetGen2BaseStat(national, 1), atkDv, statExpValue, level, FALSE));
    WriteBe16(dst + 40, CalcGen12Stat(GetGen2BaseStat(national, 2), defDv, statExpValue, level, FALSE));
    WriteBe16(dst + 42, CalcGen12Stat(GetGen2BaseStat(national, 3), speDv, statExpValue, level, FALSE));
    WriteBe16(dst + 44, CalcGen12Stat(GetGen2BaseStat(national, 4), spaDv, statExpValue, level, FALSE));
    WriteBe16(dst + 46, CalcGen12Stat(GetGen2BaseStat(national, 5), spaDv, statExpValue, level, FALSE));

    return TRUE;
}

static bool8 BuildGen1Mon(struct Pokemon *mon, u8 *dst)
{
    u16 species = GetMonData(mon, MON_DATA_SPECIES);
    // The Gen I index table is indexed by the National Dex number.
    u16 national = SpeciesToNationalPokedexNum(species);
    u16 internal;
    u16 moves[4] = {0};
    u8 pps[4] = {0};
    u16 statExpValue;
    u8 atkDv, defDv, spaDv, speDv;
    u8 level, i, usedMoves = 0, ppBonuses;
    u32 otId, exp;
    u32 hp;
    u8 status;
    bool8 isEgg = GetMonData(mon, MON_DATA_IS_EGG);
    bool8 isShiny = IsMonShiny(mon);
    u8 gender = GetMonGender(mon);
    u8 threshold;

    if (national == 0 || national > GB_LAST_GEN1_SPECIES)
        return FALSE;
    if (isEgg)
        return FALSE;
    internal = gNatToGen1Species[national];
    if (internal == 0 || internal > GB_LAST_GEN1_SPECIES)
        return FALSE;

    memset(dst, 0, GB_MON_SIZE_GEN1);

    dst[0] = internal;

    // Moves / PP.
    ppBonuses = GetMonData(mon, MON_DATA_PP_BONUSES);
    for (i = 0; i < 4; i++)
    {
        u16 move = GetMonData(mon, MON_DATA_MOVE1 + i);
        u8 j, duplicate = FALSE, bonus;

        if (move == MOVE_NONE || move > GB_LAST_GEN1_MOVE || move == GB_STRUGGLE)
            continue;
        for (j = 0; j < usedMoves; j++)
        {
            if (moves[j] == move)
            {
                duplicate = TRUE;
                break;
            }
        }
        if (duplicate)
            continue;
        moves[usedMoves] = move;
        bonus = (ppBonuses >> (2 * i)) & 3;
        pps[usedMoves] = GetGen12Pp(move, bonus) | (bonus << 6);
        usedMoves++;
    }
    if (usedMoves == 0)
        return FALSE;

    for (i = 0; i < usedMoves; i++)
    {
        dst[8 + i] = moves[i];
        dst[29 + i] = pps[i];
    }

    otId = GetMonData(mon, MON_DATA_OT_ID);
    WriteBe16(dst + 12, otId & 0xFFFF);

    level = GetMonData(mon, MON_DATA_LEVEL);
    if (level < 2)
        level = 2;
    if (level > MAX_LEVEL)
        level = MAX_LEVEL;
    exp = GetGen12ExpWithNature(species, level, GetNature(mon));
    WriteBe24(dst + 14, exp);

    // Stat exp: the reference collapses the total effort into every stat.
    statExpValue = GetGen2StatExpFromEvs(mon);
    for (i = 0; i < 5; i++)
        WriteBe16(dst + 17 + i * 2, statExpValue);

    atkDv = GetMonData(mon, MON_DATA_ATK_IV) >> 1;
    defDv = GetMonData(mon, MON_DATA_DEF_IV) >> 1;
    spaDv = (GetMonData(mon, MON_DATA_SPATK_IV) + GetMonData(mon, MON_DATA_SPDEF_IV)) >> 2;
    speDv = GetMonData(mon, MON_DATA_SPEED_IV) >> 1;

    threshold = GetGen2GenderThreshold(species);

    if (isShiny)
    {
        if (gender == MON_FEMALE && threshold > 0 && threshold <= 2)
            return FALSE;
        atkDv |= 2;
        defDv = 10;
        spaDv = 10;
        speDv = 10;
    }
    else if (IsShinyGen2(atkDv, defDv, spaDv, speDv))
    {
        speDv ^= 1;
    }

    if (threshold > 0 && threshold < 16)
    {
        if (gender == MON_FEMALE)
        {
            if (atkDv >= threshold)
                atkDv = threshold - 1;
        }
        else
        {
            if (atkDv < threshold)
                atkDv = threshold;
        }
    }

    WriteBe16(dst + 27, (speDv << 12) | (spaDv << 8) | (atkDv << 4) | defDv);

    dst[3] = level; // box level
    status = GetMonData(mon, MON_DATA_STATUS);
    dst[4] = status;
    dst[5] = gGen1Types[internal * 2];
    dst[6] = gGen1Types[internal * 2 + 1];
    dst[7] = ItemExpansionToGen2(GetMonData(mon, MON_DATA_HELD_ITEM));

    hp = GetMonData(mon, MON_DATA_HP);
    WriteBe16(dst + 1, hp);

    dst[33] = level;
    WriteBe16(dst + 34, CalcGen12Stat(GetGen1BaseStat(internal, 0), GetGen2HpDv(atkDv, defDv, spaDv, speDv), statExpValue, level, TRUE));
    WriteBe16(dst + 36, CalcGen12Stat(GetGen1BaseStat(internal, 1), atkDv, statExpValue, level, FALSE));
    WriteBe16(dst + 38, CalcGen12Stat(GetGen1BaseStat(internal, 2), defDv, statExpValue, level, FALSE));
    WriteBe16(dst + 40, CalcGen12Stat(GetGen1BaseStat(internal, 3), speDv, statExpValue, level, FALSE));
    WriteBe16(dst + 42, CalcGen12Stat(GetGen1BaseStat(internal, 4), spaDv, statExpValue, level, FALSE));

    return TRUE;
}

// ---------------------------------------------------------------------------
// Gen I/II -> Gen III
// ---------------------------------------------------------------------------

static bool8 CreateMonFromGen2(const u8 *src, const u8 *otNameSrc, const u8 *nicknameSrc, bool8 isEgg, struct Pokemon *mon)
{
    u16 national = src[0];
    u16 species;
    u16 moves[4];
    u8 pps[4];
    u16 otId;
    u32 exp;
    u8 level, i, usedMoves = 0, ppBonuses = 0;
    u8 atkDv, defDv, spaDv, speDv;
    u8 hpDv, gender, nature, unownLetter = 0;
    bool8 shiny;
    u32 pid;
    u8 name[POKEMON_NAME_LENGTH + 1];
    u8 ot[PLAYER_NAME_LENGTH + 1];
    u8 language = LANGUAGE_ENGLISH;
    u8 metLocation = TIME_CAPSULE_DEFAULT_MET_LOCATION;
    u8 metLevel, metGame = gGameVersion;
    u8 ball = BALL_POKE;
    u8 otGender = 0;
    u32 hp = 0;
    u8 isEggFlag = isEgg;

    if (national == 0 || national > GB_LAST_GEN2_SPECIES)
        return FALSE;
    // The wire format carries the National Dex number.
    species = NationalPokedexNumToSpecies(national);
    if (species == SPECIES_NONE)
        return FALSE;

    level = src[31];
    if (level < 2)
        level = 2;
    if (level > MAX_LEVEL)
        level = MAX_LEVEL;

    for (i = 0; i < 4; i++)
    {
        u16 move = src[2 + i];
        u8 j, duplicate = FALSE, bonus;

        if (move == 0 || move > GB_LAST_GEN2_MOVE || move == GB_STRUGGLE)
            continue;
        for (j = 0; j < usedMoves; j++)
        {
            if (moves[j] == move)
            {
                duplicate = TRUE;
                break;
            }
        }
        if (duplicate)
            continue;

        moves[usedMoves] = move;
        bonus = (src[23 + i] >> 6) & 3;
        pps[usedMoves] = GetMovePP(move);
        ppBonuses |= bonus << (2 * usedMoves);
        usedMoves++;
    }
    if (usedMoves == 0)
        return FALSE;

    otId = ReadBe16(src + 6);
    exp = ReadBe24(src + 8);
    nature = GetNatureFromExp(exp);

    atkDv = (ReadBe16(src + 21) >> 4) & 0xF;
    defDv = ReadBe16(src + 21) & 0xF;
    speDv = (ReadBe16(src + 21) >> 12) & 0xF;
    spaDv = (ReadBe16(src + 21) >> 8) & 0xF;
    hpDv = GetGen2HpDv(atkDv, defDv, spaDv, speDv);

    shiny = IsShinyGen2(atkDv, defDv, spaDv, speDv);
    gender = GetGen2Gender(species, atkDv);
    if (species == SPECIES_UNOWN)
    {
        unownLetter = GetGen2UnownLetter(atkDv, defDv, spaDv, speDv);
        if (unownLetter >= 26)
            return FALSE;
        unownLetter++;
    }

    pid = GenerateGen3Pid(species, gender, nature, unownLetter, otId, shiny);
    CreateMon(mon, species, level, pid, OTID_STRUCT_PRESET(otId));

    // IVs: DV * 2.
    {
        u8 iv;

        iv = hpDv * 2;
        SetMonData(mon, MON_DATA_HP_IV, &iv);
        iv = atkDv * 2;
        SetMonData(mon, MON_DATA_ATK_IV, &iv);
        iv = defDv * 2;
        SetMonData(mon, MON_DATA_DEF_IV, &iv);
        iv = speDv * 2;
        SetMonData(mon, MON_DATA_SPEED_IV, &iv);
        iv = spaDv * 2;
        SetMonData(mon, MON_DATA_SPATK_IV, &iv);
        iv = spaDv * 2;
        SetMonData(mon, MON_DATA_SPDEF_IV, &iv);
    }

    // The reference deliberately starts the received Pokémon with 0 EVs.
    for (i = 0; i < 6; i++)
    {
        u8 zero = 0;

        SetMonData(mon, MON_DATA_HP_EV + i, &zero);
    }

    for (i = 0; i < usedMoves; i++)
    {
        SetMonData(mon, MON_DATA_MOVE1 + i, &moves[i]);
        SetMonData(mon, MON_DATA_PP1 + i, &pps[i]);
    }
    SetMonData(mon, MON_DATA_PP_BONUSES, &ppBonuses);

    {
        u16 item = ItemGen2ToExpansion(src[1]);
        u8 friendship = 70;
        u8 pokerus = src[28];

        SetMonData(mon, MON_DATA_HELD_ITEM, &item);
        if (isEgg)
        {
            friendship = src[27];
            if (friendship == 0)
                friendship = 1;
        }
        SetMonData(mon, MON_DATA_FRIENDSHIP, &friendship);
        SetMonData(mon, MON_DATA_POKERUS, &pokerus);
    }

    ConvertTextGen12ToGen3(nicknameSrc, name, POKEMON_NAME_LENGTH);
    SetMonData(mon, MON_DATA_NICKNAME, name);
    ConvertTextGen12ToGen3(otNameSrc, ot, PLAYER_NAME_LENGTH);
    SetMonData(mon, MON_DATA_OT_NAME, ot);
    SetMonData(mon, MON_DATA_LANGUAGE, &language);

    {
        u8 newLevel = isEgg ? 5 : level;
        u32 newExp = ClampGen12Exp(species, newLevel, exp);

        SetMonData(mon, MON_DATA_EXP, &newExp);
        SetMonData(mon, MON_DATA_LEVEL, &newLevel);
    }

    metLevel = isEgg ? 0 : level;
    SetMonData(mon, MON_DATA_MET_LOCATION, &metLocation);
    SetMonData(mon, MON_DATA_MET_LEVEL, &metLevel);
    SetMonData(mon, MON_DATA_MET_GAME, &metGame);
    SetMonData(mon, MON_DATA_POKEBALL, &ball);

    otGender = (src[30] >> 7) & 1;
    SetMonData(mon, MON_DATA_OT_GENDER, &otGender);

    if (isEgg)
        SetMonData(mon, MON_DATA_IS_EGG, &isEggFlag);

    CalculateMonStats(mon);

    if (isEgg)
        SetMonData(mon, MON_DATA_HP, &hp);

    return TRUE;
}

static bool8 CreateMonFromGen1(const u8 *src, const u8 *otNameSrc, const u8 *nicknameSrc, struct Pokemon *mon)
{
    u16 internal = src[0];
    u16 species;
    u16 moves[4];
    u8 pps[4];
    u16 otId;
    u32 exp;
    u8 level, i, usedMoves = 0, ppBonuses = 0;
    u8 atkDv, defDv, spaDv, speDv;
    u8 hpDv, gender, nature;
    bool8 shiny;
    u32 pid;
    u8 name[POKEMON_NAME_LENGTH + 1];
    u8 ot[PLAYER_NAME_LENGTH + 1];
    u8 language = LANGUAGE_ENGLISH;
    u8 metLocation = TIME_CAPSULE_DEFAULT_MET_LOCATION;
    u8 metLevel, metGame = gGameVersion;
    u8 ball = BALL_POKE;
    u8 otGender = 0;

    if (internal == 0 || internal > GB_LAST_GEN1_SPECIES)
        return FALSE;
    species = gGen1ToNatSpecies[internal];
    if (species == 0 || species > GB_LAST_GEN1_SPECIES)
        return FALSE;
    // gGen1ToNatSpecies gives the National Dex number; the rest of the
    // conversion uses the internal SPECIES_* constant.
    species = NationalPokedexNumToSpecies(species);
    if (species == SPECIES_NONE)
        return FALSE;

    level = src[33];
    if (level < 2)
        level = 2;
    if (level > MAX_LEVEL)
        level = MAX_LEVEL;

    for (i = 0; i < 4; i++)
    {
        u16 move = src[8 + i];
        u8 j, duplicate = FALSE, bonus;

        if (move == 0 || move > GB_LAST_GEN1_MOVE || move == GB_STRUGGLE)
            continue;
        for (j = 0; j < usedMoves; j++)
        {
            if (moves[j] == move)
            {
                duplicate = TRUE;
                break;
            }
        }
        if (duplicate)
            continue;

        moves[usedMoves] = move;
        bonus = (src[29 + i] >> 6) & 3;
        pps[usedMoves] = GetMovePP(move);
        ppBonuses |= bonus << (2 * usedMoves);
        usedMoves++;
    }
    if (usedMoves == 0)
        return FALSE;

    otId = ReadBe16(src + 12);
    exp = ReadBe24(src + 14);
    nature = GetNatureFromExp(exp);

    atkDv = (ReadBe16(src + 27) >> 4) & 0xF;
    defDv = ReadBe16(src + 27) & 0xF;
    speDv = (ReadBe16(src + 27) >> 12) & 0xF;
    spaDv = (ReadBe16(src + 27) >> 8) & 0xF;
    hpDv = GetGen2HpDv(atkDv, defDv, spaDv, speDv);

    shiny = IsShinyGen2(atkDv, defDv, spaDv, speDv);
    gender = GetGen2Gender(species, atkDv);

    pid = GenerateGen3Pid(species, gender, nature, 0, otId, shiny);
    CreateMon(mon, species, level, pid, OTID_STRUCT_PRESET(otId));

    {
        u8 iv;

        iv = hpDv * 2;
        SetMonData(mon, MON_DATA_HP_IV, &iv);
        iv = atkDv * 2;
        SetMonData(mon, MON_DATA_ATK_IV, &iv);
        iv = defDv * 2;
        SetMonData(mon, MON_DATA_DEF_IV, &iv);
        iv = speDv * 2;
        SetMonData(mon, MON_DATA_SPEED_IV, &iv);
        iv = spaDv * 2;
        SetMonData(mon, MON_DATA_SPATK_IV, &iv);
        iv = spaDv * 2;
        SetMonData(mon, MON_DATA_SPDEF_IV, &iv);
    }

    // The reference deliberately starts the received Pokémon with 0 EVs.
    for (i = 0; i < 6; i++)
    {
        u8 zero = 0;

        SetMonData(mon, MON_DATA_HP_EV + i, &zero);
    }

    for (i = 0; i < usedMoves; i++)
    {
        SetMonData(mon, MON_DATA_MOVE1 + i, &moves[i]);
        SetMonData(mon, MON_DATA_PP1 + i, &pps[i]);
    }
    SetMonData(mon, MON_DATA_PP_BONUSES, &ppBonuses);

    {
        u16 item = ItemGen2ToExpansion(src[7]);
        u8 friendship = 70;
        u8 pokerus = 0;

        SetMonData(mon, MON_DATA_HELD_ITEM, &item);
        SetMonData(mon, MON_DATA_FRIENDSHIP, &friendship);
        SetMonData(mon, MON_DATA_POKERUS, &pokerus);
    }

    ConvertTextGen12ToGen3(nicknameSrc, name, POKEMON_NAME_LENGTH);
    SetMonData(mon, MON_DATA_NICKNAME, name);
    ConvertTextGen12ToGen3(otNameSrc, ot, PLAYER_NAME_LENGTH);
    SetMonData(mon, MON_DATA_OT_NAME, ot);
    SetMonData(mon, MON_DATA_LANGUAGE, &language);

    {
        u32 newExp = GetGen12ExpWithNature(species, level, nature);

        SetMonData(mon, MON_DATA_EXP, &newExp);
        SetMonData(mon, MON_DATA_LEVEL, &level);
    }

    metLevel = level;
    SetMonData(mon, MON_DATA_MET_LOCATION, &metLocation);
    SetMonData(mon, MON_DATA_MET_LEVEL, &metLevel);
    SetMonData(mon, MON_DATA_MET_GAME, &metGame);
    SetMonData(mon, MON_DATA_POKEBALL, &ball);
    SetMonData(mon, MON_DATA_OT_GENDER, &otGender);

    CalculateMonStats(mon);
    return TRUE;
}

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

    ConvertTextGen3ToGen12(gSaveBlock2Ptr->playerName, party, 10);
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
                ok = BuildGen1Mon(mon, monData);
            else
                ok = BuildGen2Mon(mon, monData);
            if (!ok)
                memset(monData, 0, monSize);
        }

        if (i < count)
        {
            GetMonData(&mons[i], MON_DATA_OT_NAME, name);
            ConvertTextGen3ToGen12(name, party + otOffset + i * 11, 10);
            GetMonData(&mons[i], MON_DATA_NICKNAME, name);
            ConvertTextGen3ToGen12(name, party + nickOffset + i * 11, 10);
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
    ConvertTextGen12ToGen3(partyCopy, gLinkPlayers[1].name, PLAYER_NAME_LENGTH);
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
            ok = CreateMonFromGen1(monData, otName, nickname, &gParties[B_TRAINER_OPPONENT_A][i]);
        else
            ok = CreateMonFromGen2(monData, otName, nickname, isEgg, &gParties[B_TRAINER_OPPONENT_A][i]);

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

// ---------------------------------------------------------------------------
// Compatibility check
// ---------------------------------------------------------------------------

u8 GbConvert_CheckParty(u8 gen, u16 *arg1, u16 *arg2)
{
    u8 count, i;

    *arg1 = 0;
    *arg2 = 0;
    CalculatePlayerPartyCount();
    count = gPartiesCount[B_TRAINER_PLAYER];

    for (i = 0; i < count; i++)
    {
        struct Pokemon *mon = &gParties[B_TRAINER_PLAYER][i];
        u16 species = GetMonData(mon, MON_DATA_SPECIES);
        u16 national = SpeciesToNationalPokedexNum(species);
        u8 mail = GetMonData(mon, MON_DATA_MAIL);
        bool8 isEgg = GetMonData(mon, MON_DATA_IS_EGG);
        u8 j;

        if (species == SPECIES_NONE)
            continue;

        // Regional/alternative forms have no Gen I/II equivalent; only the
        // default form of each species can be traded.
        if (species != NationalPokedexNumToSpecies(national))
            return TIME_CAPSULE_ERR_SPECIES;

        *arg1 = species;

        if (GetMonData(mon, MON_DATA_SANITY_IS_BAD_EGG))
            return TIME_CAPSULE_ERR_BAD_EGG;

        if (mail != 0xFF)
            return TIME_CAPSULE_ERR_MAIL;

        if (gen == 1)
        {
            if (isEgg)
                return TIME_CAPSULE_ERR_EGG;
            if (species > GB_LAST_GEN1_SPECIES || gNatToGen1Species[national] == 0)
                return TIME_CAPSULE_ERR_SPECIES;
        }
        else
        {
            if (national > GB_LAST_GEN2_SPECIES)
                return TIME_CAPSULE_ERR_SPECIES;
        }

        for (j = 0; j < 4; j++)
        {
            u16 move = GetMonData(mon, MON_DATA_MOVE1 + j);
            u16 limit = (gen == 1) ? GB_LAST_GEN1_MOVE : GB_LAST_GEN2_MOVE;

            if (move == 0)
                continue;
            if (move <= limit && move != GB_STRUGGLE)
                continue;

            // The reference silently drops moves that do not exist in the
            // target generation.  Only reject the Pokémon when none of its
            // moves can be represented, otherwise the trade would fail for a
            // single Hoenn move.
            if (*arg2 == 0)
                *arg2 = move;
        }

        {
            u8 validMoves = 0;
            u16 limit = (gen == 1) ? GB_LAST_GEN1_MOVE : GB_LAST_GEN2_MOVE;

            for (j = 0; j < 4; j++)
            {
                u16 move = GetMonData(mon, MON_DATA_MOVE1 + j);

                if (move != 0 && move <= limit && move != GB_STRUGGLE)
                    validMoves++;
            }

            if (validMoves == 0)
                return TIME_CAPSULE_ERR_MOVE;
        }

        // Female shiny with a 7:1 ratio cannot be represented in Gen II.
        if (IsMonShiny(mon) && GetMonGender(mon) == MON_FEMALE)
        {
            u8 threshold = GetGen2GenderThreshold(species);
            if (threshold > 0 && threshold <= 2)
                return TIME_CAPSULE_ERR_SHINY;
        }

        // Unown ? and ! did not exist in Gen II; shiny Unown only I and V.
        if (gen == 2 && species == SPECIES_UNOWN)
        {
            u8 letter = GET_UNOWN_LETTER(GetMonData(mon, MON_DATA_PERSONALITY));
            if (letter >= 26)
                return TIME_CAPSULE_ERR_SPECIES;
            if (IsMonShiny(mon) && letter != 8 && letter != 21)
                return TIME_CAPSULE_ERR_SHINY;
        }
    }

    return TIME_CAPSULE_OK;
}


