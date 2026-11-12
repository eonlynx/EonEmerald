#include "global.h"
#include "time_capsule.h"
#include "time_capsule/gb_protocol.h"
#include "time_capsule/gb_convert.h"
#include "time_capsule/gb_trade_link.h"
#include "event_data.h"
#include "field_control_avatar.h"
#include "field_message_box.h"
#include "field_screen_effect.h"
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

// Public session state used by trade.c/link.c.
bool8 gTimeCapsuleActive;
u8 gTimeCapsuleGeneration;
u8 gTimeCapsuleRoom;
u8 gTimeCapsuleMode;

#define tState data[0]
#define tTimer data[1]

#define LINK_TASK_ID_NONE 0xFF

static EWRAM_DATA u8 sLinkTaskId;
static EWRAM_DATA bool8 sLinkTaskRunning;

static void Task_TimeCapsuleConfirm(u8 taskId);
static void Task_TimeCapsuleConnect(u8 taskId);
static void Task_TimeCapsuleExchange(u8 taskId);
static void Task_TimeCapsuleClose(u8 taskId);
static void Task_TimeCapsuleWarp(u8 taskId);
static void Task_TimeCapsuleLink(u8 taskId);

// The GB link has to be serviced every frame for as long as the session is
// alive, not only while a particular script step is waiting: Crystal runs its
// own 0x75/0x76 handshake and its party exchange as soon as it enters the
// Cable Club room, and both time out after a few seconds.  This task keeps the
// GBA clocking during the confirmation prompts, the warp and the trade menu.
static void Task_TimeCapsuleLink(u8 taskId)
{
    if (!gTimeCapsuleActive || !GbProtocol_IsActive())
    {
        sLinkTaskId = LINK_TASK_ID_NONE;
        sLinkTaskRunning = FALSE;
        DestroyTask(taskId);
        return;
    }

    GbProtocol_Update();

    if (GbProtocol_IsFinished() || GbProtocol_IsError())
    {
        sLinkTaskId = LINK_TASK_ID_NONE;
        sLinkTaskRunning = FALSE;
        DestroyTask(taskId);
    }
}

// Ends the current `special` and returns to the script.  Only a clean session
// (link still up) keeps the GB link alive for the next step.
static void FinishSession(u8 taskId, u8 result)
{
    gSpecialVar_Result = result;
    if (result != TIME_CAPSULE_OK && result != TIME_CAPSULE_LINK_ESTABLISHED
     && result != TIME_CAPSULE_CONFIRMED)
    {
        GbProtocol_Shutdown();
        gTimeCapsuleActive = FALSE;
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

static void Task_TimeCapsuleConfirm(u8 taskId)
{
    s16 *data = gTasks[taskId].data;
    u8 result;

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
            FinishSession(taskId, TIME_CAPSULE_ERR_CANCELLED);
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
            FinishSession(taskId, TIME_CAPSULE_ERR_CANCELLED);
        }
        break;
    }
}

void TimeCapsule_TryConnect(void)
{
    u8 taskId;

    gTimeCapsuleMode = gSpecialVar_0x8004;
    gTimeCapsuleRoom = gSpecialVar_0x8005;
    gTimeCapsuleGeneration = TIME_CAPSULE_GEN_NONE;
    gTimeCapsuleActive = TRUE;

    // Kanto/Johto species are outside the Hoenn dex; the trade menu refuses
    // them unless the National Dex is enabled.
    if (!IsNationalPokedexEnabled())
        EnableNationalPokedex();

    GbProtocol_Init(gTimeCapsuleRoom, 0);

    if (!sLinkTaskRunning)
    {
        sLinkTaskId = CreateTask(Task_TimeCapsuleLink, 80);
        sLinkTaskRunning = TRUE;
    }

    taskId = CreateTask(Task_TimeCapsuleConnect, 80);
    gTasks[taskId].tState = 0;
    gTasks[taskId].tTimer = 0;
}

// Step 1: wait (with "Please wait." on screen) until the GB picks the same
// room.  This is the analogue of the Cable Club linkup, so the confirmation
// prompts come afterwards, exactly like in the GBA Cable Club.
static void Task_TimeCapsuleConnect(u8 taskId)
{
    u8 result;

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
        FinishSession(taskId, TIME_CAPSULE_ERR_CANCELLED);
        return;
    }
}

// Step 3: wait for the party block (normally already exchanged while the
// player was confirming).
void TimeCapsule_StartExchange(void)
{
    u8 taskId = CreateTask(Task_TimeCapsuleExchange, 80);

    gTasks[taskId].tState = 0;
}

static void Task_TimeCapsuleExchange(u8 taskId)
{
    u8 result;

    if (GbProtocol_IsReady())
    {
        gTimeCapsuleGeneration = GbProtocol_GetGeneration();
        FinishSession(taskId, TIME_CAPSULE_OK);
        return;
    }

    if (ProtocolFailed(&result))
    {
        FinishSession(taskId, result);
        return;
    }

    if (JOY_NEW(B_BUTTON))
    {
        FinishSession(taskId, TIME_CAPSULE_ERR_CANCELLED);
        return;
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

void TimeCapsule_GetGeneration(void)
{
    gSpecialVar_Result = gTimeCapsuleGeneration;
}

void TimeCapsule_StartBattle(void)
{
    // Colosseum is phase 2: it needs a full Gen II/Gen I battle engine.
    gSpecialVar_Result = TIME_CAPSULE_ERR_NOT_READY;
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
        WarpIntoMap();
        SetMainCallback2(CB2_ReturnToField);
        DestroyTask(taskId);
        break;
    }
}
