#include "global.h"
#include "config/time_capsule.h"
#include "time_capsule.h"
#include "time_capsule/gb_protocol.h"
#include "time_capsule/gb_link.h"
#include "time_capsule/gb_convert.h"
#include "link.h"
#include "trade.h"
#include "pokemon.h"

#if TIME_CAPSULE_DEBUG_LOG
#include "gba/isagbprint.h"
#endif

// Gen I/II link state machine.
//
// The Gen II side is a direct port of the generic protocol of
// Pokemon-Gen3-to-Gen-X (source/communicator.c):
//
//   process_data_arrived_gen2() -> check_if_continue() / communicate_buffer()
//
// with the master tables (the GBA drives the clock, so it is the "master"
// entity of that code):
//
//   enter room:  send 0x01 0x61 0xD1 0x00
//                recv 0x61 0xD1 0x00 0x00
//   start trade: send 0x75 0x75 0x76 0xFE 0xFE
//                recv 0x75 0x00 0xFD 0xFE 0xFE
//
// The important part is when 0x76 is presented.  pokecrystal's
// CheckLinkTimeout_Gen2 first sends action 5 (0x75) and waits for the other
// side's 0x75.  It then sends action 6 (0x76) and waits for the other side's
// 0x76 *reading the byte of the previous exchange*, because the GB is the
// external clock slave.  The master table therefore starts sending 0x76 as
// soon as it has seen the GB's 0x00 acknowledgement of action 5 (sends[2]),
// not when it sees the GB's 0x76.  Doing the latter loses the race against
// CheckLinkTimeout_Gen2 and Crystal reports "Too much time has elapsed.".
//
// After 0xFD (the GB's block preamble) the protocol waits for two 0xFE and
// then enters START_TRADE_SYN; the master initialises the transfer state and
// communicate_buffer() streams the three (or four, for the Trade Center)
// blocks with the reference chunk sizes, generating the 0xFD preamble of each
// chunk itself.  The GB's Serial_ExchangeBytes()/ExchangeBytes() skip those
// preamble bytes and tolerate the leading shift (see the union comment at
// wLinkReceivedPartyData in pokecrystal's ram/wram.asm).
//
// The Gen I path is kept from the previous implementation.  Gen I never runs
// the 0x75/0x76 exchange: after the 0x60/0xD4 handshake the GB sends the
// 0xFD preamble of the RNG block and the block is exchanged directly.

enum
{
    GB_STATE_IDLE = 0,
    GB_STATE_CONNECT,
    GB_STATE_GEN1_SYNC,
    GB_STATE_GEN1_ROOM_WAIT,
    GB_STATE_GEN2,
    GB_STATE_BULK, // Gen I block exchange
    GB_STATE_EXIT,
    GB_STATE_ERROR,
    GB_STATE_DONE,
};

// Port of enum START_TRADE_STATE (communicator.h).
enum
{
    TC_START_UNK = 0,
    TC_START_ENT,
    TC_START_WAI,
    TC_START_STA,
    TC_START_SYN,
    TC_START_PAR,
    TC_START_DON,
};

// Port of enum TRADING_STATE, reduced to the states the GB trade menu uses.
enum
{
    TC_TRADE_NO_INFO = 0,
    TC_TRADE_HAVE_OFFER,
    TC_TRADE_HAVE_ACCEPT,
    TC_TRADE_HAVE_SUCCESS,
};

enum
{
    GB_PARTNER_SELECT = 0,
    GB_PARTNER_CONFIRM,
};

// Gen I/II byte time: 8192 bps -> ~1 ms -> 14 scanlines at 59.7 Hz.
#define GB_LINK_PACE_LINES    14
// Give up on this frame if the GB has not clocked back within this window;
// the transfer stays pending and is retried on the next frame.
#define GB_LINK_REPLY_LINES   10
// 228 scanlines per frame / 14 = 16 bytes; 10 leaves room for the rest of the
// frame (rendering, input) and for slow GB replies.
#define GB_LINK_BYTES_FRAME   10

#define GB_LINK_ALIVE_TIMEOUT      600  // 10 seconds
#define GB_LINK_ALIVE_TIMEOUT_LONG 3600 // 60 seconds

#define GB_CONNECT_TIMEOUT    0
#define GB_BULK_TIMEOUT       3600
#define GB_SUCCESS_TIMEOUT    600
#define GB_EXIT_TIMEOUT       180

// ---------------------------------------------------------------------------
// Faithful Gen II protocol constants and tables.
// ---------------------------------------------------------------------------

// communicator.h
#define SEND_NO_INFO            0xFE
#define SYNCHRONIZE_BYTE        0xFD
#define SEND_0_INFO             0x00
#define START_TRADE_BYTE_GEN2   0x75
#define END_TRADE_BYTE_GEN2     0x7F
#define GEN2_TRADE_OFFER_START  0x70
#define CANCEL_VALUE            0x0F
#define DECLINE_VALUE           0x01
#define ACCEPT_VALUE            0x02

#define MIN_WAIT_FOR_SYN        3
#define MAX_WAIT_FOR_SYN        30

#define TC_MAX_CHUNKS           3
// 10 (RNG) + 444 (party) + 587 (patch list + mail) for the Gen II Trade
// Center.  The Colosseum has no mail (197) and Gen I has a smaller party.
#define TC_OUT_SIZE             (GB_RN_RANDOM_SIZE + GB_PARTY_DATA_GEN2 + GB_PATCH_DATA_SIZE + GB_MAIL_BLOCK_SIZE)

static EWRAM_DATA u8 sState;
static EWRAM_DATA u8 sGen;
static EWRAM_DATA u8 sRoom;
static EWRAM_DATA u8 sForcedGen;
static EWRAM_DATA u8 sResult;
static EWRAM_DATA bool8 sActive;
static EWRAM_DATA bool8 sFinished;
static EWRAM_DATA bool8 sPartyReady;
static EWRAM_DATA bool8 sLinkEstablished;
static EWRAM_DATA bool8 sPeerExited;
static EWRAM_DATA u16 sNoDataFrames;
static EWRAM_DATA u16 sFrameCounter;

// Full wire block (with preambles) built by the converter, and the block
// reassembled from the received payload for the parser.
static EWRAM_DATA u8 sBlock[GB_BLOCK_TRADE_GEN2];
static EWRAM_DATA u8 sBulk[GB_BLOCK_TRADE_GEN2];

