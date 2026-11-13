#include "global.h"
#include "time_capsule.h"
#include "time_capsule/gb_protocol.h"
#include "time_capsule/gb_convert.h"
#include "time_capsule/gb_trade_link.h"
#include "constants/event_object_movement.h"
#include "constants/event_objects.h"
#include "constants/cable_club.h"
#include "constants/trainer_types.h"
#include "event_data.h"
#include "event_object_movement.h"
#include "event_scripts.h"
#include "field_control_avatar.h"
#include "fieldmap.h"
#include "field_message_box.h"
#include "field_player_avatar.h"
#include "field_screen_effect.h"
#include "link.h"
#include "m4a.h"
#include "main.h"
#include "move.h"
#include "overworld.h"
#include "palette.h"
#include "pokemon.h"
#include "script.h"
#include "sound.h"
#include "string_util.h"
#include "strings.h"
#include "task.h"
#include "constants/songs.h"

#if TIME_CAPSULE_DEBUG_LOG
#include "gba/isagbprint.h"
#endif

// The Gen I/II trade room has two consoles in the middle of the room with a
// seat on each side of them.  The Gen I/II games draw the friend sitting at
// the other console, facing the machine (pokecrystal's maps/TradeCenter.asm
// and maps/TimeCapsule.asm, the "Chris" sprites at (3,4) and (6,4)).
//
// The GBA link can show the other player walking around the room, but the GB
// cannot, so the friend is already seated at the other console (it is an
// object event of the trade center map).
//
// The GBA is always the clock master, so the friend sits at the right
// console, like pokecrystal does for the player that uses the internal clock
// (maps/TradeCenter.asm, the "Chris" sprite at (6,4)).

// The Gen I/II link does not send the trainer's gender, so the partner uses a
// fixed sprite (see TIME_CAPSULE_PARTNER_IS_FEMALE in the config header).
#if TIME_CAPSULE_PARTNER_IS_FEMALE
#define TIME_CAPSULE_PARTNER_GFX OBJ_EVENT_GFX_LEAF
#else
#define TIME_CAPSULE_PARTNER_GFX OBJ_EVENT_GFX_RED
#endif

// The seat the player took (0 = the left console, 1 = the right one).  The
// chair trigger passes it in VAR_0x8005, like the vanilla
// PlayerEnteredTradeSeat does.
#define TIME_CAPSULE_SEAT_UNKNOWN 0xFF
static EWRAM_DATA u8 sTradeSeat;

// Public session state used by trade.c/link.c.
bool8 gTimeCapsuleActive;
u8 gTimeCapsuleGeneration;
u8 gTimeCapsuleRoom;

#define tState data[0]
#define tTimer data[1]

static void Task_TimeCapsuleConfirm(u8 taskId);
static void Task_TimeCapsuleConnect(u8 taskId);
static void Task_TimeCapsuleExchange(u8 taskId);
static void Task_TimeCapsuleClose(u8 taskId);
static void Task_TimeCapsuleWarp(u8 taskId);
static void Task_TimeCapsuleLink(u8 taskId);
static void TimeCapsule_OnReturnToField(void);

// The GB link has to be serviced every frame for as long as the session is
// alive, not only while a particular script step is waiting: Crystal runs its
// own 0x75/0x76 handshake and its party exchange as soon as it enters the
// Cable Club room, and both time out after a few seconds.  This task keeps the
// GBA clocking during the confirmation prompts, the warp and the trade menu.
static void Task_TimeCapsuleLink(u8 taskId)
{
    if (!gTimeCapsuleActive || !GbProtocol_IsActive())
    {
        DestroyTask(taskId);
        return;
    }

    GbProtocol_Update();

    if (GbProtocol_IsFinished() || GbProtocol_IsError())
        DestroyTask(taskId);
}

