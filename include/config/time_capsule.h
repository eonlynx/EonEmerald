#ifndef GUARD_CONFIG_TIME_CAPSULE_H
#define GUARD_CONFIG_TIME_CAPSULE_H

// Time Capsule (GBA <-> Gen I/II link) configuration.

// Debug logging of TX/RX bytes. Keep FALSE for release builds.
#define TIME_CAPSULE_DEBUG_LOG             TRUE

// Met location used for Pokémon received from the past.
#define TIME_CAPSULE_DEFAULT_MET_LOCATION  0xFE // METLOC_IN_GAME_TRADE

// The Gen I/II link protocol does not transmit the trainer's gender (only the
// name and the trainer ID of the party owner), so the player of the other game
// is drawn with a fixed sprite in the trade room:
//   FALSE -> OBJ_EVENT_GFX_RED  (the Gen I protagonist)
//   TRUE  -> OBJ_EVENT_GFX_LEAF (the Gen II protagonist)
#define TIME_CAPSULE_PARTNER_IS_FEMALE     FALSE

#endif // GUARD_CONFIG_TIME_CAPSULE_H