// Port of the reference's communication buffers.  The preamble bytes are not
// stored here: communicate_buffer() generates them while synchronising, and
// the converter gets them back when the block is reassembled.
static EWRAM_DATA u8 sOut[TC_OUT_SIZE];
static EWRAM_DATA u8 sIn[TC_OUT_SIZE];
static EWRAM_DATA u16 sSizes[TC_MAX_CHUNKS];
static EWRAM_DATA u16 sSizesIndex;
static EWRAM_DATA u16 sBasePos;
static EWRAM_DATA u16 sBufferCounter;    // also check_if_continue's index
static EWRAM_DATA u16 sBufferCounterOut;
static EWRAM_DATA u8 sSynTransmitted;
static EWRAM_DATA bool8 sHasTransmittedSyn;

// Start-trade tables (master entity of communicator.c).  The room byte is
// filled in by GbProtocol_Init().
static EWRAM_DATA u8 sEnterSend[4];
static EWRAM_DATA u8 sEnterRecv[4];
static const u8 sStartSend[5] = {START_TRADE_BYTE_GEN2, START_TRADE_BYTE_GEN2, 0x76, SEND_NO_INFO, SEND_NO_INFO};
static const u8 sStartRecv[5] = {START_TRADE_BYTE_GEN2, 0x00, SYNCHRONIZE_BYTE, SEND_NO_INFO, SEND_NO_INFO};

static EWRAM_DATA u8 sStartState;
static EWRAM_DATA u8 sTxByte;

// Gen I block emitter/receiver.
static EWRAM_DATA bool8 sSendSynced;
static EWRAM_DATA u8 sSendChunk;
static EWRAM_DATA u16 sSendPos;
static EWRAM_DATA u16 sSendSize;
static EWRAM_DATA u16 sSendOffset;
static EWRAM_DATA bool8 sBulkStarted;
static EWRAM_DATA u8 sRecvChunk;
static EWRAM_DATA u16 sChunkPos;
static EWRAM_DATA u16 sChunkPayload;
static EWRAM_DATA bool8 sChunkSynced;
static EWRAM_DATA u16 sBulkCount;

// Trade menu (port of get_offer/get_accept/get_success).
static EWRAM_DATA u8 sTradingState;
static EWRAM_DATA u8 sTradeOfferOut;
static EWRAM_DATA u8 sTradeOfferIn;
static EWRAM_DATA bool8 sReceivedOnce;
static EWRAM_DATA u8 sLastPeerAction;
static EWRAM_DATA u16 sTradeCmd;
static EWRAM_DATA u16 sTradeArg;
static EWRAM_DATA bool8 sHasTradeCmd;

static EWRAM_DATA u8 sSuccessValue; // anti-cloning nybble, not the full byte
static EWRAM_DATA bool8 sSuccessPrepared;
static EWRAM_DATA bool8 sSuccessDone;
static EWRAM_DATA bool8 sReexchangeStarted;
static EWRAM_DATA u16 sSuccessWaitFrames;
static EWRAM_DATA u8 sExitValue;

static void SetError(u8 error);
static bool8 TcParseReceivedBlock(void);
static void TcComputeSuccessValue(void);

static u8 TradeBase(void)
{
    return (sGen == TIME_CAPSULE_GEN_1) ? 0x60 : 0x70;
}

static u16 PartyDataSize(void)
{
    return (sGen == TIME_CAPSULE_GEN_1) ? GB_PARTY_DATA_GEN1 : GB_PARTY_DATA_GEN2;
}

// ---------------------------------------------------------------------------
// Payload <-> wire block conversion
// ---------------------------------------------------------------------------

static void TcSetSizes(void)
{
    sSizes[0] = GB_RN_RANDOM_SIZE;
    sSizes[1] = PartyDataSize();
    sSizes[2] = GB_PATCH_DATA_SIZE;
    if (sGen == TIME_CAPSULE_GEN_2 && sRoom == TIME_CAPSULE_ROOM_TRADE)
        sSizes[2] += GB_MAIL_BLOCK_SIZE;
}

// The converter builds the complete wire block (preambles included).  The
// reference protocol only streams the payload of each chunk and generates the
// preambles itself, so copy the payloads out of the block.
static void TcBuildOutBuffer(void)
{
    u16 partySize = PartyDataSize();
    u16 partyOffset = GB_RN_BLOCK_SIZE + GB_PARTY_PREAMBLE_SIZE;
    u16 patchOffset = partyOffset + partySize + GB_PATCH_PREAMBLE_SIZE;

    GbConvert_BuildTradeBlock(sGen, sRoom, sBlock);

    memcpy(sOut, sBlock + GB_RN_PREAMBLE_SIZE, GB_RN_RANDOM_SIZE);
    memcpy(sOut + GB_RN_RANDOM_SIZE, sBlock + partyOffset, partySize);
    memcpy(sOut + GB_RN_RANDOM_SIZE + partySize, sBlock + patchOffset, sSizes[2]);
}

