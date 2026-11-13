Common_EventScript_TimeCapsuleAttendant::
	lock
	faceplayer
	goto_if_unset FLAG_SYS_POKEDEX_GET, TimeCapsule_EventScript_Closed
	message TimeCapsule_Text_Intro
	waitmessage
	message CableClub_Text_WhichService
	waitmessage
	delay 28
	multichoice 0, 0, MULTI_CABLE_CLUB_NO_RECORD_MIX, FALSE
	switch VAR_RESULT
	case 0, TimeCapsule_EventScript_Trade
	case 1, TimeCapsule_EventScript_Battle
	case 2, TimeCapsule_EventScript_Exit
	case MULTI_B_PRESSED, TimeCapsule_EventScript_Exit
	end

TimeCapsule_EventScript_Closed::
	msgbox TimeCapsule_Text_Closed, MSGBOX_DEFAULT
	release
	end

TimeCapsule_EventScript_Exit::
	msgbox CableClub_Text_PleaseVisitAgain, MSGBOX_DEFAULT
	release
	end

TimeCapsule_EventScript_Battle::
	special TimeCapsule_StartBattle
	msgbox gText_UndergoingAdjustments, MSGBOX_DEFAULT
	goto TimeCapsule_EventScript_Abort

TimeCapsule_EventScript_Trade::
	setvar VAR_0x8005, TIME_CAPSULE_ROOM_TRADE
	goto TimeCapsule_EventScript_Connect

TimeCapsule_EventScript_Connect::
	specialvar VAR_RESULT, CalculatePlayerPartyCount
	goto_if_lt VAR_RESULT, 2, TimeCapsule_EventScript_NeedTwoMons
	call Common_EventScript_SaveGame
	goto_if_eq VAR_RESULT, 0, TimeCapsule_EventScript_Abort
	message gText_PleaseWaitForLink
	waitmessage
	special TimeCapsule_TryConnect
	closemessage
	switch VAR_RESULT
	case TIME_CAPSULE_LINK_ESTABLISHED, TimeCapsule_EventScript_Confirm
	case TIME_CAPSULE_ERR_CANCELLED, TimeCapsule_EventScript_Abort
	case TIME_CAPSULE_ERR_NO_LINK, TimeCapsule_EventScript_NoLink
	case TIME_CAPSULE_ERR_TIMEOUT, TimeCapsule_EventScript_Timeout
	case TIME_CAPSULE_ERR_WRONG_ROOM, TimeCapsule_EventScript_WrongRoom
	goto TimeCapsule_EventScript_Abort
	end

TimeCapsule_EventScript_Confirm::
	special TimeCapsule_Confirm
	switch VAR_RESULT
	case TIME_CAPSULE_CONFIRMED, TimeCapsule_EventScript_EnterRoom
	case TIME_CAPSULE_ERR_CANCELLED, TimeCapsule_EventScript_Abort
	case TIME_CAPSULE_ERR_NO_LINK, TimeCapsule_EventScript_NoLink
	case TIME_CAPSULE_ERR_TIMEOUT, TimeCapsule_EventScript_Timeout
	case TIME_CAPSULE_ERR_WRONG_ROOM, TimeCapsule_EventScript_WrongRoom
	goto TimeCapsule_EventScript_Abort
	end

TimeCapsule_EventScript_AwaitLinkup::
	@ The player sits down at the console and waits for the other player, so
	@ the exchange task shows "PLEASE WAIT FOR LINK..." (like the Cable Club)
	@ and turns the sprite towards the machine.
	special TimeCapsule_StartExchange
	closemessage
	switch VAR_RESULT
	case TIME_CAPSULE_OK, TimeCapsule_EventScript_CheckParty
	case TIME_CAPSULE_ERR_CANCELLED, TimeCapsule_EventScript_Abort
	case TIME_CAPSULE_ERR_NO_LINK, TimeCapsule_EventScript_NoLink
	case TIME_CAPSULE_ERR_TIMEOUT, TimeCapsule_EventScript_Timeout
	case TIME_CAPSULE_ERR_WRONG_ROOM, TimeCapsule_EventScript_WrongRoom
	case TIME_CAPSULE_ERR_SPECIES, TimeCapsule_EventScript_Timeout
	goto TimeCapsule_EventScript_Abort
	end

