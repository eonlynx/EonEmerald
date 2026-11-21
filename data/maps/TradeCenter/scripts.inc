TradeCenter_MapScripts::
	map_script MAP_SCRIPT_ON_WARP_INTO_MAP_TABLE, TradeCenter_OnWarpIntoMap
	.byte 0

@ The friend of the Time Capsule sits at the other console of the trade room,
@ but only during a Time Capsule session: in the normal Cable Club the seat
@ has to stay free.
@ This is a table of (var, value, script) entries.  It runs once, after the
@ object events of the map have been spawned and before the first frame, and
@ it runs immediately (unlike the on-frame table, it does not lock the player).
TradeCenter_OnWarpIntoMap:
	map_script_2 VAR_CABLE_CLUB_STATE, USING_TIME_CAPSULE_TRADE, TradeCenter_EventScript_PartnerHere
	map_script_2 VAR_CABLE_CLUB_STATE, USING_TRADE_CENTER, TradeCenter_EventScript_PartnerGone
	map_script_2 VAR_CABLE_CLUB_STATE, 0, TradeCenter_EventScript_PartnerGone
	.2byte 0

TradeCenter_EventScript_PartnerHere::
	showobjectat LOCALID_TRADE_CENTER_PARTNER, MAP_TRADE_CENTER
	end

TradeCenter_EventScript_PartnerGone::
	hideobjectat LOCALID_TRADE_CENTER_PARTNER, MAP_TRADE_CENTER
	end