// Reassemble the wire layout the converter's parser expects.  The GB games
// skip leading control bytes (see Link_FindFirstNonControlCharacter_SkipZero
// and the wLinkReceivedPartyData union), so the preambles are not part of the
// received payload; put them back here.
static bool8 TcParseReceivedBlock(void)
{
    u16 partySize = PartyDataSize();
    u16 partyOffset = GB_RN_BLOCK_SIZE + GB_PARTY_PREAMBLE_SIZE;
    u16 patchOffset = partyOffset + partySize;
    u16 mailOffset = patchOffset + GB_PATCH_LIST_SIZE;
    u16 size = mailOffset;
    bool8 ok;

    memset(sBulk, 0, sizeof(sBulk));
    memset(sBulk, SYNCHRONIZE_BYTE, GB_RN_PREAMBLE_SIZE);
    memcpy(sBulk + GB_RN_PREAMBLE_SIZE, sIn, GB_RN_RANDOM_SIZE);

    memset(sBulk + GB_RN_BLOCK_SIZE, SYNCHRONIZE_BYTE, GB_PARTY_PREAMBLE_SIZE);
    memcpy(sBulk + partyOffset, sIn + GB_RN_RANDOM_SIZE, partySize);

    memset(sBulk + patchOffset, SYNCHRONIZE_BYTE, GB_PATCH_PREAMBLE_SIZE);
    memcpy(sBulk + patchOffset + GB_PATCH_PREAMBLE_SIZE,
           sIn + GB_RN_RANDOM_SIZE + partySize, GB_PATCH_DATA_SIZE);

    if (sGen == TIME_CAPSULE_GEN_2 && sRoom == TIME_CAPSULE_ROOM_TRADE)
    {
        memcpy(sBulk + mailOffset,
               sIn + GB_RN_RANDOM_SIZE + partySize + GB_PATCH_DATA_SIZE,
               GB_MAIL_BLOCK_SIZE);
        size += GB_MAIL_BLOCK_SIZE;
    }

    ok = GbConvert_ParseTradeBlock(sGen, sRoom, sBulk, size);

#if TIME_CAPSULE_DEBUG_LOG
    DebugPrintfLevel(MGBA_LOG_DEBUG, "TC bulk: parse=%u size=%u", ok, size);
#endif

    if (!ok)
    {
        SetError(TIME_CAPSULE_ERR_SPECIES);
        return FALSE;
    }

    sTxByte = SEND_NO_INFO;
    sPartyReady = TRUE;
    sResult = TIME_CAPSULE_OK;
    sTradingState = TC_TRADE_NO_INFO;
    sTradeOfferIn = 0;
    sReceivedOnce = FALSE;
    sLastPeerAction = 0xFF;
    sSynTransmitted = 0;

#if TIME_CAPSULE_DEBUG_LOG
    DebugPrintfLevel(MGBA_LOG_DEBUG, "TC bulk: ready");
#endif

    return TRUE;
}

// ---------------------------------------------------------------------------
// Port of communicator.c
// ---------------------------------------------------------------------------

static void TcResetTransferBetweenSync(void)
{
    sHasTransmittedSyn = FALSE;
    sSynTransmitted = 0;
    sBufferCounter = 0;
    sBufferCounterOut = 0;
}

static void TcInitTransferData(void)
{
    sBasePos = 0;
    TcResetTransferBetweenSync();
    sSizesIndex = 0;
    sStartState = TC_START_PAR;
}

static u8 TcCheckIfContinue(u8 data, const u8 *sends, const u8 *recvs,
                            u16 size, u8 newState, u8 newSend, u8 filter)
{
    if (filter && data >= 0x10)
        data &= 0xF0;

    if (data == recvs[sBufferCounter])
    {
        sBufferCounter++;
        if (sBufferCounter >= size || sends[sBufferCounter] == SEND_NO_INFO)
        {
            sBufferCounter = 0;
            sStartState = newState;
            return newSend;
        }
    }

    return sends[sBufferCounter];
}

static u8 TcCommunicateBuffer(u8 data)
{
    bool8 ignoreData = FALSE;

    if (!sHasTransmittedSyn)
    {
        if (sSynTransmitted >= MAX_WAIT_FOR_SYN)
            sSynTransmitted = MAX_WAIT_FOR_SYN;

        if (sSynTransmitted < MIN_WAIT_FOR_SYN)
        {
            if (data != SEND_NO_INFO && data == SYNCHRONIZE_BYTE)
                sSynTransmitted++;
            return SYNCHRONIZE_BYTE;
        }

        if (data != SEND_NO_INFO && data != SYNCHRONIZE_BYTE)
        {
            sHasTransmittedSyn = TRUE;
        }
        else
        {
            if (data != SEND_NO_INFO)
                sSynTransmitted++;
            return SYNCHRONIZE_BYTE;
        }
    }

    if (!ignoreData && data != SEND_NO_INFO)
    {
        sIn[sBufferCounter + sBasePos] = data;
        sBufferCounter++;

        if (sBufferCounter == sSizes[sSizesIndex])
        {
            sBasePos += sSizes[sSizesIndex];
            sSizesIndex++;
            TcResetTransferBetweenSync();

            if (sSizesIndex >= TC_MAX_CHUNKS)
            {
                sStartState = TC_START_DON;
                sTradingState = TC_TRADE_NO_INFO;
                if (!TcParseReceivedBlock())
                    return SEND_NO_INFO;
            }

            if (sBufferCounterOut < sSizes[sSizesIndex - 1])
                return sOut[sBasePos - 1];
            return SEND_0_INFO;
        }
    }

    sBufferCounterOut++;
    if (sBufferCounterOut > sSizes[sSizesIndex])
        sBufferCounterOut = sSizes[sSizesIndex];

    return sOut[sBufferCounterOut - 1 + sBasePos];
}

// ---------------------------------------------------------------------------
// Trade menu (port of get_offer/get_accept/get_success)
// ---------------------------------------------------------------------------

static void TcReportTradeCommand(u8 byte, u16 cmd, u16 arg)
{
    if (byte == sLastPeerAction)
        return;

    sLastPeerAction = byte;
    sTradeCmd = cmd;
    sTradeArg = arg;
    sHasTradeCmd = TRUE;

#if TIME_CAPSULE_DEBUG_LOG
    DebugPrintfLevel(MGBA_LOG_DEBUG, "TC peer: %02X cmd=%u arg=%u", byte, cmd, arg);
#endif
}

static u8 TcGetOffer(u8 data)
{
    u8 base = TradeBase();
    u8 limit = base + GB_PARTY_SIZE;

    if ((data >= base && data < limit) || data == base + CANCEL_VALUE)
    {
        if (data != sLastPeerAction)
        {
            if (!sReceivedOnce)
            {
                sReceivedOnce = TRUE;
            }
            else
            {
                sReceivedOnce = FALSE;

                if (data == base + CANCEL_VALUE)
                {
                    // END_TRADE_BYTE_GEN2: the other game left the room.
                    sTradeOfferIn = CANCEL_VALUE;
                    sPeerExited = TRUE;
                    sTradeCmd = LINKCMD_REQUEST_CANCEL;
                    sTradeArg = 0;
                    sHasTradeCmd = TRUE;
                    sLastPeerAction = data;
                    sExitValue = data;
                    sTxByte = data;
                    sState = GB_STATE_EXIT;
                    sFrameCounter = 0;
                    return data;
                }

                sTradeOfferIn = data - base;
                TcReportTradeCommand(data, LINKCMD_READY_TO_TRADE, data - base);
                sTradingState = TC_TRADE_NO_INFO;
            }
        }
    }

    return base + sTradeOfferOut;
}