@ The friend of the other game, sitting at the other console of the trade
@ room.  It is busy with its own trade, so the vanilla Cable Club message for
@ a busy partner is used.
TimeCapsule_EventScript_Partner::
	goto_if_ne VAR_CABLE_CLUB_STATE, USING_TIME_CAPSULE_TRADE, TimeCapsule_EventScript_PartnerNone
	msgbox CableClub_Text_TooBusyToNotice, MSGBOX_DEFAULT
	closemessage
	end

TimeCapsule_EventScript_PartnerNone:
	end

@ The player wants to leave the trade room.  The vanilla Cable Club asks for a
@ confirmation before it terminates the link.
TimeCapsule_EventScript_ConfirmLeaveLinkRoom::
	msgbox Text_TerminateLinkConfirmation, MSGBOX_YESNO
	goto_if_eq VAR_RESULT, YES, TimeCapsule_EventScript_TerminateLink
	erasebox 0, 0, 29, 19
	goto TimeCapsule_EventScript_AwaitLinkup
	end

@ The link is terminated: the other game left the room or the player asked
@ for it.  The other game is told with the Gen I/II exit byte (0xF) and the
@ player is escorted out of the room, like the vanilla Cable Club does.
TimeCapsule_EventScript_TerminateLink::
	messageautoscroll Text_TerminateLinkPleaseWait
	waitmessage
	special TimeCapsule_CloseLink
	goto EventScript_DoLinkRoomExit

TimeCapsule_EventScript_CheckParty::
	special TimeCapsule_CheckParty
	switch VAR_RESULT
	case TIME_CAPSULE_OK, TimeCapsule_EventScript_TakeSeat
	case TIME_CAPSULE_ERR_SPECIES, TimeCapsule_EventScript_BadSpecies
	case TIME_CAPSULE_ERR_MOVE, TimeCapsule_EventScript_BadMove
	case TIME_CAPSULE_ERR_MAIL, TimeCapsule_EventScript_BadMail
	case TIME_CAPSULE_ERR_EGG, TimeCapsule_EventScript_BadEgg
	case TIME_CAPSULE_ERR_BAD_EGG, TimeCapsule_EventScript_BadEgg
	case TIME_CAPSULE_ERR_SHINY, TimeCapsule_EventScript_BadShiny
	goto TimeCapsule_EventScript_BadSpecies
	end

// The player has walked to one of the chairs in the trade room.  This is the
// GBA equivalent of pressing A at the Cable Club console on the GB side: from
// here the trade menu takes over, exactly like in a normal Cable Club trade.
TimeCapsule_EventScript_TakeSeat::
	special PlayerEnteredTradeSeat
	end

TimeCapsule_EventScript_BadSpecies::
	special TimeCapsule_CloseLink
	msgbox TimeCapsule_Text_RejectSpecies, MSGBOX_DEFAULT
	goto TimeCapsule_EventScript_Abort

TimeCapsule_EventScript_BadMove::
	special TimeCapsule_CloseLink
	msgbox TimeCapsule_Text_RejectMove, MSGBOX_DEFAULT
	goto TimeCapsule_EventScript_Abort

TimeCapsule_EventScript_BadMail::
	special TimeCapsule_CloseLink
	msgbox TimeCapsule_Text_RejectMail, MSGBOX_DEFAULT
	goto TimeCapsule_EventScript_Abort

TimeCapsule_EventScript_BadEgg::
	special TimeCapsule_CloseLink
	msgbox TimeCapsule_Text_RejectEgg, MSGBOX_DEFAULT
	goto TimeCapsule_EventScript_Abort

TimeCapsule_EventScript_BadShiny::
	special TimeCapsule_CloseLink
	msgbox TimeCapsule_Text_RejectShiny, MSGBOX_DEFAULT
	goto TimeCapsule_EventScript_Abort

TimeCapsule_EventScript_NoLink::
	msgbox CableClub_Text_OtherTrainerNotReady, MSGBOX_DEFAULT
	goto TimeCapsule_EventScript_LeaveRoomOrEnd

TimeCapsule_EventScript_Timeout::
	msgbox Text_LinkErrorPleaseReset, MSGBOX_DEFAULT
	goto TimeCapsule_EventScript_LeaveRoomOrEnd

