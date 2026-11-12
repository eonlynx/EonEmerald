#ifndef GUARD_CONFIG_TIME_CAPSULE_H
#define GUARD_CONFIG_TIME_CAPSULE_H

// Time Capsule (GBA <-> Gen I/II link) configuration.

// Master switch. When disabled, the NPC still exists but refuses to link.
#define TIME_CAPSULE_ENABLED               TRUE

// Generation profiles. Both are required for v1.
#define TIME_CAPSULE_SUPPORT_GEN1          TRUE
#define TIME_CAPSULE_SUPPORT_GEN2          TRUE

// Colosseum is phase 2 (it needs a full Gen II/Gen I battle engine).
// When FALSE, the NPC shows a "not ready" message instead of linking.
#define TIME_CAPSULE_COLOSSEUM_ENABLED     FALSE

// Korean Gen II uses a two-byte charset and is not supported in v1.
#define TIME_CAPSULE_ALLOW_KOREAN          FALSE

// Debug logging of TX/RX bytes. Keep FALSE for release builds.
#define TIME_CAPSULE_DEBUG_LOG             TRUE

// Save the GBA game before completing the trade success handshake.
#define TIME_CAPSULE_SAVE_BEFORE_TRADE     TRUE

// Met location used for Pokémon received from the past.
#define TIME_CAPSULE_DEFAULT_MET_LOCATION  0xFE // METLOC_IN_GAME_TRADE

#endif // GUARD_CONFIG_TIME_CAPSULE_H