static u8 TcGetAccept(u8 data)
{
    u8 base = TradeBase();

    if (data == base + DECLINE_VALUE || data == base + ACCEPT_VALUE)
    {
        if (data != sLastPeerAction)
        {
            if (!sReceivedOnce)
            {
                sReceivedOnce = TRUE;
            }
            else
            {
                sReceivedOnce = FALSE;
                sTradeOfferIn = data - base;

                if (data == base + ACCEPT_VALUE)
                    TcReportTradeCommand(data, LINKCMD_INIT_BLOCK, 0);
                else
                    TcReportTradeCommand(data, LINKCMD_READY_CANCEL_TRADE, 0);

                sTradingState = TC_TRADE_NO_INFO;
            }
        }
    }

    return base + sTradeOfferOut;
}

static u8 TcGetSuccess(u8 data)
{
    u8 base = TradeBase();

    if ((data & 0xF0) == (base & 0xF0))
    {
        // The GB is still sending its anti-cloning byte, so it has not
        // received ours yet or is still saving.  Keep presenting ours; the
        // trade is only complete when the GB starts the next block exchange
        // (CheckLinkTimeout_Gen2's 0x75 for Gen II, or the 0x60/0xFD stream
        // for Gen I, which never verifies the byte).
        sReceivedOnce = TRUE;

        if (sGen == TIME_CAPSULE_GEN_1)
        {
            sSuccessDone = TRUE;
            sReexchangeStarted = FALSE;
        }
    }

    return base | sSuccessValue;
}

static void TcProcessTradeMenu(u8 data)
{
    // A restarted link (the GB went back to the Cable Club receptionist)
    // presents 0x61 / the room byte again.  0x71/0x72 are ambiguous with
    // offers/accepts, so only 0x61 and the 0xD0 room byte restart here.
    if (sGen == TIME_CAPSULE_GEN_2 && data == 0x61)
    {
        sStartState = TC_START_ENT;
        sBufferCounter = 0;
        sPartyReady = FALSE;
        sTxByte = 0x01;
        return;
    }

    if (data == 0xD1 || data == 0xD2)
    {
        if (data != (0xD0 | sRoom))
        {
            SetError(TIME_CAPSULE_ERR_WRONG_ROOM);
            return;
        }

        sStartState = TC_START_ENT;
        sBufferCounter = 2;
        sPartyReady = FALSE;
        sLinkEstablished = TRUE;
        sTxByte = data;
        return;
    }

    // After the trade the GB saves and jumps straight back into
    // Gen2ToGen2LinkComms, i.e. into a new CheckLinkTimeout_Gen2.  A 0x75
    // while we are presenting the anti-cloning byte can only be that
    // re-handshake (the success byte itself is 0x70..0x72).  This is the
    // definitive "the other side received our byte and saved" signal.
    if (sTradingState == TC_TRADE_HAVE_SUCCESS && data == START_TRADE_BYTE_GEN2)
    {
        sSuccessDone = TRUE;
        sReexchangeStarted = TRUE;
        sStartState = TC_START_STA;
        sBufferCounter = 0;
        sTradingState = TC_TRADE_NO_INFO;
        sTxByte = START_TRADE_BYTE_GEN2;
        return;
    }

    if (sSynTransmitted < MIN_WAIT_FOR_SYN)
    {
        sSynTransmitted++;
        sTxByte = SEND_NO_INFO;
        return;
    }

    switch (sTradingState)
    {
    case TC_TRADE_HAVE_OFFER:
        sTxByte = TcGetOffer(data);
        break;
    case TC_TRADE_HAVE_ACCEPT:
        sTxByte = TcGetAccept(data);
        break;
    case TC_TRADE_HAVE_SUCCESS:
        sTxByte = TcGetSuccess(data);
        break;
    default:
        sTxByte = SEND_NO_INFO;
        break;
    }
}

// ---------------------------------------------------------------------------
// process_data_arrived_gen2 (master entity)
// ---------------------------------------------------------------------------

static void TcProcessIdle(u8 data)
{
    if (data == 0x01 || data == 0x02)
    {
        // Establishment probe (USING_EXTERNAL_CLOCK / USING_INTERNAL_CLOCK).
        sBufferCounter = 0;
        sStartState = TC_START_ENT;
        sTxByte = sEnterSend[0];
        return;
    }

    if (data == 0x61 || data == 0x62 || data == 0x71 || data == 0x72)
    {
        // Receptionist byte (0x60|action, or 0x70|action after the GB has
        // already selected its room).  Normalise it for the enter table.
        sBufferCounter = 0;
        sStartState = TC_START_ENT;
        sTxByte = sEnterSend[0];
        return;
    }

    if (data == 0xD1 || data == 0xD2)
    {
        // Link_EnsureSync completed before we saw the receptionist byte.
        if (data != (0xD0 | sRoom))
        {
            SetError(TIME_CAPSULE_ERR_WRONG_ROOM);
            return;
        }

        sLinkEstablished = TRUE;
        sBufferCounter = 2;
        sStartState = TC_START_ENT;
        sTxByte = sEnterSend[2];
        return;
    }

    if (data == START_TRADE_BYTE_GEN2)
    {
        sBufferCounter = 0;
        sStartState = TC_START_STA;
        sTxByte = sStartSend[0];
        return;
    }

    if (data == END_TRADE_BYTE_GEN2)
    {
        sPeerExited = TRUE;
        sExitValue = data;
        sTxByte = data;
        sState = GB_STATE_EXIT;
        sFrameCounter = 0;
        return;
    }

    sTxByte = (sStartState == TC_START_UNK) ? sEnterSend[0] : sStartSend[0];
}