// Loading a map (the cable club room is entered with a warp) resets every
// task, so the link task created before the warp is gone afterwards and a
// plain "already created" flag would lie forever.  Always look the task up.
void TimeCapsule_EnsureLinkTask(void)
{
    if (!gTimeCapsuleActive || !GbProtocol_IsActive() || GbProtocol_IsFinished()
     || GbProtocol_IsError())
        return;

    if (FindTaskIdByFunc(Task_TimeCapsuleLink) == TASK_NONE)
        CreateTask(Task_TimeCapsuleLink, 80);
}

// The friend of the other game leaves the trade room together with the
// player.  It is an object event of the trade center map (see
// data/maps/TradeCenter), which the map's on-frame script hides unless a
// Time Capsule session is running.
static void RemoveTradePartner(void)
{
    RemoveObjectEventByLocalIdAndMap(LOCALID_TRADE_CENTER_PARTNER,
                                      gSaveBlock1Ptr->location.mapNum,
                                      gSaveBlock1Ptr->location.mapGroup);
}

// The player sits down at the console of the trade room.  Like the vanilla
// Cable Club (GetDirectionForEventScript in overworld.c) the sprite turns
// towards the machine.
static void FacePlayerAtTradeConsole(u8 seat)
{
    enum Direction dir = (seat == 0) ? DIR_EAST : DIR_WEST;
    struct ObjectEvent *playerObjEvent = &gObjectEvents[gPlayerAvatar.objectEventId];

    SetObjectEventDirection(playerObjEvent, dir);
    PlayerFaceDirection(dir);
}

// Ends the current `special` and returns to the script.  Only a clean session
// (link still up) keeps the GB link alive for the next step.
static void FinishSession(u8 taskId, u8 result)
{
    gSpecialVar_Result = result;

    // A cable pull / GB power-off at any point shows the same black
    // "Communication error" screen as the vanilla Cable Club.
    if (result == TIME_CAPSULE_ERR_LINK_LOST)
    {
        GbProtocol_Shutdown();
        gTimeCapsuleActive = FALSE;
        RemoveTradePartner();
        HideFieldMessageBox();
        SetLinkErrorBuffer(0, 0, 0, FALSE);
        SetMainCallback2(CB2_LinkError);
        DestroyTask(taskId);
        return;
    }

    if (result != TIME_CAPSULE_OK && result != TIME_CAPSULE_LINK_ESTABLISHED
     && result != TIME_CAPSULE_CONFIRMED)
    {
        GbProtocol_Shutdown();
        gTimeCapsuleActive = FALSE;
        RemoveTradePartner();
    }
    ScriptContext_Enable();
    DestroyTask(taskId);
}

// TRUE when the GB link died before the current step finished.
static bool8 ProtocolFailed(u8 *result)
{
    if (GbProtocol_IsError())
    {
        *result = GbProtocol_GetResult();
        if (*result == GB_PROTO_RESULT_ONGOING || *result == TIME_CAPSULE_OK)
            *result = TIME_CAPSULE_ERR_TIMEOUT;
        return TRUE;
    }
    if (GbProtocol_IsFinished())
    {
        *result = TIME_CAPSULE_ERR_TIMEOUT;
        return TRUE;
    }
    return FALSE;
}

void TimeCapsule_Confirm(void)
{
    u8 taskId = CreateTask(Task_TimeCapsuleConfirm, 80);

    gTasks[taskId].tState = 0;
    gTasks[taskId].tTimer = 0;
}

// The player cancels the link: the other game has to be told before the cable
// is torn down, otherwise it stays waiting in the Cable Club.  The exit byte
// is the same one the Gen I/II games use to leave a link room (0xF), and the
// link is kept alive for a moment so the other game can clock it.
#define TIME_CAPSULE_EXIT_FRAMES 60
#define TIME_CAPSULE_STATE_EXITING  5

static void RequestLinkExit(u8 taskId)
{
    GbTrade_RequestExit();
    gTasks[taskId].tTimer = 0;
    gTasks[taskId].tState = TIME_CAPSULE_STATE_EXITING;
}

