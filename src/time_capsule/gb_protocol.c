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
// This is a port of Pokemon-Gen3-to-GenX's `process_data_arrived_gen2` and
// `communicate_buffer` (source/communicator.c), with the byte pacing and
// discovery of Poke_Transporter_GB (source/link_handler.cpp):
//
//  * the GBA is the clock master and performs one exchange per ~14 scanlines,
//    i.e. the Gen I/II 8192 bps byte time, ~16 bytes per frame.  Doing a
//    single exchange per frame does not work: the GB only keeps its outgoing
//    byte in SB for a fraction of a frame, so a 60 Hz master keeps reading
//    SERIAL_NO_DATA_BYTE (0xFE) during the 0x75/0x76 handshake;
//  * 0xFE means "the other side had nothing to send" and is ignored by every
//    state (Gen3-to-GenX: `if (data != SEND_NO_INFO)`);
//  * the room sequence is 0x61 -> 0xD1 -> 0x00, then the trade start
//    sequence is 0x75 until the peer answers 0x75, then 0x76 until it answers
//    0x76.  The peer keeps repeating each byte, so echoing it is enough;
//  * only after 0x76 does the party block start.  It is exchanged in the same
//    four chunks the GB uses, and the first three are preceded by a preamble
//    byte (0xFD) which the GB clocks but does not store.

enum
{
    GB_STATE_IDLE = 0,
    GB_STATE_CONNECT,
    GB_STATE_GEN1_SYNC,
    GB_STATE_GEN2_ENTER,
    GB_STATE_ROOM_WAIT,
    GB_STATE_GEN1_ROOM_WAIT,
    GB_STATE_BULK,
    GB_STATE_TRADE,
    GB_STATE_SUCCESS_WAIT,
    GB_STATE_SUCCESS,
    GB_STATE_EXIT,
    GB_STATE_ERROR,
    GB_STATE_DONE,
    GB_STATE_BLOCK_WAIT,
};

enum
{
    GB_PHASE_SELECT = 0,
    GB_PHASE_CONFIRM,
};

// Gen I/II byte time: 8192 bps -> ~1 ms -> 14 scanlines at 59.7 Hz.
#define GB_LINK_PACE_LINES    14
// Give up on this frame if the GB has not clocked back within this window;
// the transfer stays pending and is retried on the next frame.
#define GB_LINK_REPLY_LINES   10
// 228 scanlines per frame / 14 = 16 bytes; 10 leaves room for the rest of the
// frame (rendering, input) and for slow GB replies.
#define GB_LINK_BYTES_FRAME   10

// The Cable Club waits until the player presses B; do the same.
#define GB_CONNECT_TIMEOUT    0
#define GB_BULK_TIMEOUT       3600
#define GB_SUCCESS_TIMEOUT    600
#define GB_EXIT_TIMEOUT       180

static EWRAM_DATA u8 sState;
static EWRAM_DATA u8 sGen;
static EWRAM_DATA u8 sRoom;
static EWRAM_DATA u8 sForcedGen;
static EWRAM_DATA u8 sResult;
static EWRAM_DATA bool8 sActive;
static EWRAM_DATA bool8 sFinished;
static EWRAM_DATA bool8 sPartyReady;
static EWRAM_DATA bool8 sLinkEstablished;

static EWRAM_DATA u16 sFrameCounter;

static EWRAM_DATA u8 sBlock[GB_BLOCK_TRADE_GEN2];
static EWRAM_DATA u8 sBulk[GB_BLOCK_TRADE_GEN2];
static EWRAM_DATA u16 sBulkCount;

// Outgoing stream: it is emitted chunk by chunk, exactly like the GB's
// Serial_ExchangeBytes.  Each chunk starts with a sync phase (we keep sending
// the preamble until the GB sends one), then its bytes are streamed.
static EWRAM_DATA bool8 sSendSynced;
static EWRAM_DATA u8 sSendChunk;
static EWRAM_DATA u16 sSendPos;
static EWRAM_DATA u16 sSendSize;
static EWRAM_DATA u16 sSendOffset;
static EWRAM_DATA u8 sTxByte;