static void TcProcessGen2(u8 data)
{
    switch (sStartState)
    {
    case TC_START_ENT:
        if (sBufferCounter == 0 && (data == 0xD1 || data == 0xD2))
        {
            // Link_EnsureSync arrived before the repeating receptionist byte
            // was seen a second time.  Jump straight to the room ack step.
            if (data != (0xD0 | sRoom))
            {
                SetError(TIME_CAPSULE_ERR_WRONG_ROOM);
                break;
            }

            sLinkEstablished = TRUE;
            sBufferCounter = 2;
            sTxByte = sEnterSend[2];
            break;
        }

        if (sBufferCounter == 0)
        {
            // The receptionist phase sends the action with the 0x60 high
            // nibble (0x61/0x62), or 0x71/0x72 if the GB still had a room
            // selected.  The table compares the normalised 0x61.
            if (data == 0x61 || data == 0x62 || data == 0x71 || data == 0x72)
                data = 0x61;
        }
        else if ((data == 0xD1 || data == 0xD2) && data != (0xD0 | sRoom))
        {
            // Link_EnsureSync selected a different room.
            SetError(TIME_CAPSULE_ERR_WRONG_ROOM);
            break;
        }

        sTxByte = TcCheckIfContinue(data, sEnterSend, sEnterRecv, 4,
                                    TC_START_WAI, sStartSend[0], 0);
        break;

    case TC_START_STA:
        if (data == START_TRADE_BYTE_GEN2 && sBufferCounter >= 2)
        {
            // Crystal restarted CheckLinkTimeout_Gen2 (its check 6 failed).
            // Go back to the beginning of the start-trade table and answer
            // its action 5 again instead of deadlocking on 0x76.
            sBufferCounter = 0;
            sTxByte = sStartSend[0];
            break;
        }

        sTxByte = TcCheckIfContinue(data, sStartSend, sStartRecv, 5,
                                    TC_START_SYN, SEND_NO_INFO, 0);
        break;

    case TC_START_SYN:
        // The master does not wait for anything here: it initialises the
        // transfer state and starts the synchronisation phase.
        TcInitTransferData();
        sTxByte = SEND_NO_INFO;
        break;

    case TC_START_PAR:
        sTxByte = TcCommunicateBuffer(data);
        break;

    case TC_START_DON:
        TcProcessTradeMenu(data);
        break;

    default:
        TcProcessIdle(data);
        break;
    }
}

// ---------------------------------------------------------------------------
// Gen I block exchange (unchanged)
// ---------------------------------------------------------------------------

static void SetTx(u8 byte)
{
    sTxByte = byte;
}

static void StartBlock(bool8 synced)
{
    (void)synced;
    (void)GbConvert_BuildTradeBlock(sGen, sRoom, sBlock);
    sTxByte = 0xFE;

    sSendChunk = 0;
    sSendPos = 0;
    sSendOffset = GB_RN_PREAMBLE_SIZE;
    sSendSize = GB_RN_RANDOM_SIZE;
    sSendSynced = FALSE;

    sBulkStarted = FALSE;
    sBulkCount = 0;
    sRecvChunk = 0;
    sChunkPos = 0;
    sChunkPayload = GB_RN_RANDOM_SIZE;
    sChunkSynced = FALSE;

    sPartyReady = FALSE;
    sState = GB_STATE_BULK;
    sFrameCounter = 0;
}

static u8 NextTxByteGen1(void)
{
    if (sState != GB_STATE_BULK)
        return sTxByte;

    if (!sSendSynced)
        return 0xFD;

    if (sSendPos >= sSendSize)
    {
        if (sSendChunk + 1 >= 3)
            return 0xFD;

        sSendChunk++;
        switch (sSendChunk)
        {
        case 1:
            sSendOffset = GB_RN_BLOCK_SIZE + GB_PARTY_PREAMBLE_SIZE;
            sSendSize = PartyDataSize();
            break;
        case 2:
            sSendOffset = GB_RN_BLOCK_SIZE + GB_PARTY_PREAMBLE_SIZE + PartyDataSize()
                        + GB_PATCH_PREAMBLE_SIZE;
            sSendSize = GB_PATCH_DATA_SIZE;
            break;
        }
        sSendPos = 0;
        sSendSynced = FALSE;
        return 0xFD;
    }

    return sBlock[sSendOffset + sSendPos++];
}

static void BeginChunk(u8 chunk)
{
    u8 i;
    u8 preamble;
    u16 offset;

    switch (chunk)
    {
    case 0:
        preamble = GB_RN_PREAMBLE_SIZE;
        offset = 0;
        sChunkPayload = GB_RN_RANDOM_SIZE;
        break;
    case 1:
        preamble = GB_PARTY_PREAMBLE_SIZE;
        offset = GB_RN_BLOCK_SIZE;
        sChunkPayload = PartyDataSize();
        break;
    default:
        preamble = GB_PATCH_PREAMBLE_SIZE;
        offset = GB_RN_BLOCK_SIZE + GB_PARTY_PREAMBLE_SIZE + PartyDataSize();
        sChunkPayload = GB_PATCH_DATA_SIZE;
        break;
    }

    sRecvChunk = chunk;
    sChunkPos = 0;
    sBulkCount = offset;
    for (i = 0; i < preamble; i++)
        sBulk[sBulkCount++] = 0xFD;
    sChunkSynced = (preamble == 0);
}

static bool8 ParseBulk(void)
{
    bool8 ok = GbConvert_ParseTradeBlock(sGen, sRoom, sBulk, sBulkCount);

#if TIME_CAPSULE_DEBUG_LOG
    DebugPrintfLevel(MGBA_LOG_DEBUG, "TC bulk: parse=%u size=%u", ok, sBulkCount);
#endif

    sTxByte = 0xFE;
    if (!ok)
    {
        SetError(TIME_CAPSULE_ERR_SPECIES);
        return FALSE;
    }

    sPartyReady = TRUE;
    sResult = TIME_CAPSULE_OK;
    sStartState = TC_START_DON;
    sTradingState = TC_TRADE_NO_INFO;
    sTradeOfferIn = 0;
    sReceivedOnce = FALSE;
    sLastPeerAction = 0xFF;
    sSynTransmitted = 0;
    sState = GB_STATE_GEN2;
    sFrameCounter = 0;
    return TRUE;
}

static void ProcessBulkByte(u8 byte)
{
    if (!sBulkStarted)
    {
        if (byte != 0xFD)
            return;
        sBulkStarted = TRUE;
        BeginChunk(0);
    }

    if (!sChunkSynced)
    {
        if (byte == 0xFD)
            return;
        sChunkSynced = TRUE;
    }

    if (sBulkCount < ARRAY_COUNT(sBulk))
        sBulk[sBulkCount] = byte;
    sBulkCount++;
    sChunkPos++;

    if (sChunkPos >= sChunkPayload)
    {
        if (sRecvChunk + 1 >= 3)
            ParseBulk();
        else
            BeginChunk(sRecvChunk + 1);
    }
}

// ---------------------------------------------------------------------------
// State transitions and byte dispatch
// ---------------------------------------------------------------------------

