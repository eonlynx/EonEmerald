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
    GB_PARTNER_SELECT = 0, // Waiting for the Pokémon the partner selected.
    GB_PARTNER_CONFIRM,    // Waiting for the answer of the partner's confirm screen.
};

// The steps of the Gen I/II trade start handshake.
enum
{
    GB_ROOM_SYNC = 0,   // The room byte (0xD0 | room) is exchanged.
    GB_ROOM_START_TRADE, // The start-trade byte (0x75) is exchanged.
    GB_ROOM_PEER_READY,  // The GB sent its start-trade byte, it waits for ours.
    GB_ROOM_BLOCK,       // The 0x76 is exchanged, the GB waits for the block.
};

// Gen I/II byte time: 8192 bps -> ~1 ms -> 14 scanlines at 59.7 Hz.
#define GB_LINK_PACE_LINES    14
// Give up on this frame if the GB has not clocked back within this window;
// the transfer stays pending and is retried on the next frame.
#define GB_LINK_REPLY_LINES   10
// 228 scanlines per frame / 14 = 16 bytes; 10 leaves room for the rest of the
// frame (rendering, input) and for slow GB replies.
#define GB_LINK_BYTES_FRAME   10

// The other game has to keep answering: if it stops sending bytes its console
// was turned off or the cable was pulled.  The Gen I/II games only exchange
// bytes while they are really linking, so the timeout is generous in the
// states where the other game waits for its own player (at the console or in
// the trade menu).
#define GB_LINK_ALIVE_TIMEOUT      600  // 10 seconds
#define GB_LINK_ALIVE_TIMEOUT_LONG 3600 // 60 seconds

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
// The other game left the trade room (it sent the Gen I/II exit byte).
static EWRAM_DATA bool8 sPeerExited;
// Frames since the other game last sent something.
static EWRAM_DATA u16 sNoDataFrames;

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
static EWRAM_DATA u8 sPartnerState;

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
    sRoomPhase = GB_ROOM_SYNC;
    sPartyReady = FALSE;
    sBulkStarted = FALSE;
    sBulkCount = 0;
    sSendSynced = FALSE;
    sSendChunk = 0;
    sSendPos = 0;
    sPeerExited = FALSE;
    sNoDataFrames = 0;
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
        sPartnerState = GB_PARTNER_SELECT;
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

