Common_EventScript_TimeCapsuleAttendant::
	lock
	faceplayer
	goto_if_unset FLAG_SYS_POKEDEX_GET, TimeCapsule_EventScript_Closed
	message TimeCapsule_Text_Intro
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
	msgbox TimeCapsule_Text_ColosseumNotReady, MSGBOX_DEFAULT
	goto TimeCapsule_EventScript_Abort

TimeCapsule_EventScript_Trade::
	setvar VAR_0x8004, TIME_CAPSULE_MODE_TRADE
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
	case TIME_CAPSULE_CONFIRMED, TimeCapsule_EventScript_AwaitLinkup
	case TIME_CAPSULE_ERR_CANCELLED, TimeCapsule_EventScript_Abort
	case TIME_CAPSULE_ERR_NO_LINK, TimeCapsule_EventScript_NoLink
	case TIME_CAPSULE_ERR_TIMEOUT, TimeCapsule_EventScript_Timeout
	case TIME_CAPSULE_ERR_WRONG_ROOM, TimeCapsule_EventScript_WrongRoom
	goto TimeCapsule_EventScript_Abort
	end

TimeCapsule_EventScript_AwaitLinkup::
	message gText_AwaitingLinkup
	waitmessage
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

TimeCapsule_EventScript_CheckParty::
	special TimeCapsule_CheckParty
	switch VAR_RESULT
	case TIME_CAPSULE_OK, TimeCapsule_EventScript_EnterRoom
	case TIME_CAPSULE_ERR_SPECIES, TimeCapsule_EventScript_BadSpecies
	case TIME_CAPSULE_ERR_MOVE, TimeCapsule_EventScript_BadMove
	case TIME_CAPSULE_ERR_MAIL, TimeCapsule_EventScript_BadMail
	case TIME_CAPSULE_ERR_EGG, TimeCapsule_EventScript_BadEgg
	case TIME_CAPSULE_ERR_BAD_EGG, TimeCapsule_EventScript_BadEgg
	case TIME_CAPSULE_ERR_SHINY, TimeCapsule_EventScript_BadShiny
	goto TimeCapsule_EventScript_BadSpecies
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
	release
	end

TimeCapsule_EventScript_Timeout::
	msgbox Text_LinkErrorPleaseReset, MSGBOX_DEFAULT
	release
	end

TimeCapsule_EventScript_WrongRoom::
	msgbox Text_PlayersMadeDifferentSelections, MSGBOX_DEFAULT
	release
	end

TimeCapsule_EventScript_NeedTwoMons::
	msgbox CableClub_Text_NeedTwoMonsToTrade, MSGBOX_DEFAULT
	goto TimeCapsule_EventScript_Abort

TimeCapsule_EventScript_Abort::
	msgbox CableClub_Text_PleaseVisitAgain, MSGBOX_DEFAULT
	release
	end

TimeCapsule_EventScript_EnterRoom::
	special HealPlayerParty
	messageautoscroll CableClub_Text_PleaseEnter
	waitmessage
	setvar VAR_0x8004, USING_TIME_CAPSULE_TRADE
	copyvar VAR_CABLE_CLUB_STATE, VAR_0x8004
	call CableClub_EventScript_OpenDirectCornerBarrier
	special DrawWholeMapView
	playse SE_CLICK
	delay 60
	applymovement VAR_LAST_TALKED, Movement_AttendantFaceLeft
	waitmovement 0
	applymovement LOCALID_PLAYER, Movement_PlayerApproachLinkRoomLeft
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

TimeCapsule_EventScript_ExitLinkRoom::
	lockall
	special TimeCapsule_CloseLink
	setvar VAR_CABLE_CLUB_STATE, 0
	call CableClub_EventScript_CloseDirectCornerBarrier
	special DrawWholeMapView
	playse SE_CLICK
	applymovement LOCALID_PLAYER, Movement_PlayerExitLinkRoom
	waitmovement 0
	msgbox CableClub_Text_HopeToSeeYouAgain, MSGBOX_DEFAULT
	erasebox 0, 0, 29, 19
	releaseall
	end