static void SetError(u8 error)
{
    sResult = error;
    sState = GB_STATE_ERROR;
    sFrameCounter = 0;
}

static void ProcessByteInner(u8 byte)
{
    switch (sState)
    {
    case GB_STATE_CONNECT:
        // Gen I markers first: they never run the Gen II handshake.
        if (byte == 0x60)
        {
            sGen = TIME_CAPSULE_GEN_1;
            sResult = TIME_CAPSULE_OK;
            SetTx(0x60);
            sState = GB_STATE_GEN1_SYNC;
            sFrameCounter = 0;
        }
        else if (byte == 0xD4 || byte == 0xD5)
        {
            sGen = TIME_CAPSULE_GEN_1;
            sResult = TIME_CAPSULE_OK;
            sLinkEstablished = TRUE;
            SetTx(byte);
            sState = GB_STATE_GEN1_ROOM_WAIT;
            sFrameCounter = 0;
        }
        else if (byte == 0xDE)
        {
            SetError(TIME_CAPSULE_ERR_NO_LINK);
        }
        else if (byte == 0x61 || byte == 0x62 || byte == 0x71 || byte == 0x72
              || byte == 0xD1 || byte == 0xD2 || byte == 0x75
              || byte == END_TRADE_BYTE_GEN2)
        {
            // First unambiguous Gen II byte.  From now on the reference
            // state machine drives the link.
            sGen = TIME_CAPSULE_GEN_2;
            sResult = TIME_CAPSULE_OK;
            sState = GB_STATE_GEN2;
            sStartState = TC_START_UNK;
            sBufferCounter = 0;
            TcSetSizes();
            TcBuildOutBuffer();
            TcProcessGen2(byte);
            sFrameCounter = 0;
        }
        break;

    case GB_STATE_GEN2:
        TcProcessGen2(byte);
        break;

    case GB_STATE_GEN1_SYNC:
        if (byte == 0x60)
        {
            SetTx(0x60);
        }
        else if (byte == 0xD4 || byte == 0xD5)
        {
            u8 wanted = (sRoom == TIME_CAPSULE_ROOM_TRADE) ? 0xD4 : 0xD5;

            if (byte != wanted)
            {
                SetError(TIME_CAPSULE_ERR_WRONG_ROOM);
                break;
            }
            SetTx(byte);
            sLinkEstablished = TRUE;
            sState = GB_STATE_GEN1_ROOM_WAIT;
            sFrameCounter = 0;
        }
        else if (byte == 0xD6)
        {
            SetError(TIME_CAPSULE_ERR_NO_LINK);
        }
        else if (byte == 0xD0)
        {
            SetTx(0xFE);
        }
        break;

    case GB_STATE_GEN1_ROOM_WAIT:
        if (byte == 0x60)
        {
            SetTx(0x60);
        }
        else if (byte == 0xFD)
        {
            StartBlock(TRUE);
            ProcessBulkByte(byte);
        }
        break;

    case GB_STATE_BULK:
        if (!sSendSynced && byte == 0xFD)
            sSendSynced = TRUE;
        ProcessBulkByte(byte);
        break;

    case GB_STATE_EXIT:
        if (byte == sExitValue)
        {
            sFinished = TRUE;
            GbLink_Shutdown();
            sState = GB_STATE_DONE;
        }
        break;

    default:
        break;
    }
}

static void ProcessByte(u8 byte)
{
    u8 oldState = sState;
    u8 oldSub = sStartState;
    u16 oldBc = sBufferCounter;

    // SERIAL_NO_DATA_BYTE: the other side had nothing to send on this
    // exchange.  The reference only feeds non-0xFE bytes to the protocol, so
    // 0xFE never advances a table or a buffer.
    if (byte == 0xFE)
        return;

    sNoDataFrames = 0;
    ProcessByteInner(byte);

#if TIME_CAPSULE_DEBUG_LOG
    if (sState != oldState || sStartState != oldSub
     || ((sStartState == TC_START_ENT || sStartState == TC_START_STA) && sBufferCounter != oldBc))
        DebugPrintfLevel(MGBA_LOG_DEBUG, "TC rx=%02X state %u->%u sub %u->%u idx %u tx=%02X",
                         byte, oldState, sState, oldSub, sStartState, sBufferCounter, sTxByte);
#endif
}

// Waits (bounded by one scanline window) for the pending transfer.
static bool8 WaitReply(u8 *rx)
{
    u16 start = REG_VCOUNT;

    while (!GbLink_Poll(rx))
    {
        if ((u16)(REG_VCOUNT - start) >= GB_LINK_REPLY_LINES)
            return FALSE;
    }

    return TRUE;
}

// The other game has to keep answering.  When it stops sending bytes its
// console was turned off or the cable was pulled.
static void CheckPeerIsAlive(void)
{
    u16 timeout;

    switch (sState)
    {
    case GB_STATE_CONNECT:
        if (++sNoDataFrames > GB_LINK_ALIVE_TIMEOUT)
            SetError(TIME_CAPSULE_ERR_TIMEOUT);
        return;

    case GB_STATE_GEN2:
        // Between trades the GB player thinks at its menu; the GB keeps
        // answering 0xFE, so there is nothing to time out.
        if (sStartState == TC_START_DON)
            return;

        // During the initial Cable Club menus (the GB can save the game and
        // walk around) use the long timeout; the actual block exchange is
        // quick.
        timeout = (sStartState == TC_START_PAR) ? GB_LINK_ALIVE_TIMEOUT
                                                : GB_LINK_ALIVE_TIMEOUT_LONG;
        break;

    case GB_STATE_GEN1_SYNC:
    case GB_STATE_GEN1_ROOM_WAIT:
        timeout = GB_LINK_ALIVE_TIMEOUT_LONG;
        break;

    case GB_STATE_BULK:
        timeout = GB_LINK_ALIVE_TIMEOUT;
        break;

    default:
        return;
    }

    if (++sNoDataFrames > timeout)
        SetError(TIME_CAPSULE_ERR_LINK_LOST);
}

// ---------------------------------------------------------------------------
// Public interface
// ---------------------------------------------------------------------------