// Waits for the other game to clock the exit byte.  Returns TRUE when the
// cable can be torn down.
static bool8 WaitForLinkExit(u8 taskId)
{
    if (GbProtocol_IsFinished() || ++gTasks[taskId].tTimer > TIME_CAPSULE_EXIT_FRAMES)
    {
        GbProtocol_Shutdown();
        gTimeCapsuleActive = FALSE;
        return TRUE;
    }

    return FALSE;
}

static void Task_TimeCapsuleConfirm(u8 taskId)
{
    s16 *data = gTasks[taskId].data;
    u8 result;

    if (tState == TIME_CAPSULE_STATE_EXITING)
    {
        if (WaitForLinkExit(taskId))
            FinishSession(taskId, TIME_CAPSULE_ERR_CANCELLED);
        return;
    }

    // The GB is answered by Task_TimeCapsuleLink while the player reads the
    // prompts; this task only drives the message box and the input.
    if (ProtocolFailed(&result))
    {
        HideFieldMessageBox();
        FinishSession(taskId, result);
        return;
    }

    switch (tState)
    {
    case 0:
        ShowFieldAutoScrollMessage(gText_ConfirmLinkWhenPlayersReady);
        tTimer = 0;
        tState++;
        break;
    case 1:
        if (++tTimer > 30)
            tState++;
        break;
    case 2:
        if (JOY_NEW(A_BUTTON))
        {
            PlaySE(SE_SELECT);
            HideFieldMessageBox();
            // You + the other game = 2 players.
            ConvertIntToDecimalStringN(gStringVar1, 2, STR_CONV_MODE_LEFT_ALIGN, 1);
            ShowFieldAutoScrollMessage(gText_ConfirmStartLinkWithXPlayers);
            tTimer = 0;
            tState++;
        }
        else if (JOY_NEW(B_BUTTON))
        {
            HideFieldMessageBox();
            RequestLinkExit(taskId);
        }
        break;
    case 3:
        if (++tTimer > 30)
            tState++;
        break;
    case 4:
        if (JOY_NEW(A_BUTTON))
        {
            PlaySE(SE_SELECT);
            HideFieldMessageBox();
            FinishSession(taskId, TIME_CAPSULE_CONFIRMED);
        }
        else if (JOY_NEW(B_BUTTON))
        {
            HideFieldMessageBox();
            RequestLinkExit(taskId);
        }
        break;
    }
}

void TimeCapsule_TryConnect(void)
{
    u8 taskId;

    gTimeCapsuleRoom = gSpecialVar_0x8005;
    gTimeCapsuleGeneration = TIME_CAPSULE_GEN_NONE;
    gTimeCapsuleActive = TRUE;
    sTradeSeat = TIME_CAPSULE_SEAT_UNKNOWN;

    // Kanto/Johto species are outside the Hoenn dex; the trade menu refuses
    // them unless the National Dex is enabled.
    if (!IsNationalPokedexEnabled())
        EnableNationalPokedex();

    GbProtocol_Init(gTimeCapsuleRoom);
    TimeCapsule_EnsureLinkTask();

    taskId = CreateTask(Task_TimeCapsuleConnect, 80);
    gTasks[taskId].tState = 0;
    gTasks[taskId].tTimer = 0;
}

// Step 1: wait (with "Please wait." on screen) until the GB picks the same
// room.  This is the analogue of the Cable Club linkup, so the confirmation
// prompts come afterwards, exactly like in the GBA Cable Club.
static void Task_TimeCapsuleConnect(u8 taskId)
{
    s16 *data = gTasks[taskId].data;
    u8 result;

    if (tState == TIME_CAPSULE_STATE_EXITING)
    {
        if (WaitForLinkExit(taskId))
            FinishSession(taskId, TIME_CAPSULE_ERR_CANCELLED);
        return;
    }

    if (GbProtocol_IsLinkEstablished())
    {
        gTimeCapsuleGeneration = GbProtocol_GetGeneration();
        FinishSession(taskId, TIME_CAPSULE_LINK_ESTABLISHED);
        return;
    }

    if (ProtocolFailed(&result))
    {
        FinishSession(taskId, result);
        return;
    }

    if (JOY_NEW(B_BUTTON))
    {
        RequestLinkExit(taskId);
        return;
    }
}