// Incoming stream: the block arrives in chunks, the first three with a
// preamble byte the sender clocks but does not store.
static EWRAM_DATA bool8 sBulkStarted;
static EWRAM_DATA u8 sRecvChunk;
static EWRAM_DATA u16 sChunkPos;
static EWRAM_DATA u16 sChunkPayload;
static EWRAM_DATA bool8 sChunkSynced;

// Start-trade phase: which of 0x75/0x76 the peer last showed us.
static EWRAM_DATA u8 sRoomPhase;

static EWRAM_DATA u16 sTradeCmd;
static EWRAM_DATA u16 sTradeArg;
static EWRAM_DATA bool8 sHasTradeCmd;
static EWRAM_DATA u8 sPhase;

static EWRAM_DATA u8 sSuccessValue;
static EWRAM_DATA u8 sPeerSuccessValue;
static EWRAM_DATA bool8 sSuccessDone;
static EWRAM_DATA u8 sExitValue;

static u8 TradeBase(void)
{
    return (sGen == TIME_CAPSULE_GEN_1) ? 0x60 : 0x70;
}

static u16 PartyDataSize(void)
{
    return (sGen == TIME_CAPSULE_GEN_1) ? GB_PARTY_DATA_GEN1 : GB_PARTY_DATA_GEN2;
}

static u8 NumberOfChunks(void)
{
    if (sGen == TIME_CAPSULE_GEN_2 && sRoom == TIME_CAPSULE_ROOM_TRADE)
        return 4;
    return 3;
}

// The party block is exchanged in the same chunks the GB uses.  The first
// three come from Serial_ExchangeBytes and are preceded by "preamble" bytes
// that the receiver clocks but does not store; mail is sent with plain
// ExchangeBytes, without a preamble.
static u16 ChunkOffset(u8 chunk)
{
    switch (chunk)
    {
    case 0:
        return 0;
    case 1:
        return GB_RN_BLOCK_SIZE;
    case 2:
        return GB_RN_BLOCK_SIZE + GB_PARTY_PREAMBLE_SIZE + PartyDataSize();
    case 3:
        return GB_RN_BLOCK_SIZE + GB_PARTY_PREAMBLE_SIZE + PartyDataSize()
             + GB_PATCH_LIST_SIZE;
    }

    return 0;
}

static u8 ChunkPreamble(u8 chunk)
{
    switch (chunk)
    {
    case 0:
        return GB_RN_PREAMBLE_SIZE;
    case 1:
        return GB_PARTY_PREAMBLE_SIZE;
    case 2:
        return GB_PATCH_PREAMBLE_SIZE;
    }

    return 0;
}

static u16 ChunkPayload(u8 chunk)
{
    switch (chunk)
    {
    case 0:
        return GB_RN_RANDOM_SIZE;
    case 1:
        return PartyDataSize();
    case 2:
        return GB_PATCH_DATA_SIZE;
    case 3:
        return GB_MAIL_BLOCK_SIZE;
    }

    return 0;
}

// Full size of a chunk on the wire (preamble + payload).  This is what a
// Serial_ExchangeBytes call transfers.
static u16 ChunkTotal(u8 chunk)
{
    return ChunkPreamble(chunk) + ChunkPayload(chunk);
}

static u16 ExpectedBulkSize(void)
{
    if (sGen == TIME_CAPSULE_GEN_1)
        return GB_BLOCK_TRADE_GEN1;
    if (sRoom == TIME_CAPSULE_ROOM_TRADE)
        return GB_BLOCK_TRADE_GEN2;
    return GB_BLOCK_COLOSSEUM_GEN2;
}

static void SetTx(u8 byte)
{
    sTxByte = byte;
}

static void StartBlock(bool8 synced)
{
    (void)GbConvert_BuildTradeBlock(sGen, sRoom, sBlock);
    sTxByte = 0xFE;

    // Emitter: chunk by chunk, with a sync phase in front of each chunk.
    sSendChunk = 0;
    sSendPos = 0;
    sSendOffset = ChunkOffset(0);
    sSendSize = ChunkTotal(0);
    sSendSynced = synced || (ChunkPreamble(0) == 0);

    sBulkStarted = FALSE;
    sBulkCount = 0;
    sRecvChunk = 0;
    sChunkPos = 0;
    sChunkPayload = ChunkPayload(0);
    sChunkSynced = (ChunkPreamble(0) == 0);

    sPartyReady = FALSE;
    sState = GB_STATE_BULK;
    sFrameCounter = 0;
}