void GbProtocol_Init(u8 room, u8 forcedGen)
{
    GbProtocol_Shutdown();

    sState = GB_STATE_CONNECT;
    sGen = TIME_CAPSULE_GEN_NONE;
    sRoom = room;
    sForcedGen = forcedGen;
    sResult = GB_PROTO_RESULT_ONGOING;
    sActive = TRUE;
    sFinished = FALSE;
    sPartyReady = FALSE;
    sLinkEstablished = FALSE;
    sFrameCounter = 0;
    sBulkCount = 0;
    sBulkStarted = FALSE;
    sSendSynced = FALSE;
    sSendChunk = 0;
    sSendPos = 0;
    sSendSize = 0;
    sSendOffset = 0;
    sHasTradeCmd = FALSE;
    sPeerExited = FALSE;
    sNoDataFrames = 0;
    sSuccessDone = FALSE;
    sSuccessValue = 0;
    sSuccessPrepared = FALSE;
    sTradingState = TC_TRADE_NO_INFO;
    sTradeOfferOut = 0;
    sTradeOfferIn = 0;
    sReceivedOnce = FALSE;
    sLastPeerAction = 0xFF;
    sStartState = TC_START_UNK;
    sBufferCounter = 0;
    sSizesIndex = 0;
    sBasePos = 0;

    sEnterSend[0] = 0x01;
    sEnterSend[1] = 0x61;
    sEnterSend[2] = 0xD0 | room;
    sEnterSend[3] = 0x00;
    sEnterRecv[0] = 0x61;
    sEnterRecv[1] = 0xD0 | room;
    sEnterRecv[2] = 0x00;
    sEnterRecv[3] = 0x00;

    GbLink_Init();

    // Ask the GB to use the external clock, so the GBA stays the clock
    // master (Poke_Transporter_GB does the same: outData = 0x01).
    sTxByte = 0x01;
}

void GbProtocol_Shutdown(void)
{
    GbLink_Shutdown();
    sActive = FALSE;
    sLinkEstablished = FALSE;
    sState = GB_STATE_IDLE;
}

void GbProtocol_Update(void)
{
    u8 i;
    u8 rx;

    if (!sActive || sState == GB_STATE_DONE || sState == GB_STATE_IDLE)
        return;

    sFrameCounter++;

    // The trade is complete when the GB starts its next CheckLinkTimeout.
    // If it never comes back (for example the player turned the GB off right
    // after saving), release the GBA trade animation instead of hanging.
    if (sState == GB_STATE_GEN2 && sTradingState == TC_TRADE_HAVE_SUCCESS && !sSuccessDone)
    {
        if (++sSuccessWaitFrames > GB_SUCCESS_TIMEOUT)
        {
            sSuccessDone = TRUE;
            sReexchangeStarted = FALSE;
        }
    }

    if ((sState == GB_STATE_BULK) && sFrameCounter > GB_BULK_TIMEOUT)
        SetError(TIME_CAPSULE_ERR_TIMEOUT);

    CheckPeerIsAlive();

#if TIME_CAPSULE_DEBUG_LOG
    if ((sFrameCounter % 60) == 0)
    {
        GbLink_LogDebug();
        DebugPrintfLevel(MGBA_LOG_DEBUG, "TC proto: state=%u gen=%u sub=%u trade=%u tx=%02X bc=%u sz=%u off=%u",
                         sState, sGen, sStartState, sTradingState, sTxByte,
                         sBufferCounter,
                         (sSizesIndex < TC_MAX_CHUNKS) ? sSizes[sSizesIndex] : 0,
                         sTradeOfferOut);
    }
#endif

    // The master clocks itself; one byte per ~14 scanlines, sixteen per frame.
    for (i = 0; i < GB_LINK_BYTES_FRAME; i++)
    {
        if (!sActive)
            return;

        if (!GbLink_IsBusy())
        {
            GbLink_WaitLines(GB_LINK_PACE_LINES);
            GbLink_Start(sTxByte);
        }

        if (!WaitReply(&rx))
            return;

        ProcessByte(rx);
        sTxByte = (sState == GB_STATE_BULK) ? NextTxByteGen1() : sTxByte;
    }

    switch (sState)
    {
    case GB_STATE_CONNECT:
#if GB_CONNECT_TIMEOUT > 0
        if (sFrameCounter > GB_CONNECT_TIMEOUT)
            SetError(TIME_CAPSULE_ERR_NO_LINK);
#endif
        break;

    case GB_STATE_EXIT:
        if (sFrameCounter > GB_EXIT_TIMEOUT)
        {
            sFinished = TRUE;
            GbLink_Shutdown();
            sState = GB_STATE_DONE;
        }
        break;

    case GB_STATE_ERROR:
        if (sFrameCounter > 30)
        {
            sFinished = TRUE;
            GbLink_Shutdown();
            sState = GB_STATE_DONE;
        }
        break;

    default:
        break;
    }
}

bool8 GbProtocol_IsActive(void)
{
    return sActive;
}

bool8 GbProtocol_IsReady(void)
{
    return sPartyReady;
}

bool8 GbProtocol_IsLinkEstablished(void)
{
    return sLinkEstablished;
}

bool8 GbProtocol_IsError(void)
{
    return sResult != GB_PROTO_RESULT_ONGOING && sResult != TIME_CAPSULE_OK;
}

bool8 GbProtocol_IsFinished(void)
{
    return sFinished;
}

bool8 GbProtocol_IsExiting(void)
{
    return sState == GB_STATE_EXIT;
}

bool8 GbProtocol_PeerLeft(void)
{
    return sPeerExited;
}

u8 GbProtocol_GetResult(void)
{
    return sResult;
}

u8 GbProtocol_GetGeneration(void)
{
    return sGen;
}

bool8 GbProtocol_HasTradeCommand(void)
{
    return sHasTradeCmd;
}

u16 GbProtocol_GetTradeCommand(void)
{
    return sTradeCmd;
}

u16 GbProtocol_GetTradeArg(void)
{
    return sTradeArg;
}

void GbProtocol_ClearTradeCommand(void)
{
    sHasTradeCmd = FALSE;
}