TimeCapsule_EventScript_WrongRoom::
	msgbox Text_PlayersMadeDifferentSelections, MSGBOX_DEFAULT
	goto TimeCapsule_EventScript_LeaveRoomOrEnd

TimeCapsule_EventScript_NeedTwoMons::
	msgbox CableClub_Text_NeedTwoMonsToTrade, MSGBOX_DEFAULT
	goto TimeCapsule_EventScript_Abort

TimeCapsule_EventScript_Abort::
	msgbox CableClub_Text_PleaseVisitAgain, MSGBOX_DEFAULT
	goto TimeCapsule_EventScript_LeaveRoomOrEnd

// Once the player is inside the trade room a failure must warp back to the
// Cable Club and close the barrier, exactly like the normal Cable Club exit.
// Before the warp the NPC script simply releases the player.
TimeCapsule_EventScript_LeaveRoomOrEnd::
	goto_if_ne VAR_CABLE_CLUB_STATE, USING_TIME_CAPSULE_TRADE, TimeCapsule_EventScript_ReleaseAndEnd
	special CleanupLinkRoomState
	special ReturnFromLinkRoom
	end

TimeCapsule_EventScript_ReleaseAndEnd::
	release
	end

TimeCapsule_EventScript_EnterRoom::
	messageautoscroll CableClub_Text_PleaseEnter
	waitmessage
	copyvar VAR_0x8007, VAR_LAST_TALKED
	setvar VAR_0x8004, USING_TIME_CAPSULE_TRADE
	call CableClub_EventScript_OpenDirectCornerBarrier
	special DrawWholeMapView
	playse SE_CLICK
	delay 60
	applymovement VAR_LAST_TALKED, Movement_AttendantFaceLeft
	waitmovement 0
	closemessage
	applymovement LOCALID_PLAYER, TimeCapsule_Movement_PlayerApproachLinkRoom
	waitmovement 0
	opendoor 9, 1
	waitdooranim
	applymovement LOCALID_PLAYER, Movement_PlayerEnterLinkRoom
	waitmovement 0
	hideplayer
	closedoor 9, 1
	waitdooranim
	release
	special SetCableClubWarp
	setwarp MAP_TRADE_CENTER, 5, 8
	special TimeCapsule_DoWarp
	end

@ The time capsule attendant stands one tile to the right of the Cable Club
@ attendant, so the player starts one tile further right than in the vanilla
@ script and has to take one extra step left to line up with the door.
TimeCapsule_Movement_PlayerApproachLinkRoom:
	walk_left
	walk_left
	walk_up
	walk_up
	step_end

@ The link is over: the other game left the room or the player asked for it.
@ This mirrors the vanilla Cable Club exit from the trade center: the trainer
@ card data the link overwrote is restored, the player is escorted out of the
@ room and the barrier of the Direct Corner is closed again.
TimeCapsule_EventScript_ExitLinkRoom::
	lockall
	call TimeCapsule_EventScript_PlayerExitTradeCenter
	call CableClub_EventScript_CloseDirectCornerBarrier
	special DrawWholeMapView
	playse SE_CLICK
	erasebox 0, 0, 29, 19
	releaseall
	end

TimeCapsule_EventScript_PlayerExitTradeCenter::
	special TimeCapsule_CloseLink
	setvar VAR_CABLE_CLUB_STATE, 0
	goto_if_eq VAR_0x8007, 0, TimeCapsule_EventScript_PlayerExitLinkRoom
	applymovement LOCALID_PLAYER, Movement_PlayerFaceAttendantRight
	waitmovement 0
	applymovement VAR_0x8007, Movement_AttendantFaceLeft
	waitmovement 0
	message CableClub_Text_TrainerCardDataOverwritten
	waitmessage
	playse SE_PIN
	delay 60
	message CableClub_Text_HopeToSeeYouAgain
	waitmessage
	applymovement LOCALID_PLAYER, Movement_PlayerExitLinkRoom
	waitmovement 0
	applymovement VAR_0x8007, Movement_AttendantFaceDown
	waitmovement 0
	return

TimeCapsule_EventScript_PlayerExitLinkRoom::
	message CableClub_Text_HopeToSeeYouAgain
	waitmessage
	applymovement LOCALID_PLAYER, Movement_PlayerExitLinkRoom
	waitmovement 0
	return