// Byte for the next exchange.  This mirrors Serial_ExchangeBytes: while the
// chunk is not synchronised we emit the preamble, and once the GB answers with
// one we stream that chunk's bytes.  When a chunk is exhausted the next one
// starts with its own sync phase (reset_transfer_data_between_sync).
static u8 NextTxByte(void)
{
    if (sState != GB_STATE_BULK)
        return sTxByte;

    if (!sSendSynced)
        return 0xFD;

    if (sSendPos >= sSendSize)
    {
        if (sSendChunk + 1 >= NumberOfChunks())
            return 0xFD; // block sent; keep the line in sync until the GB ends

        sSendChunk++;
        sSendOffset = ChunkOffset(sSendChunk);
        sSendSize = ChunkTotal(sSendChunk);
        sSendPos = 0;
        sSendSynced = (ChunkPreamble(sSendChunk) == 0);
        return 0xFD;
    }

    return sBlock[sSendOffset + sSendPos++];
}

static void SetError(u8 error)
{
    sResult = error;
    sState = GB_STATE_ERROR;
    sFrameCounter = 0;
}

// The GB closed the link and went back to the Cable Club receptionist.  That
// shows up as the establishment byte it keeps in SB (0x01 while it asks for
// the internal clock, 0x02 while it waits as the external-clock slave).  Start
// the discovery from scratch instead of waiting forever.
static void RestartHandshake(void)
{
    sGen = TIME_CAPSULE_GEN_NONE;
    sResult = GB_PROTO_RESULT_ONGOING;
    sLinkEstablished = FALSE;
    sRoomPhase = 0;
    sPartyReady = FALSE;
    sBulkStarted = FALSE;
    sBulkCount = 0;
    sSendSynced = FALSE;
    sSendChunk = 0;
    sSendPos = 0;
    SetTx(0x01);
    sState = GB_STATE_CONNECT;
    sFrameCounter = 0;

#if TIME_CAPSULE_DEBUG_LOG
    DebugPrintfLevel(MGBA_LOG_DEBUG, "TC proto: GB restarted the link");
#endif
}

static void ParseBulk(void)
{
    bool8 ok = GbConvert_ParseTradeBlock(sGen, sRoom, sBulk, sBulkCount);

#if TIME_CAPSULE_DEBUG_LOG
    DebugPrintfLevel(MGBA_LOG_DEBUG, "TC bulk: parse=%u size=%u", ok, sBulkCount);
#endif

    sTxByte = 0xFE;

    if (ok)
    {
        sPartyReady = TRUE;
        sResult = TIME_CAPSULE_OK;
        sState = GB_STATE_TRADE;
        sPhase = GB_PHASE_SELECT;
        sFrameCounter = 0;
    }
    else
    {
        SetError(TIME_CAPSULE_ERR_SPECIES);
    }
}

// Incoming party block.  Mirrors the GB's Serial_ExchangeBytes: every chunk
// starts with a run of preamble bytes which are clocked but not stored, and
// storage begins at the first byte that is not a preamble.  The preamble is
// rebuilt in the buffer so the result always matches the block layout.
static void BeginChunk(u8 chunk)
{
    u8 i;
    u8 preamble = ChunkPreamble(chunk);

    sRecvChunk = chunk;
    sChunkPos = 0;
    sChunkPayload = ChunkPayload(chunk);
    sBulkCount = ChunkOffset(chunk);

    for (i = 0; i < preamble; i++)
        sBulk[sBulkCount++] = 0xFD;

    sChunkSynced = (preamble == 0);
}

