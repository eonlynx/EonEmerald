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
    TC_TRADE_WAIT_ACK,
    TC_TRADE_WAIT_CONFIRM,
    TC_TRADE_HAVE_SUCCESS,
};

// Gen I/II byte time: 8192 bps -> ~0.98 ms -> ~12 scanlines at 59.7 Hz.
// Pokemon-Gen3-to-Gen-X uses a VCOUNT wait of 900 us (~12 lines); this is the
// same effective byte rate.
#define GB_LINK_PACE_LINES    12
// Give up on this frame if the GB has not clocked back within this window;
// the transfer stays pending and is retried on the next frame.
#define GB_LINK_REPLY_LINES   10
// The GB must have armed its outgoing byte (SC external clock) before the
// master clocks it; otherwise the transfer is lost.  This is the same
// slave-ready check the reference performs on SI (SIO_RDY).
#define GB_LINK_READY_LINES   12
// 228 scanlines per frame; 14 exchanges at 12 lines each use 168 lines and
// leave ~60 lines (4 ms) for rendering, input and sound.  The reference runs
// one exchange per VCOUNT interrupt (~18/frame); doing that here would block
// the main loop for the whole frame, so 14 is the safe maximum.
#define GB_LINK_BYTES_FRAME   14

#define GB_LINK_ALIVE_TIMEOUT      3600 // 60 seconds, only while streaming data

#define GB_BULK_TIMEOUT       3600
#define GB_SUCCESS_TIMEOUT    3600
#define GB_EXIT_TIMEOUT       60
// Crystal's success WaitLinkTransfer does ten receive frames and ten
// acknowledge frames after it detects our byte.  Keep presenting the success
// byte for that long before starting the next CheckLinkTimeout, otherwise a
// 0x75 can overwrite its wOtherPlayerLinkAction (important for Mew/Celebi).
#define GB_SUCCESS_HOLD_FRAMES 20
// Non-0xFE bytes Crystal may send while it is still acknowledging the
// selection.  24 covers ten receive + ten acknowledge frames plus margin.
#define GB_SELECT_ACK_TIMEOUT 24
// Frames the error state stays on screen before shutting the link down.
#define GB_ERROR_DISPLAY_FRAMES 30

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
static EWRAM_DATA u8 sSynRequired;
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
// Each phase owns its own output value.  They are never shared: the party
// slot used during the selection must not be visible as a confirmation
// answer (0x70 is a valid slot and also a valid "accept" to Crystal).
static EWRAM_DATA u8 sSelectionOut;   // our party slot during selection
static EWRAM_DATA u8 sConfirmOut;     // ACCEPT_VALUE / DECLINE_VALUE
static EWRAM_DATA bool8 sConfirmOutValid;
static EWRAM_DATA bool8 sReceivedOnce;
static EWRAM_DATA u8 sLastPeerAction;
static EWRAM_DATA u16 sTradeCmd;
static EWRAM_DATA u16 sTradeArg;
static EWRAM_DATA bool8 sHasTradeCmd;
// The selection exchange has to finish before we may present an accept:
// Crystal records the last 0x7X byte it reads there as our party slot.
static EWRAM_DATA bool8 sAcceptPending;
// Crystal may confirm before the GBA player does; remember it, because the
// GB stops repeating 0x72 as soon as its own confirmation exchange ends.
static EWRAM_DATA bool8 sPeerConfirmed;
static EWRAM_DATA u8 sAckWaitFrames;
// Keep the selection offer on the wire for one more full frame after it has
// been detected.  Crystal's main loop samples hSerialReceive once per frame;
// the detection itself only proves that its offer arrived, not that it has
// already sampled ours.
static EWRAM_DATA u8 sSelectionHoldFrames;

static void TcClearConfirmOut(void)
{
    sConfirmOut = DECLINE_VALUE;
    sConfirmOutValid = FALSE;
}

static void TcSetConfirmOut(u8 value)
{
    sConfirmOut = value;
    sConfirmOutValid = TRUE;
}

static EWRAM_DATA u8 sSuccessValue; // anti-cloning nybble, not the full byte
static EWRAM_DATA bool8 sSuccessPrepared;
static EWRAM_DATA bool8 sSuccessDone;
static EWRAM_DATA u8 sSuccessHoldFrames;
static EWRAM_DATA bool8 sReexchangeStarted;
// When the 0x75 that starts the next CheckLinkTimeout arrives while the
// success phase was still active, Crystal's previous 0x00 acknowledge may
// still be in flight.  Require two action-5 bytes before accepting the 0x00
// that advances the start table to 0x76.
static EWRAM_DATA u8 sStartReexchangeGuard;
static EWRAM_DATA u16 sSuccessWaitFrames;
static EWRAM_DATA u8 sExitValue;

static void SetError(u8 error);
static bool8 TcParseReceivedBlock(void);
static void TcComputeSuccessValue(void);