// The player sits down at the console of the trade room.  Like the vanilla
// Cable Club the sprite turns towards the machine, and the friend of the
// other game is already sitting at the other console.

// Step 3: wait for the party block while the GB player presses A at its trade
// room console.  The GBA is already standing on a chair in its own trade room.
void TimeCapsule_StartExchange(void)
{
    u8 taskId = CreateTask(Task_TimeCapsuleExchange, 80);

    // The chair the player took is passed in VAR_0x8005, like the vanilla
    // PlayerEnteredTradeSeat does.  It is remembered, because the player can
    // cancel the seat and take it again.
    if (sTradeSeat == TIME_CAPSULE_SEAT_UNKNOWN)
        sTradeSeat = gSpecialVar_0x8005;

    gTasks[taskId].tState = 0;
    gTasks[taskId].data[2] = sTradeSeat;
}

static void Task_TimeCapsuleExchange(u8 taskId)
{
    s16 *data = gTasks[taskId].data;
    u8 result;

    if (tState == 0)
    {
        // The vanilla Cable Club shows "PLEASE WAIT FOR LINK..." while the
        // player takes a seat and waits for the other player.
        ShowFieldMessage(gText_PleaseWaitForLink);
        FacePlayerAtTradeConsole(data[2]);
        tState = 1;
    }

    if (GbProtocol_IsReady())
    {
        HideFieldMessageBox();
        gTimeCapsuleGeneration = GbProtocol_GetGeneration();
        FinishSession(taskId, TIME_CAPSULE_OK);
        return;
    }

    if (ProtocolFailed(&result))
    {
        HideFieldMessageBox();
        FinishSession(taskId, result);
        return;
    }

    if (GbProtocol_PeerLeft())
    {
        // The other game left the trade room: the player is escorted out of
        // the room, like the vanilla Cable Club does when the link is
        // terminated.
        HideFieldMessageBox();
        DestroyTask(taskId);
        ScriptContext_SetupScript(TimeCapsule_EventScript_TerminateLink);
        return;
    }

    if (JOY_NEW(B_BUTTON))
    {
        // The vanilla Cable Club asks for a confirmation before the link is
        // terminated.
        HideFieldMessageBox();
        DestroyTask(taskId);
        ScriptContext_SetupScript(TimeCapsule_EventScript_ConfirmLeaveLinkRoom);
    }
}

void TimeCapsule_CloseLink(void)
{
    u8 taskId;

    // Always defer to a task.  If we called ScriptContext_Enable() directly
    // here, the waitstate that follows the `special` command would stop the
    // script again and the NPC would appear frozen at the next message.
    taskId = CreateTask(Task_TimeCapsuleClose, 80);
    gTasks[taskId].tState = 0;
    gTasks[taskId].tTimer = 0;

    // Leaving the trade center resets every task, so the link task that was
    // pumping the protocol is gone by the time the NPC asks for the exit
    // handshake.  Bring it back or the GB never sees the 0x7F.
    TimeCapsule_EnsureLinkTask();

    if (gTimeCapsuleActive)
        GbTrade_RequestExit();
}

static void Task_TimeCapsuleClose(u8 taskId)
{
    s16 *data = gTasks[taskId].data;

    if (!gTimeCapsuleActive)
    {
        ScriptContext_Enable();
        DestroyTask(taskId);
        return;
    }

    tTimer++;

    if (GbProtocol_IsFinished() || tTimer > 120)
    {
        GbProtocol_Shutdown();
        gTimeCapsuleActive = FALSE;
        RemoveTradePartner();
        ScriptContext_Enable();
        DestroyTask(taskId);
    }
}