static void ProcessBulkByte(u8 byte)
{
    if (!sBulkStarted)
    {
        // Wait for the peer's first preamble byte; it marks the start of the
        // block rather than being part of it.
        if (byte != 0xFD)
            return;

        sBulkStarted = TRUE;
        BeginChunk(0);
    }

    if (!sChunkSynced)
    {
        if (byte == 0xFD)
            return; // still in this chunk's preamble

        sChunkSynced = TRUE;
    }

    if (sBulkCount < ARRAY_COUNT(sBulk))
        sBulk[sBulkCount] = byte;
    sBulkCount++;
    sChunkPos++;

    if (sChunkPos >= sChunkPayload)
    {
        if (sRecvChunk + 1 >= NumberOfChunks())
            ParseBulk();
        else
            BeginChunk(sRecvChunk + 1);
    }
}

static void ProcessTradeByte(u8 byte)
{
    u8 base = TradeBase();

    if (byte == 0xFE || byte == 0x00)
        return;

    if (byte >= base && byte < base + 6)
    {
        sTradeCmd = LINKCMD_READY_TO_TRADE;
        sTradeArg = byte - base;
        sHasTradeCmd = TRUE;
        sPhase = GB_PHASE_SELECT;
    }
    else if (byte == base + 1)
    {
        if (sPhase == GB_PHASE_CONFIRM)
            sTradeCmd = LINKCMD_READY_CANCEL_TRADE;
        else
            sTradeCmd = LINKCMD_REQUEST_CANCEL;
        sTradeArg = 0;
        sHasTradeCmd = TRUE;
    }
    else if (byte == base + 2)
    {
        sTradeCmd = LINKCMD_INIT_BLOCK;
        sTradeArg = 0;
        sHasTradeCmd = TRUE;
        sPhase = GB_PHASE_CONFIRM;
    }
    else if (byte == base + 0xF)
    {
        sTradeCmd = LINKCMD_REQUEST_CANCEL;
        sTradeArg = 0;
        sHasTradeCmd = TRUE;
        sExitValue = base + 0xF;
        sState = GB_STATE_EXIT;
        sFrameCounter = 0;
    }
}

static void ProcessByteInner(u8 byte);

static void ProcessByte(u8 byte)
{
    u8 oldState = sState;

    // SERIAL_NO_DATA_BYTE: the other side had nothing to send on this
    // exchange.  Ignoring it is what the reference implementations do, and it
    // is essential because the GB only arms its byte for a moment.
    if (byte == 0xFE)
        return;

    ProcessByteInner(byte);

#if TIME_CAPSULE_DEBUG_LOG
    if (sState != oldState)
        DebugPrintfLevel(MGBA_LOG_DEBUG, "TC rx=%02X state %u -> %u", byte, oldState, sState);
    else if (oldState != GB_STATE_BULK && oldState != GB_STATE_TRADE)
        DebugPrintfLevel(MGBA_LOG_DEBUG, "TC rx=%02X state=%u", byte, oldState);
#endif
}