// The Gen I/II trade menu exchanges two bytes per round: the index of the
// selected Pokémon and then the answer of the confirm screen (1 = the trade
// was canceled, 2 = the trade was confirmed).  The index of the second
// Pokémon is 1 as well and the index of the third Pokémon is 2 as well, so
// the screen the partner is on has to be tracked to tell them apart.
static void ProcessTradeByte(u8 byte)
{
    u8 base = TradeBase();

    if (byte == 0xFE || byte == 0x00)
        return;

    if (byte == base + 0xF)
    {
        // The partner left the trade room.  This is the Gen I/II "leave the
        // link" byte: answer it with the same byte and tear the link down.
        sTradeCmd = LINKCMD_REQUEST_CANCEL;
        sTradeArg = 0;
        sHasTradeCmd = TRUE;
        sPeerExited = TRUE;
        sExitValue = base + 0xF;
        SetTx(sExitValue);
        sState = GB_STATE_EXIT;
        sFrameCounter = 0;
        return;
    }

    if (byte < base || byte >= base + 6)
        return;

    if (sPartnerState == GB_PARTNER_CONFIRM)
    {
        if (byte == base + 1)
        {
            // The partner canceled the trade.  It goes back to the party
            // menu, so the next byte is a new selection.
            sTradeCmd = LINKCMD_READY_CANCEL_TRADE;
            sTradeArg = 0;
            sHasTradeCmd = TRUE;
            sPartnerState = GB_PARTNER_SELECT;
            return;
        }
        if (byte == base + 2)
        {
            // The partner confirmed the trade.
            sTradeCmd = LINKCMD_INIT_BLOCK;
            sTradeArg = 0;
            sHasTradeCmd = TRUE;
            return;
        }
    }

    // The partner selected one of its Pokémon.
    sTradeCmd = LINKCMD_READY_TO_TRADE;
    sTradeArg = byte - base;
    sHasTradeCmd = TRUE;
    sPartnerState = GB_PARTNER_CONFIRM;
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

    sNoDataFrames = 0;
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
        else if (byte == 0x61 || byte == 0x71)
        {
            // The GB's receptionist byte.  The high nibble is the link mode:
            // 0x60 is the Gen I time capsule, 0x70 the trade center.  It has
            // to be answered with the same high nibble, or the GB's
            // WaitLinkTransfer never accepts the byte and the link fails.
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
                SetTx(byte);
                sState = GB_STATE_GEN2_ENTER;
            }
            sFrameCounter = 0;
        }
        else if (byte == 0xDE)
        {
            // FailedLinkToPast: the Gen I game cannot link to the past.
            SetError(TIME_CAPSULE_ERR_NO_LINK);
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
            sRoomPhase = GB_ROOM_SYNC;
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
            sRoomPhase = GB_ROOM_SYNC;
            SetTx(byte);
            sState = GB_STATE_ROOM_WAIT;
            sFrameCounter = 0;
        }
        else if (byte == 0x61 || byte == 0x71)
        {
            // The GB's receptionist byte again (see GB_STATE_CONNECT).
            SetTx(byte);
        }
        break;

    case GB_STATE_ROOM_WAIT:
        // CheckLinkTimeout_Gen2 on the GB side sends 0x75 (link action 5)
        // until it sees ours, and only later 0x76 (action 6).  Echoing the
        // peer keeps us from switching to 0x76 while the GB is still
        // validating 0x75, which would make it report a timeout.
        // The Gen I/II trade start is a three step handshake (see
        // Pokemon-Gen3-to-GenX's gen2_start_trade_start_trade_procedure):
        //
        //   the master sends 0x75, waits for the peer's 0x75, sends 0x75
        //   again, waits for the peer's 0x00 acknowledgement and only then
        //   sends 0x76.
        //
        // The 0x76 has to be sent *before* the GB reads it: pokecrystal's
        // CheckLinkTimeout_Gen2 reads the byte of the previous exchange (the
        // slave is one exchange behind), so a master that waits for the GB's
        // 0x76 would make the GB read a 0x75 and report a timeout.
        if (byte == 0x75)
        {
            // The GB is ready for the trade (its start-trade byte).
            if (sRoomPhase == GB_ROOM_START_TRADE)
                sRoomPhase = GB_ROOM_PEER_READY;
            SetTx(0x75);
        }
        else if (byte == 0x00)
        {
            if (sRoomPhase == GB_ROOM_SYNC)
            {
                // The room sync is finished (the GB sent its 0x00 acks, so
                // its Link_EnsureSync is done).  Present the start-trade byte
                // right away, exactly like gen2_start_trade_start_trade_procedure
                // does, instead of waiting for the GB to offer it first.
                sRoomPhase = GB_ROOM_START_TRADE;
                SetTx(0x75);
            }
            else if (sRoomPhase == GB_ROOM_START_TRADE || sRoomPhase == GB_ROOM_PEER_READY)
            {
                // The GB acknowledged the start-trade byte and now waits for
                // the block, so the 0x76 can be sent.
                sRoomPhase = GB_ROOM_BLOCK;
                SetTx(0x76);
            }
        }
        else if (byte == 0x76)
        {
            // The GB waits for the block now.
            sRoomPhase = GB_ROOM_BLOCK;
            SetTx(0x76);
            sState = GB_STATE_BLOCK_WAIT;
            sFrameCounter = 0;
        }
        else if (byte == 0xFD)
        {
            // The GB already starts the block.
            StartBlock(FALSE);
            ProcessBulkByte(byte);
        }
        else if (byte == 0x02 || byte == 0x01)
        {
            RestartHandshake();
        }
        else if (byte == 0x7F)
        {
            // END_TRADE_BYTE_GEN2: the GB left the Cable Club room.
            sExitValue = 0x7F;
            SetTx(0x7F);
            sState = GB_STATE_EXIT;
            sFrameCounter = 0;
        }
        else if (sRoomPhase == GB_ROOM_SYNC)
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
        else if (byte == 0x7F)
        {
            sExitValue = 0x7F;
            SetTx(0x7F);
            sState = GB_STATE_EXIT;
            sFrameCounter = 0;
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
        // The anti-cloning byte has to match exactly: pokecrystal keeps
        // sending its byte until it receives the same value
        // (.send_checkbyte in engine/link/link.asm).
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
    sPartnerState = GB_PARTNER_SELECT;
    sPeerExited = FALSE;
    sNoDataFrames = 0;
    sSuccessDone = FALSE;
    sPeerSuccessValue = 0;
    sRoomPhase = GB_ROOM_SYNC;

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

// The other game has to keep answering.  When it stops sending bytes its
// console was turned off or the cable was pulled, which the vanilla Cable
// Club reports with the black "Communication error" screen.
static void CheckPeerIsAlive(void)
{
    u16 timeout;

    switch (sState)
    {
    case GB_STATE_CONNECT:
        // Nobody answered the linkup.  The vanilla Cable Club shows its
        // "link error" message for this, not the black screen.
        if (++sNoDataFrames > GB_LINK_ALIVE_TIMEOUT)
            SetError(TIME_CAPSULE_ERR_TIMEOUT);
        return;
    case GB_STATE_ROOM_WAIT:
    case GB_STATE_TRADE:
        // The Gen I/II game waits for its own player (at the console or in
        // the trade menu) and does not exchange bytes while it does, so this
        // can only be a long timeout.
        timeout = GB_LINK_ALIVE_TIMEOUT_LONG;
        break;
    case GB_STATE_BULK:
    case GB_STATE_BLOCK_WAIT:
    case GB_STATE_SUCCESS_WAIT:
    case GB_STATE_SUCCESS:
        timeout = GB_LINK_ALIVE_TIMEOUT;
        break;
    default:
        return;
    }

    if (++sNoDataFrames > timeout)
        SetError(TIME_CAPSULE_ERR_LINK_LOST);
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

    CheckPeerIsAlive();

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

    switch (cmd)
    {
    case LINKCMD_READY_TO_TRADE:
        // The index of the selected Pokémon (0-5).
        SetTx(base + (arg & 0xF));
        break;
    case LINKCMD_INIT_BLOCK:
        // The confirm screen of the Gen I/II games.
        SetTx(base + 2);
        break;
    case LINKCMD_READY_CANCEL_TRADE:
        // The trade was declined, both of them stay in the trade room.
        SetTx(base + 1);
        break;
    case LINKCMD_REQUEST_CANCEL:
        // The player leaves the trade room.  The Gen I/II games leave the
        // link with the 0xF byte, the same one the partner uses to leave.
        sExitValue = base + 0xF;
        SetTx(sExitValue);
        sState = GB_STATE_EXIT;
        sFrameCounter = 0;
        break;
    case LINKCMD_PLAYER_CANCEL_TRADE:
    case LINKCMD_PARTNER_CANCEL_TRADE:
        // The trade was canceled, both of them stay in the trade room.
        SetTx(base + 1);
        break;
    case LINKCMD_BOTH_CANCEL_TRADE:
        // Both of them leave the trade room.
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