static u8 TradeBase(void)
{
    return (sGen == TIME_CAPSULE_GEN_1) ? 0x60 : 0x70;
}

// Single source of truth for the byte the GBA presents in each trade-menu
// phase.  Keeping this derived from the phase (instead of reusing one
// "trade offer" variable) is what prevents the selection slot from being
// sent as a confirmation answer: 0x70 means "first Pokémon" while selecting
// and "accept" while confirming.
static u8 TcTradeTxByte(void)
{
    u8 base = TradeBase();

    if (sState == GB_STATE_EXIT)
        return sExitValue;

    switch (sTradingState)
    {
    case TC_TRADE_HAVE_OFFER:
        return base + sSelectionOut;
    case TC_TRADE_WAIT_ACK:
        return SEND_NO_INFO;
    case TC_TRADE_WAIT_CONFIRM:
    case TC_TRADE_HAVE_ACCEPT:
        return sConfirmOutValid ? base + sConfirmOut : SEND_NO_INFO;
    case TC_TRADE_HAVE_SUCCESS:
        return base | sSuccessValue;
    default:
        return SEND_NO_INFO;
    }
}

// Executable version of the receive table in the research document.  A byte
// is only interpreted by the phase that owns it; anything else is ignored.
static bool8 TcPhaseAccepts(u8 data)
{
    u8 base = TradeBase();

    switch (sTradingState)
    {
    case TC_TRADE_HAVE_OFFER:
        return (data >= base && data < base + GB_PARTY_SIZE)
            || data == base + CANCEL_VALUE;
    case TC_TRADE_WAIT_ACK:
        return data == 0x00;
    case TC_TRADE_WAIT_CONFIRM:
    case TC_TRADE_HAVE_ACCEPT:
        return data == base + DECLINE_VALUE || data == base + ACCEPT_VALUE;
    case TC_TRADE_HAVE_SUCCESS:
        return (data & 0xF0) == (base & 0xF0);
    default:
        return FALSE;
    }
}

// The wire byte must follow the trade phase on every transfer, not only when
// a non-0xFE byte arrives.  Otherwise a phase change with no further input
// leaves the previous phase's byte on the wire: Crystal's confirmation
// WaitLinkTransfer reads the last received byte and would accept a stale
// selection slot (0x70..0x75) as its answer.
static void TcRefreshTradeTxByte(void)
{
    u8 base = TradeBase();

    if (sState == GB_STATE_EXIT)
    {
        sTxByte = sExitValue;
        return;
    }

    if (sState != GB_STATE_GEN2 || sStartState != TC_START_DON)
        return;

    if (sSelectionHoldFrames
     && (sTradingState == TC_TRADE_HAVE_OFFER
      || sTradingState == TC_TRADE_WAIT_ACK))
    {
        sTxByte = base + sSelectionOut;
        return;
    }

    if (sTradingState == TC_TRADE_HAVE_OFFER
     && sSynTransmitted < MIN_WAIT_FOR_SYN)
        sTxByte = SEND_NO_INFO;
    else
        sTxByte = TcTradeTxByte();
}


// Any 0xD0..0xDF byte is a room-sync byte (Link_EnsureSync): the GB only
// checks the high nibble, and it uses 0xD0 for the Time Capsule room and 0xD4
// in EnterTimeCapsule.  Accepting the whole range keeps its sync loop alive
// without weakening the 0xD1/0xD2 room check for the trade rooms.
static bool8 IsRoomSyncByte(u8 data)
{
    return (data & 0xF0) == 0xD0;
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
    sReceivedOnce = FALSE;
    sLastPeerAction = 0xFF;
    sAcceptPending = FALSE;
    sPeerConfirmed = FALSE;
    sSelectionHoldFrames = 0;
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
    // The first chunk keeps the reference's three-byte minimum: the start
    // table and the START_TRADE_SYN state already consume the first two FD
    // bytes of Crystal's RN preamble.  The party chunk uses its real six-byte
    // preamble, so an idle 0x00 after a partial preamble cannot start the
    // next chunk early.  The patch chunk keeps the reference's three bytes.
    sSynTransmitted = 0;
    if (sSizesIndex == 0)
        sSynRequired = MIN_WAIT_FOR_SYN;
    else if (sSizesIndex == 1)
        sSynRequired = GB_PARTY_PREAMBLE_SIZE;
    else
        sSynRequired = GB_PATCH_PREAMBLE_SIZE;
    sBufferCounter = 0;
    sBufferCounterOut = 0;
}