static void ProcessByteInner(u8 byte)
{
    switch (sState)
    {
    case GB_STATE_CONNECT:
        // The GBA keeps asking for the external clock (0x01) until the GB
        // answers with its link byte, like Gen3-to-GenX's master routine.
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
        else if (byte == 0x61)
        {
            sGen = TIME_CAPSULE_GEN_2;
            sResult = TIME_CAPSULE_OK;
            if (sForcedGen == TIME_CAPSULE_GEN_1)
            {
                // Put a Gen II game into GSC's Time Capsule mode.
                SetTx(0x00);
                sState = GB_STATE_GEN1_SYNC;
            }
            else
            {
                SetTx(0x61);
                sState = GB_STATE_GEN2_ENTER;
            }
            sFrameCounter = 0;
        }
        else if (byte == 0xD1 || byte == 0xD2)
        {
            if (byte != ((sRoom == TIME_CAPSULE_ROOM_TRADE) ? 0xD1 : 0xD2))
            {
                SetError(TIME_CAPSULE_ERR_WRONG_ROOM);
                break;
            }
            sGen = TIME_CAPSULE_GEN_2;
            sResult = TIME_CAPSULE_OK;
            sLinkEstablished = TRUE;
            sRoomPhase = 0;
            SetTx(byte);
            sState = GB_STATE_ROOM_WAIT;
            sFrameCounter = 0;
        }
        break;

    case GB_STATE_GEN2_ENTER:
        // gen2_start_trade_enter_room: 0x61 -> 0xD1 -> 0x00.  The GB may
        // already be at the room sync when we get here, so accept the room
        // byte directly instead of insisting on another 0x61.
        if (byte == 0xD1 || byte == 0xD2)
        {
            if (byte != ((sRoom == TIME_CAPSULE_ROOM_TRADE) ? 0xD1 : 0xD2))
            {
                SetError(TIME_CAPSULE_ERR_WRONG_ROOM);
                break;
            }
            sLinkEstablished = TRUE;
            sRoomPhase = 0;
            SetTx(byte);
            sState = GB_STATE_ROOM_WAIT;
            sFrameCounter = 0;
        }
        else if (byte == 0x61)
        {
            SetTx(0x61);
        }
        break;

    case GB_STATE_ROOM_WAIT:
        // CheckLinkTimeout_Gen2 on the GB side sends 0x75 (link action 5)
        // until it sees ours, and only later 0x76 (action 6).  Echoing the
        // peer keeps us from switching to 0x76 while the GB is still
        // validating 0x75, which would make it report a timeout.
        if (byte == 0x75)
        {
            sRoomPhase = 1;
            SetTx(0x75);
        }
        else if (byte == 0x76)
        {
            sRoomPhase = 2;
            SetTx(0x76);
            sState = GB_STATE_BLOCK_WAIT;
            sFrameCounter = 0;
        }
        else if (byte == 0x02 || byte == 0x01)
        {
            RestartHandshake();
        }
        else if (sRoomPhase == 0)
        {
            // Keep presenting the room byte.  The GB's Link_EnsureSync
            // retries until one of the two bytes it receives has the 0xD0
            // high nibble, so echoing the "no data" acks would make it fail
            // and send Crystal back to the receptionist.
            SetTx(0xD0 | sRoom);
        }
        break;

    case GB_STATE_BLOCK_WAIT:
        // Keep repeating 0x76 until the GB leaves its own 0x76 phase.  It only
        // starts the party block afterwards, and the first byte it sends is
        // the preamble of the first Serial_ExchangeBytes chunk.
        if (byte == 0xFD)
        {
            StartBlock(FALSE);
            ProcessBulkByte(byte);
        }
        else if (byte == 0x76)
        {
            SetTx(0x76);
        }
        else if (byte == 0x02 || byte == 0x01)
        {
            RestartHandshake();
        }
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
            // The Gen I player picked "CANCEL" at its own CABLE CLUB menu.
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
            // The Gen I game does several 0x60 sync exchanges (and ten zero
            // bytes) before it starts the RN block.  Do not start the bulk
            // data yet or it would be consumed by those sync exchanges.
            SetTx(0x60);
        }
        else if (byte == 0xFD)
        {
            // First byte of the GB's RN preamble.  Gen I does not run the
            // 0x75/0x76 exchange, so start our block immediately and treat
            // this byte as the first chunk's preamble.
            StartBlock(TRUE);
            ProcessBulkByte(byte);
        }
        break;

    case GB_STATE_BULK:
        // Emitter sync: the GB answers our preamble with one of its own, and
        // that is the signal to start streaming the current chunk.
        if (!sSendSynced && byte == 0xFD)
            sSendSynced = TRUE;

        ProcessBulkByte(byte);
        break;

    case GB_STATE_TRADE:
        ProcessTradeByte(byte);
        break;

    case GB_STATE_SUCCESS_WAIT:
        // The GB sends its anti-cloning byte (0x70/0x71/0x72 or 0x60) after
        // its trade animation.  Capture it without sending ours yet; the GBA
        // sends its own byte after saving.
        if (byte >= TradeBase() && byte <= TradeBase() + 2)
            sPeerSuccessValue = byte;
        break;

    case GB_STATE_SUCCESS:
        if (byte == sSuccessValue)
        {
            sSuccessDone = TRUE;
            GbProtocol_StartReexchange();
        }
        else
        {
            SetTx(sSuccessValue);
        }
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
    sPhase = GB_PHASE_SELECT;
    sSuccessDone = FALSE;
    sPeerSuccessValue = 0;
    sRoomPhase = 0;

    GbLink_Init();

    // First byte: ask the GB to use the external clock, so the GBA stays the
    // clock master (Poke_Transporter_GB does the same: outData = 0x01).
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

    // The GB can stop answering at any point (cable pulled, game reset).
    // Anything else is controlled by the player.
    if ((sState == GB_STATE_BULK || sState == GB_STATE_BLOCK_WAIT)
     && sFrameCounter > GB_BULK_TIMEOUT)
        SetError(TIME_CAPSULE_ERR_TIMEOUT);

#if TIME_CAPSULE_DEBUG_LOG
    if ((sFrameCounter % 60) == 0)
    {
        GbLink_LogDebug();
        DebugPrintfLevel(MGBA_LOG_DEBUG, "TC proto: state=%u gen=%u result=%u bulk=%u/%u",
                         sState, sGen, sResult, sBulkCount, ExpectedBulkSize());
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
            return; // the GB has not armed its byte yet

        ProcessByte(rx);
        sTxByte = NextTxByte();
    }

    switch (sState)
    {
    case GB_STATE_CONNECT:
#if GB_CONNECT_TIMEOUT > 0
        if (sFrameCounter > GB_CONNECT_TIMEOUT)
            SetError(TIME_CAPSULE_ERR_NO_LINK);
#endif
        break;

    case GB_STATE_SUCCESS:
        if (sFrameCounter > GB_SUCCESS_TIMEOUT)
        {
            sSuccessDone = TRUE;
            GbProtocol_StartReexchange();
        }
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

u8 GbProtocol_GetResult(void)
{
    return sResult;
}

u8 GbProtocol_GetGeneration(void)
{
    return sGen;
}

u8 GbProtocol_GetRoom(void)
{
    return sRoom;
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

    switch (cmd)
    {
    case LINKCMD_READY_TO_TRADE:
        SetTx(base + (arg & 0xF));
        sPhase = GB_PHASE_SELECT;
        break;
    case LINKCMD_INIT_BLOCK:
        SetTx(base + 2);
        sPhase = GB_PHASE_CONFIRM;
        break;
    case LINKCMD_READY_CANCEL_TRADE:
        SetTx(base + 1);
        break;
    case LINKCMD_REQUEST_CANCEL:
        SetTx(base + 1);
        break;
    case LINKCMD_PLAYER_CANCEL_TRADE:
    case LINKCMD_BOTH_CANCEL_TRADE:
    case LINKCMD_PARTNER_CANCEL_TRADE:
        sExitValue = base + 0xF;
        SetTx(sExitValue);
        sState = GB_STATE_EXIT;
        sFrameCounter = 0;
        break;
    case LINKCMD_START_TRADE:
        // Both sides confirmed; the GB animates and then sends its success
        // byte.  Capture it, but do not send ours until after the GBA save.
        sPeerSuccessValue = 0;
        sState = GB_STATE_SUCCESS_WAIT;
        sFrameCounter = 0;
        break;
    case LINKCMD_READY_FINISH_TRADE:
        GbProtocol_StartSuccess();
        break;
    default:
        break;
    }
}

void GbProtocol_StartSuccess(void)
{
    u8 base = TradeBase();
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

    sSuccessValue = base + n;
    sSuccessDone = FALSE;

    if (sPeerSuccessValue == sSuccessValue)
    {
        sSuccessDone = TRUE;
        GbProtocol_StartReexchange();
        return;
    }

    sState = GB_STATE_SUCCESS;
    sFrameCounter = 0;
    SetTx(sSuccessValue);
}

bool8 GbProtocol_IsSuccessDone(void)
{
    return sSuccessDone;
}

void GbProtocol_StartReexchange(void)
{
    if (!sActive || sState == GB_STATE_DONE)
        return;

    StartBlock(FALSE);
}

void GbProtocol_RequestExit(void)
{
    u8 base = TradeBase();

    sExitValue = base + 0xF;
    SetTx(sExitValue);
    sState = GB_STATE_EXIT;
    sFrameCounter = 0;
}