void GbProtocol_SendTradeCommand(u16 cmd, u16 arg)
{
    u8 base = TradeBase();

    // A new command of ours restarts the peer debounce and the "space things
    // out" counter, exactly like try_to_offer()/try_to_accept_offer()/
    // try_to_success() in communicator.c.
    sReceivedOnce = FALSE;
    sLastPeerAction = 0xFF;
    sSynTransmitted = 0;

#if TIME_CAPSULE_DEBUG_LOG
    DebugPrintfLevel(MGBA_LOG_DEBUG, "TC trade: cmd=%u arg=%u state=%u", cmd, arg, sTradingState);
#endif

    switch (cmd)
    {
    case LINKCMD_READY_TO_TRADE:
        sTradeOfferOut = arg & 0xF;
        sTradingState = TC_TRADE_HAVE_OFFER;
        sTxByte = base + sTradeOfferOut;
        break;

    case LINKCMD_INIT_BLOCK:
        sTradeOfferOut = ACCEPT_VALUE;
        sTradingState = TC_TRADE_HAVE_ACCEPT;
        sTxByte = base + sTradeOfferOut;
        break;

    case LINKCMD_READY_CANCEL_TRADE:
    case LINKCMD_PLAYER_CANCEL_TRADE:
    case LINKCMD_PARTNER_CANCEL_TRADE:
        sTradeOfferOut = DECLINE_VALUE;
        sTradingState = TC_TRADE_HAVE_ACCEPT;
        sTxByte = base + sTradeOfferOut;
        break;

    case LINKCMD_REQUEST_CANCEL:
    case LINKCMD_BOTH_CANCEL_TRADE:
        sTradeOfferOut = CANCEL_VALUE;
        sTradingState = TC_TRADE_HAVE_OFFER;
        sExitValue = base + CANCEL_VALUE;
        sTxByte = sExitValue;
        sState = GB_STATE_EXIT;
        sFrameCounter = 0;
        break;

    case LINKCMD_START_TRADE:
        // The trade animation is about to start.  Capture the anti-cloning
        // nybble now: the reference computes it before swapping the mons
        // (prepare_gen23_success), while the GBA trade code swaps them when
        // the animation ends.  The byte itself is only presented after the
        // animation, like the GB's LinkTrade.
        if (!sSuccessPrepared)
        {
            sSuccessPrepared = TRUE;
            TcComputeSuccessValue();
        }
        break;

    case LINKCMD_READY_FINISH_TRADE:
        GbProtocol_StartSuccess();
        break;

    default:
        break;
    }
}

static void TcComputeSuccessValue(void)
{
    u16 playerSpecies = 0;
    u16 partnerSpecies = 0;
    u8 playerIndex = gSelectedTradeMonPositions[0];
    u8 partnerIndex = gSelectedTradeMonPositions[1] % PARTY_SIZE;
    u8 n = 0;

    if (playerIndex < PARTY_SIZE)
        playerSpecies = GetMonData(&gParties[B_TRAINER_PLAYER][playerIndex], MON_DATA_SPECIES);
    partnerSpecies = GetMonData(&gParties[B_TRAINER_OPPONENT_A][partnerIndex], MON_DATA_SPECIES);

    if (playerSpecies == SPECIES_MEW || partnerSpecies == SPECIES_MEW)
        n = 1;
    else if (playerSpecies == SPECIES_CELEBI || partnerSpecies == SPECIES_CELEBI)
        n = 2;

    sSuccessValue = n;
    sSuccessPrepared = TRUE;
}

void GbProtocol_StartSuccess(void)
{
    u8 base = TradeBase();

    if (!sSuccessPrepared)
        TcComputeSuccessValue();

    sSuccessDone = FALSE;
    sReexchangeStarted = FALSE;
    sSuccessWaitFrames = 0;
    sTradeOfferOut = sSuccessValue;
    sTradingState = TC_TRADE_HAVE_SUCCESS;
    sReceivedOnce = FALSE;
    sLastPeerAction = 0xFF;
    sSynTransmitted = 0;
    sTxByte = base | sSuccessValue;
}

bool8 GbProtocol_IsSuccessDone(void)
{
    return sSuccessDone;
}

void GbProtocol_StartReexchange(void)
{
    if (!sActive || sState == GB_STATE_DONE)
        return;

    if (sGen == TIME_CAPSULE_GEN_1)
    {
        // gen1_to_gen1 has no CheckLinkTimeout: after saving, the GB jumps
        // back to Gen2ToGen1LinkComms, sends 0x60 and then the 0xFD preamble
        // of the RNG block.  Wait for that preamble with the Gen I block
        // emitter.
        sState = GB_STATE_GEN1_ROOM_WAIT;
        sStartState = TC_START_UNK;
        sBufferCounter = 0;
        sPartyReady = FALSE;
        sSuccessDone = FALSE;
        sSuccessPrepared = FALSE;
        sReceivedOnce = FALSE;
        sLastPeerAction = 0xFF;
        sTxByte = SEND_NO_INFO;
        sFrameCounter = 0;
        sNoDataFrames = 0;
        return;
    }

    // The GB jumps straight from its save back into Gen2ToGen2LinkComms, so
    // this is a complete re-handshake, not just another block.  If the 0x75
    // that announced the re-exchange was already caught, keep the STA phase
    // (and answer it with 0x75); otherwise the reference restarts its master
    // routine with ENTER_TRADE_MASTER (0x01) and answers the 0x75 later.
    TcSetSizes();
    TcBuildOutBuffer();

    sSizesIndex = 0;
    sBasePos = 0;
    sHasTransmittedSyn = FALSE;
    sSynTransmitted = 0;
    sBufferCounterOut = 0;
    sTradingState = TC_TRADE_NO_INFO;
    sPartyReady = FALSE;
    sSuccessDone = FALSE;
    sSuccessPrepared = FALSE;
    sReceivedOnce = FALSE;
    sLastPeerAction = 0xFF;
    sState = GB_STATE_GEN2;

    if (sReexchangeStarted)
    {
        sStartState = TC_START_STA;
        sBufferCounter = 0;
        sTxByte = START_TRADE_BYTE_GEN2;
    }
    else
    {
        sStartState = TC_START_UNK;
        sBufferCounter = 0;
        sTxByte = 0x01;
    }
    sReexchangeStarted = FALSE;

    sFrameCounter = 0;
    sNoDataFrames = 0;
}

void GbProtocol_RequestExit(void)
{
    u8 base = TradeBase();

    sExitValue = base + CANCEL_VALUE;
    sTxByte = sExitValue;
    sState = GB_STATE_EXIT;
    sFrameCounter = 0;
}

void GbProtocol_PauseForSave(void)
{
    if (sState == GB_STATE_GEN2 && sStartState == TC_START_DON)
    {
        sTradingState = TC_TRADE_NO_INFO;
        sTxByte = SEND_NO_INFO;
        sHasTradeCmd = FALSE;
    }
}