static void TcInitTransferData(void)
{
    sBasePos = 0;
    sSizesIndex = 0;
    TcResetTransferBetweenSync();
    sStartState = TC_START_PAR;
    sNoDataFrames = 0;
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

        if (sSynTransmitted < sSynRequired)
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
#if TIME_CAPSULE_DEBUG_LOG
            DebugPrintfLevel(MGBA_LOG_DEBUG, "TC chunk done: next=%u base=%u rx=%02X syn=%u/%u",
                             sSizesIndex, sBasePos, data, sSynTransmitted, sSynRequired);
#endif

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

static void TcGetOffer(u8 data)
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
                    sPeerExited = TRUE;
                    sTradeCmd = LINKCMD_REQUEST_CANCEL;
                    sTradeArg = 0;
                    sHasTradeCmd = TRUE;
                    sLastPeerAction = data;
                    sExitValue = data;
                    sTxByte = data;
                    sState = GB_STATE_EXIT;
                    sFrameCounter = 0;
                    return;
                }
                TcReportTradeCommand(data, LINKCMD_READY_TO_TRADE, data - base);
                // The selection phase is over; its debounce byte must not
                // hide Crystal's later 0x72 confirmation (the same value as
                // its slot-2 offer).  Start the ack/confirm phase fresh.
                sLastPeerAction = 0xFF;
                sReceivedOnce = FALSE;
                // Gen II still has to acknowledge the selection (ten 0x00
                // frames).  Keep our party slot on the wire for one more full
                // frame so Crystal's main loop can sample it, then switch to
                // 0xFE: keeping it during the confirmation would be read as
                // an accept.  Gen I has no such phase.
                if (sGen == TIME_CAPSULE_GEN_2)
                {
                    sTradingState = TC_TRADE_WAIT_ACK;
                    sAckWaitFrames = 0;
                    sSelectionHoldFrames = 1;
                    TcClearConfirmOut();
                    return;
                }

                sTradingState = TC_TRADE_NO_INFO;
            }
        }
    }
}

static void TcGetAccept(u8 data)
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

                if (data == base + ACCEPT_VALUE
                 && sConfirmOutValid
                 && sConfirmOut == ACCEPT_VALUE)
                {
                    TcReportTradeCommand(data, LINKCMD_INIT_BLOCK, 0);
                }
                else
                {
                    // The GB declined, or we had already declined: keep the
                    // decline value so the GB cannot start a trade this side
                    // did not accept.
                    TcSetConfirmOut(DECLINE_VALUE);
                    TcReportTradeCommand(data, LINKCMD_READY_CANCEL_TRADE, 0);
                }

                sTradingState = TC_TRADE_NO_INFO;
            }
        }
    }
}

static void TcGetSuccess(u8 data)
{
    u8 base = TradeBase();

    if (sGen == TIME_CAPSULE_GEN_1)
    {
        // Gen I has no anti-cloning exchange.  After its animation it starts
        // the re-exchange with 0x60, which is the signal that we may evolve
        // and save.
        if ((data & 0xF0) == (base & 0xF0))
        {
            sSuccessDone = TRUE;
            sSuccessHoldFrames = 0;
            sReexchangeStarted = FALSE;
        }
        return;
    }

    // Both sides derive the anti-cloning nybble from the same two traded
    // mons, so only our exact success byte completes the handshake.  This is
    // deliberately stricter than the reference's high-nibble test: after the
    // confirmation exchange Crystal sends 0x72 (accept), and that stale byte
    // must not be mistaken for the post-animation success exchange.
    if (data == (base | sSuccessValue))
    {
        if (!sReceivedOnce)
        {
            sReceivedOnce = TRUE;
        }
        else
        {
            // Crystal still has ten receive and ten acknowledge frames to
            // run.  Do not let the caller switch the wire to 0x75 until they
            // are over; wOtherPlayerLinkAction would be overwritten.
            sSuccessDone = TRUE;
            sSuccessHoldFrames = GB_SUCCESS_HOLD_FRAMES;
#if TIME_CAPSULE_DEBUG_LOG
            DebugPrintfLevel(MGBA_LOG_DEBUG, "TC post: success seen value=%u hold=%u",
                             sSuccessValue, sSuccessHoldFrames);
#endif
        }
    }
}