void TimeCapsule_CheckParty(void)
{
    u16 arg1 = 0;
    u16 arg2 = 0;
    u8 result = GbConvert_CheckParty(gTimeCapsuleGeneration, &arg1, &arg2);

    gSpecialVar_Result = result;
    if (result == TIME_CAPSULE_ERR_SPECIES || result == TIME_CAPSULE_ERR_MOVE
     || result == TIME_CAPSULE_ERR_MAIL || result == TIME_CAPSULE_ERR_EGG
     || result == TIME_CAPSULE_ERR_SHINY || result == TIME_CAPSULE_ERR_BAD_EGG)
    {
        if (arg1 != 0)
            StringCopy(gStringVar1, GetSpeciesName(arg1));
        if (arg2 != 0)
            StringCopy(gStringVar2, GetMoveName(arg2));
    }

#if TIME_CAPSULE_DEBUG_LOG
    DebugPrintfLevel(MGBA_LOG_DEBUG, "TC party: result=%u mon=%u move=%u",
                     result, arg1, arg2);
#endif
}

void TimeCapsule_StartBattle(void)
{
    // Colosseum is phase 2: it needs a full Gen II/Gen I battle engine.
    gSpecialVar_Result = TIME_CAPSULE_ERR_NOT_READY;
}

// The friend of the other game is drawn with the Gen I sprites (the same
// ones the expansion uses for a Gen I/II link partner).  The sprite is set
// once the trade center has been loaded: the object event uses a fixed
// graphics id, so the shared dynamic graphics vars (VAR_OBJ_GFX_ID_*) that
// other maps use for their own NPCs are left alone.
static void SetTradePartnerGraphics(void)
{
    u8 objectEventId = GetObjectEventIdByLocalIdAndMap(LOCALID_TRADE_CENTER_PARTNER,
                                                       gSaveBlock1Ptr->location.mapNum,
                                                       gSaveBlock1Ptr->location.mapGroup);

    if (objectEventId != OBJECT_EVENTS_COUNT)
        ObjectEventSetGraphicsId(&gObjectEvents[objectEventId], TIME_CAPSULE_PARTNER_GFX);
}

void TimeCapsule_DoWarp(void)
{
    LockPlayerFieldControls();
    TryFadeOutOldMapMusic();
    WarpFadeOutScreen();
    PlaySE(SE_EXIT);
    CreateTask(Task_TimeCapsuleWarp, 10);
}

static void Task_TimeCapsuleWarp(u8 taskId)
{
    s16 *data = gTasks[taskId].data;

    switch (tState)
    {
    case 0:
        LockPlayerFieldControls();
        tState++;
        break;
    case 1:
        if (!gPaletteFade.active && BGMusicStopped())
            tState++;
        break;
    case 2:
        // The state has to be set before the map is loaded: the trade center's
        // on-warp-into-map table uses it to show the friend of the Time Capsule,
        // and the Cable Club's on-frame script (which reads it too) cannot run
        // any more once CB2_LoadMap clears CB1.
        VarSet(VAR_CABLE_CLUB_STATE, USING_TIME_CAPSULE_TRADE);
        gFieldCallback = TimeCapsule_OnReturnToField;
        WarpIntoMap();
        // CB2_ReturnToField only resumes the previous map and never reloads
        // tilesets or the tilemap, which left the trade center corrupted.
        // CB2_LoadMap performs the full map load (and still runs
        // gFieldCallback from LoadMapInStepsLocal).
        SetMainCallback2(CB2_LoadMap);
        DestroyTask(taskId);
        break;
    }
}

// Runs once the trade center has finished loading.  Entering the room resets
// every task (ResumeMap -> ResetTasks), which would leave the GB without a
// clock while the player walks to the seat and Crystal is already sending its
// 0x75/0x76 handshake.  Recreate the link task here.
//
// The friend of the other game is already sitting at the other console, like
// the Gen I/II trade room does.
static void TimeCapsule_OnReturnToField(void)
{
    FieldCB_DefaultWarpExit();
    SetTradePartnerGraphics();
    TimeCapsule_EnsureLinkTask();
}