static void TcProcessTradeMenu(u8 data)
{
    u8 base = TradeBase();

    // A restarted link (the GB went back to the Cable Club receptionist)
    // presents 0x61/0x62 / the room byte again.  0x71/0x72 are ambiguous with
    // offers/accepts, so only 0x61, 0x62 and the 0xD0 room byte restart here.
    if (sGen == TIME_CAPSULE_GEN_2 && (data == 0x61 || data == 0x62))
    {
        sStartState = TC_START_ENT;
        sBufferCounter = 0;
        sPartyReady = FALSE;
        sTradingState = TC_TRADE_NO_INFO;
        sAcceptPending = FALSE;
        sPeerConfirmed = FALSE;
        sSelectionHoldFrames = 0;
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
        sTradingState = TC_TRADE_NO_INFO;
        sAcceptPending = FALSE;
        sPeerConfirmed = FALSE;
        sSelectionHoldFrames = 0;
        sTxByte = data;
        return;
    }

    // The other game left the trade room.  This can arrive while the menu is
    // idle (NO_INFO), not only during a selection, so it has to be handled
    // here as well as in TcGetOffer.  Without this the GBA keeps the menu open
    // and later waits forever for an echo after its own exit.
    if (data == base + CANCEL_VALUE)
    {
        sPeerExited = TRUE;
        sExitValue = data;
        sTxByte = data;
        sState = GB_STATE_EXIT;
        sFrameCounter = 0;
        return;
    }

    // After the trade the GB saves and jumps straight back into
    // Gen2ToGen2LinkComms, i.e. into a new CheckLinkTimeout_Gen2.  A 0x75
    // while we are presenting the anti-cloning byte can only be that
    // re-handshake (the success byte itself is 0x70..0x72).  This is the
    // fallback for a success exchange we did not observe (for example because
    // it happened while the trade animation was still using the accept byte).
    if (sTradingState == TC_TRADE_HAVE_SUCCESS && data == START_TRADE_BYTE_GEN2)
    {
        sSuccessDone = TRUE;
        sSuccessHoldFrames = 0;
        sReexchangeStarted = TRUE;
        sStartState = TC_START_STA;
        sBufferCounter = 0;
        sStartReexchangeGuard = 1;
        sTradingState = TC_TRADE_NO_INFO;
        sTxByte = START_TRADE_BYTE_GEN2;
#if TIME_CAPSULE_DEBUG_LOG
        DebugPrintfLevel(MGBA_LOG_DEBUG, "TC post: 0x75 in success -> guarded start table");
#endif
        return;
    }

    // The byte clocked in this exchange belongs to the phase that was active
    // before the handlers below run.  The handlers only change the phase;
    // the next exchange automatically presents the new phase's byte.  This
    // is the same one-byte delay as communicator.c and it means a value can
    // never be interpreted under the wrong phase.
    if (sTradingState == TC_TRADE_HAVE_OFFER
     && sSynTransmitted < MIN_WAIT_FOR_SYN)
        sTxByte = SEND_NO_INFO;
    else
        sTxByte = TcTradeTxByte();

    // Crystal acknowledges the selection with ten 0x00 frames.  Our offer
    // was presented until this phase; from now on TcTradeTxByte() returns
    // 0xFE, so the selection slot cannot leak into Crystal's confirmation.
    if (sTradingState == TC_TRADE_WAIT_ACK)
    {
        bool8 ackOver = FALSE;

        // Only 0x00 is meaningful in this phase: it is Crystal's selection
        // acknowledge.  0x7X bytes here are its repeated selection offer and
        // must not be interpreted as a confirmation (0x72 is also slot 2).
        if (data == 0x00)
        {
            ackOver = TRUE;
        }
        else if (++sAckWaitFrames > GB_SELECT_ACK_TIMEOUT)
        {
            // The 0x00 ack was missed (or the GB is unusually slow).  Move
            // on; a confirmation that is already repeating will be picked up
            // by the WAIT_CONFIRM handler on the next byte.
            ackOver = TRUE;
        }

        if (ackOver)
        {
            sTradingState = TC_TRADE_WAIT_CONFIRM;
            sLastPeerAction = 0xFF;
            sReceivedOnce = FALSE;

            if (sAcceptPending)
            {
                sAcceptPending = FALSE;
                TcSetConfirmOut(ACCEPT_VALUE);
                sTradingState = TC_TRADE_HAVE_ACCEPT;

                if (sPeerConfirmed)
                    TcReportTradeCommand(base + ACCEPT_VALUE, LINKCMD_INIT_BLOCK, 0);
            }
        }
        return;
    }

    if (sTradingState == TC_TRADE_WAIT_CONFIRM)
    {
        // Crystal's cancel must win over a simultaneous local accept: if we
        // answered 0x72, Crystal's WaitLinkTransfer would read it as "yes"
        // even though its player pressed NO.
        if (data == base + DECLINE_VALUE)
        {
            // Crystal's player canceled its confirmation.  Answer with the
            // same decline so its WaitLinkTransfer exits.
            TcReportTradeCommand(data, LINKCMD_READY_CANCEL_TRADE, 0);
            TcSetConfirmOut(DECLINE_VALUE);
            sAcceptPending = FALSE;
            sTradingState = TC_TRADE_HAVE_ACCEPT;
            return;
        }

        if (sAcceptPending)
        {
            sAcceptPending = FALSE;
            TcSetConfirmOut(ACCEPT_VALUE);
            sTradingState = TC_TRADE_HAVE_ACCEPT;

            if (sPeerConfirmed)
                TcReportTradeCommand(base + ACCEPT_VALUE, LINKCMD_INIT_BLOCK, 0);
            return;
        }

        if (data == base + ACCEPT_VALUE)
            sPeerConfirmed = TRUE;

        // 0xFE is ignored by WaitLinkTransfer; Crystal keeps waiting for this
        // player's answer instead of starting a trade on its own.
        return;
    }

    if (sSynTransmitted < MIN_WAIT_FOR_SYN)
    {
        sSynTransmitted++;
        return;
    }

    // Phase-scoped receive: the table above is the contract.
    if (!TcPhaseAccepts(data))
        return;

    switch (sTradingState)
    {
    case TC_TRADE_HAVE_OFFER:
        TcGetOffer(data);
        break;
    case TC_TRADE_HAVE_ACCEPT:
        TcGetAccept(data);
        break;
    case TC_TRADE_HAVE_SUCCESS:
        TcGetSuccess(data);
        break;
    default:
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

    if (IsRoomSyncByte(data))
    {
        // Link_EnsureSync completed before we saw the receptionist byte.
        // 0xD1/0xD2 select the trade room and must match the one chosen on
        // the GBA; 0xD0/0xD4 are generic sync retries and are answered with
        // our room byte without confirming the link yet.
        if ((data == 0xD1 || data == 0xD2) && data != (0xD0 | sRoom))
        {
            SetError(TIME_CAPSULE_ERR_WRONG_ROOM);
            return;
        }

        if (data == 0xD1 || data == 0xD2)
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
        if (sLinkEstablished && data == START_TRADE_BYTE_GEN2)
        {
            // The GB already finished the room sync and moved on to
            // CheckLinkTimeout_Gen2 (0x75) while a late 0xD0/0xD4 retry had
            // put us back in the enter table.  0x75 is unambiguous at this
            // point: answer it and enter the start-trade table instead of
            // waiting forever for the two 0x00 acks.
            sBufferCounter = 0;
            sStartState = TC_START_STA;
            sTxByte = sStartSend[0];
            break;
        }

        if (sBufferCounter == 0 && IsRoomSyncByte(data))
        {
            // Link_EnsureSync arrived before the repeating receptionist byte
            // was seen a second time.  Jump straight to the room ack step.
            if ((data == 0xD1 || data == 0xD2) && data != (0xD0 | sRoom))
            {
                SetError(TIME_CAPSULE_ERR_WRONG_ROOM);
                break;
            }

            if (data == 0xD1 || data == 0xD2)
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
        else if (sBufferCounter == 1 && IsRoomSyncByte(data))
        {
            // The GB's Link_EnsureSync may present 0xD0 (Time Capsule room)
            // or 0xD4 (EnterTimeCapsule) here.  Link_EnsureSync only checks
            // the high nibble, so accept any 0xD0..0xDF as our room byte.
            if ((data == 0xD1 || data == 0xD2) && data != (0xD0 | sRoom))
            {
                SetError(TIME_CAPSULE_ERR_WRONG_ROOM);
                break;
            }

            if (data == 0xD1 || data == 0xD2)
                sLinkEstablished = TRUE;

            data = 0xD0 | sRoom;
        }

        sTxByte = TcCheckIfContinue(data, sEnterSend, sEnterRecv, 4,
                                    TC_START_WAI, sStartSend[0], 0);
        break;

    case TC_START_STA:
        if (sStartReexchangeGuard)
        {
            // Entered from the success fallback.  Crystal's previous 0x00
            // acknowledge can still be in flight, so do not treat the first
            // 0x00 as the action-5 acknowledgement.  Wait for the action-5
            // bytes to repeat (or for an already-started action 6) first.
            if (data == 0x76)
            {
                sStartReexchangeGuard = 0;
                sBufferCounter = 2;
                sTxByte = 0x76;
                break;
            }

            if (data == START_TRADE_BYTE_GEN2)
            {
                if (sStartReexchangeGuard < 2)
                    sStartReexchangeGuard++;
                sTxByte = START_TRADE_BYTE_GEN2;
                break;
            }

            if (data == 0x00 && sStartReexchangeGuard >= 2)
            {
                // This is the real action-5 acknowledge.  Count the action-5
                // byte we already saw and let the table advance to 0x76.
                sStartReexchangeGuard = 0;
                sBufferCounter = 1;
            }
            else
            {
                sTxByte = START_TRADE_BYTE_GEN2;
                break;
            }
        }

        if (sBufferCounter < 2 && IsRoomSyncByte(data))
        {
            // The GB retried Link_EnsureSync (0xD0/0xD4) before offering
            // its start-trade byte.  Answer with the room byte; switching to
            // 0x75 here would make its sync loop fail.
            if ((data == 0xD1 || data == 0xD2) && data != (0xD0 | sRoom))
            {
                SetError(TIME_CAPSULE_ERR_WRONG_ROOM);
                break;
            }

            sBufferCounter = 2;
            sStartState = TC_START_ENT;
            sTxByte = sEnterSend[2];
            break;
        }

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
    sNoDataFrames = 0;
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
    sReceivedOnce = FALSE;
    sLastPeerAction = 0xFF;
    sAcceptPending = FALSE;
    sPeerConfirmed = FALSE;
    sSelectionHoldFrames = 0;
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
        if (byte == sExitValue
         || (sFrameCounter > 10 && (byte == 0x00 || byte == 0x01 || byte == 0x02)))
        {
            // The peer echoed our exit byte, acknowledged it with 0x00, or
            // has already gone back to the Cable Club receptionist and is
            // probing with 0x01/0x02.  Any of these means the link session is
            // over; do not wait for a 0x7F that will never arrive.
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

#if TIME_CAPSULE_DEBUG_LOG
    {
        static u16 logCount;
        if (logCount < 256 || (++logCount & 0x7) == 0)
        {
            DebugPrintfLevel(MGBA_LOG_DEBUG,
                             "TC data: rx=%02X state=%u sub=%u bc=%u tx=%02X",
                             byte, sState, sStartState, sBufferCounter, sTxByte);
            logCount++;
        }
    }
#endif

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
        // The GB is at the Cable Club receptionist: it may still be
        // discovering the link, saving the game or waiting for its player.
        // The reference master routine has no timeout here; the GB's own
        // link timeout is the one that decides.  Cancelling with B is always
        // possible on the GBA side.
        return;

    case GB_STATE_GEN2:
        // Between trades the GB player thinks at its menu; the GB keeps
        // answering 0xFE, so there is nothing to time out.
        if (sStartState == TC_START_DON)
            return;

        // The room sync, the start-trade handshake and the wait for the GB
        // player to press A at the console are controlled by the other game.
        // pokecrystal itself keeps retrying (CheckLinkTimeout_Gen2) and only
        // reports a timeout after its own frames counter expires, so the GBA
        // must not give up first.  Only the actual block exchange, where both
        // sides are streaming data, has a bounded timeout.
        if (sStartState == TC_START_PAR)
            timeout = GB_LINK_ALIVE_TIMEOUT;
        else
            return;
        break;

    case GB_STATE_GEN1_SYNC:
    case GB_STATE_GEN1_ROOM_WAIT:
        // Same as above: wait for the Gen I/II player, never time out.
        return;

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

void GbProtocol_Init(u8 room)
{
    GbProtocol_Shutdown();

    sState = GB_STATE_CONNECT;
    sGen = TIME_CAPSULE_GEN_NONE;
    sRoom = room;
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
    sSuccessHoldFrames = 0;
    sStartReexchangeGuard = 0;
    sSuccessValue = 0;
    sSuccessPrepared = FALSE;
    sTradingState = TC_TRADE_NO_INFO;
    sSelectionOut = 0;
    TcClearConfirmOut();
    sReceivedOnce = FALSE;
    sLastPeerAction = 0xFF;
    sAcceptPending = FALSE;
    sPeerConfirmed = FALSE;
    sSelectionHoldFrames = 0;
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

#if TIME_CAPSULE_DEBUG_LOG
    DebugPrintfLevel(MGBA_LOG_DEBUG, "TC build: posttrade-2 %s %s", __DATE__, __TIME__);
#endif

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

    // Hold the success byte for the rest of Crystal's receive/acknowledge
    // frames before the caller starts the next CheckLinkTimeout.
    if (sSuccessHoldFrames)
        sSuccessHoldFrames--;

    // The trade is complete when the GB starts its next CheckLinkTimeout.
    // If it never comes back (for example the player turned the GB off right
    // after saving), release the GBA trade animation instead of hanging.
    if (sState == GB_STATE_GEN2 && sTradingState == TC_TRADE_HAVE_SUCCESS && !sSuccessDone)
    {
        if (++sSuccessWaitFrames > GB_SUCCESS_TIMEOUT)
        {
            sSuccessDone = TRUE;
            sSuccessHoldFrames = 0;
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
        DebugPrintfLevel(MGBA_LOG_DEBUG, "TC proto: state=%u gen=%u sub=%u trade=%u tx=%02X bc=%u sz=%u sel=%u conf=%u/%u",
                         sState, sGen, sStartState, sTradingState, sTxByte,
                         sBufferCounter,
                         (sSizesIndex < TC_MAX_CHUNKS) ? sSizes[sSizesIndex] : 0,
                         sSelectionOut, sConfirmOutValid, sConfirmOut);
    }
#endif

    // One full frame of selection hold: the offer stays on the wire for the
    // rest of the current frame, then the refresh switches to the new phase.
    if (sSelectionHoldFrames)
        sSelectionHoldFrames--;

    // The master clocks itself; one byte per ~14 scanlines, sixteen per frame.
    for (i = 0; i < GB_LINK_BYTES_FRAME; i++)
    {
        if (!sActive)
            return;

        if (!GbLink_IsBusy())
        {
            // Do not clock an unarmed slave.  This is what keeps the GBA in
            // step with the GB's main loop: the GB only arms SB after its
            // serial interrupt has stored the previous byte, so clocking
            // blindly would let us run many bytes ahead and lose the answer.
            if (!GbLink_WaitPeerReady(GB_LINK_READY_LINES))
                break;

            // Re-derive the trade-menu byte from the current phase on every
            // transfer.  This is what prevents a stale selection/accept byte
            // from remaining on the wire while the other side is silent.
            TcRefreshTradeTxByte();

            GbLink_WaitLines(GB_LINK_PACE_LINES);
            GbLink_Start(sTxByte);
        }

        if (!WaitReply(&rx))
            break;

        ProcessByte(rx);
        sTxByte = (sState == GB_STATE_BULK) ? NextTxByteGen1() : sTxByte;
    }

    switch (sState)
    {
    case GB_STATE_CONNECT:
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
        if (sFrameCounter > GB_ERROR_DISPLAY_FRAMES)
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

#if TIME_CAPSULE_DEBUG_LOG
    DebugPrintfLevel(MGBA_LOG_DEBUG, "TC trade: cmd=%u arg=%u state=%u", cmd, arg, sTradingState);
#endif

    switch (cmd)
    {
    case LINKCMD_READY_TO_TRADE:
        // Start of the selection exchange.  A new offer restarts the peer
        // debounce and the "space things out" counter, exactly like
        // try_to_offer() in communicator.c.  Only the selection output is
        // set here; the confirmation output is explicitly invalid.
        sReceivedOnce = FALSE;
        sLastPeerAction = 0xFF;
        sSynTransmitted = 0;
        sAcceptPending = FALSE;
        sPeerConfirmed = FALSE;
        sSelectionHoldFrames = 0;
        sSelectionOut = arg & 0xF;
        TcClearConfirmOut();
        sTradingState = TC_TRADE_HAVE_OFFER;
        break;

    case LINKCMD_INIT_BLOCK:
        // Our player accepted.  The selection exchange may still be running
        // (HAVE_OFFER) or Crystal may still be acknowledging it (WAIT_ACK).
        // Keep the selection phase on the wire until it finishes; 0x72 is
        // presented afterwards.  Sending it early would make Crystal record
        // 0x72 (accept) as our party slot.
        if (sTradingState == TC_TRADE_HAVE_OFFER
         || sTradingState == TC_TRADE_WAIT_ACK
         || sTradingState == TC_TRADE_WAIT_CONFIRM)
        {
            sAcceptPending = TRUE;
            break;
        }

        sReceivedOnce = FALSE;
        sLastPeerAction = 0xFF;
        sSynTransmitted = 0;
        TcSetConfirmOut(ACCEPT_VALUE);
        sTradingState = TC_TRADE_HAVE_ACCEPT;

        if (sPeerConfirmed)
        {
            // Crystal already confirmed; the GB may have stopped repeating
            // 0x72, so tell the menu immediately.
            TcReportTradeCommand(base + ACCEPT_VALUE, LINKCMD_INIT_BLOCK, 0);
        }
        break;

    case LINKCMD_READY_CANCEL_TRADE:
    case LINKCMD_PLAYER_CANCEL_TRADE:
    case LINKCMD_PARTNER_CANCEL_TRADE:
        sReceivedOnce = FALSE;
        sLastPeerAction = 0xFF;
        sSynTransmitted = 0;
        sAcceptPending = FALSE;
        sPeerConfirmed = FALSE;
        sSelectionHoldFrames = 0;
        TcSetConfirmOut(DECLINE_VALUE);
        sTradingState = TC_TRADE_HAVE_ACCEPT;
        break;

    case LINKCMD_REQUEST_CANCEL:
    case LINKCMD_BOTH_CANCEL_TRADE:
        sReceivedOnce = FALSE;
        sLastPeerAction = 0xFF;
        sSynTransmitted = 0;
        sAcceptPending = FALSE;
        sPeerConfirmed = FALSE;
        sSelectionHoldFrames = 0;
        TcClearConfirmOut();
        sTradingState = TC_TRADE_HAVE_OFFER;
        sExitValue = base + CANCEL_VALUE;
        sState = GB_STATE_EXIT;
        sFrameCounter = 0;
        break;

    case LINKCMD_START_TRADE:
        // The success nybble is prepared when the trade menu has stored the
        // final selections (CB_WaitToStartTrade), not here: this command is
        // queued as soon as both players confirm, before the menu writes
        // gSelectedTradeMonPositions.  Preparing it from stale positions
        // would send 0x70 for a Mew/Celebi trade.
        sSuccessPrepared = FALSE;
        sAcceptPending = FALSE;
        sPeerConfirmed = FALSE;
        sSelectionHoldFrames = 0;
        break;

    case LINKCMD_READY_FINISH_TRADE:
        GbProtocol_StartSuccess();
        break;

    default:
        // SET_MONS_TO_TRADE and the other menu-only commands have no Gen I/II
        // wire representation.  They must not reset the peer debounce (a
        // 0x72 offer followed by a 0x72 confirm is a legitimate sequence) or
        // clear a confirmation that is already in progress.
        break;
    }

    // The wire byte is always derived from the phase that is now active.
    sTxByte = TcTradeTxByte();

#if TIME_CAPSULE_DEBUG_LOG
    DebugPrintfLevel(MGBA_LOG_DEBUG, "TC trade: -> state=%u tx=%02X acceptPending=%u peerConfirmed=%u",
                     sTradingState, sTxByte, sAcceptPending, sPeerConfirmed);
#endif
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

    // The protocol may already have entered this phase at the start of the
    // trade animation, the 0x75 fallback may already have started the
    // re-exchange, or the next block stream may even be running (if Crystal
    // finished its save before our animation ended).  Do not wipe the
    // sightings / start table just because the animation callback calls us
    // again on the next frame.
    if (sStartState != TC_START_DON
     || sTradingState == TC_TRADE_HAVE_SUCCESS
     || sReexchangeStarted)
    {
        if (sStartState == TC_START_DON && sTradingState == TC_TRADE_HAVE_SUCCESS)
            sTxByte = base | sSuccessValue;
        return;
    }

    sSuccessDone = FALSE;
    sSuccessHoldFrames = 0;
    sReexchangeStarted = FALSE;
    sStartReexchangeGuard = 0;
    sSuccessWaitFrames = 0;
    sTradingState = TC_TRADE_HAVE_SUCCESS;
    sReceivedOnce = FALSE;
    sLastPeerAction = 0xFF;
    sAcceptPending = FALSE;
    sPeerConfirmed = FALSE;
    sSelectionHoldFrames = 0;
    TcClearConfirmOut();
    sSynTransmitted = 0;
    sTxByte = base | sSuccessValue;
#if TIME_CAPSULE_DEBUG_LOG
    DebugPrintfLevel(MGBA_LOG_DEBUG, "TC post: success start value=%u tx=%02X", sSuccessValue, sTxByte);
#endif
}

bool8 GbProtocol_IsSuccessDone(void)
{
    return sSuccessDone && sSuccessHoldFrames == 0;
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
        sAcceptPending = FALSE;
        sPeerConfirmed = FALSE;
        sSelectionHoldFrames = 0;
        sTxByte = SEND_NO_INFO;
        sFrameCounter = 0;
        sNoDataFrames = 0;
        return;
    }

    // Crystal jumps straight from its save back into Gen2ToGen2LinkComms, i.e.
    // into CheckLinkTimeout_Gen2 and then the next block exchange.  Present
    // 0x75 immediately: Crystal's action-5 timeout is only a few frames, and
    // it ignores anything that is not 0x7X while it waits.
    //
    // Drop any half-finished SIO transfer (the reference stops and restarts
    // its transfer at this point).  A stale armed byte from the previous
    // block would otherwise be counted as the first byte of the next one.
    GbLink_Shutdown();
    GbLink_Init();

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
    sSuccessHoldFrames = 0;
    sSuccessPrepared = FALSE;
    sReceivedOnce = FALSE;
    sLastPeerAction = 0xFF;
    sAcceptPending = FALSE;
    sPeerConfirmed = FALSE;
    sSelectionHoldFrames = 0;
    sState = GB_STATE_GEN2;

    if (!sReexchangeStarted)
        sStartReexchangeGuard = 0;
    sStartState = TC_START_STA;
    sBufferCounter = 0;
    sTxByte = START_TRADE_BYTE_GEN2;
    sReexchangeStarted = FALSE;

#if TIME_CAPSULE_DEBUG_LOG
    DebugPrintfLevel(MGBA_LOG_DEBUG, "TC post: reexchange start guard=%u tx=%02X",
                     sStartReexchangeGuard, sTxByte);
#endif

    sFrameCounter = 0;
    sNoDataFrames = 0;
}

// Rebuild the outgoing party block after a local evolution.  The re-exchange
// handshake may already be running; only rebuild while the block stream has
// not started (STA/UNK), otherwise the buffer is in use.
void GbProtocol_RebuildOutBufferIfIdle(void)
{
    if (!sActive || sState != GB_STATE_GEN2)
        return;

    if (sStartState == TC_START_PAR)
        return;

    TcSetSizes();
    TcBuildOutBuffer();
}

void GbProtocol_RequestExit(void)
{
    u8 base = TradeBase();

    sExitValue = base + CANCEL_VALUE;
    sTxByte = sExitValue;
    sState = GB_STATE_EXIT;
    sFrameCounter = 0;
}


