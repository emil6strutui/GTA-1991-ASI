#pragma once

// ============================================================================
// STORIES SPRINTING CONFIGURATION
// Animation patches for GTA Stories-style sprinting behavior
// Credits: All animations for Sprinting - SlingShot753
// ============================================================================

struct CStoriesSprintingConfig {
    // ---- Feature toggles ----
    bool enable1HandedAnimGroup = true;  // Use PLAYERBBBAT group for pistols/SMGs (hook)
    bool enableRocketSprint = true;      // Rocket launcher sprint animation
    bool enableArmedSprint = true;       // 2-handed weapon sprint animation
    bool enable1HandedSprint = true;     // 1-handed weapon sprint animation
    bool enableChainsawSprint = true;    // Chainsaw sprint animation
    bool enable1HandedRun = true;        // 1-handed weapon run animation
    bool enableFatMuscleIdle = true;     // Fat/muscle body idle animations
};

extern CStoriesSprintingConfig SprintConfig;

// ============================================================================
// ANIMATION STRING ADDRESSES (from .rdata section)
// These point to animation names in the game's read-only data
// ============================================================================
// Required animations to add:
// - inside anim/ped.ifp
// - inside muscular.ifp and fat.ifp from gta3.img

namespace SprintingAddresses {

// Animation name strings
constexpr int STR_LIMP             = 0x85D24C;  // "LIMP"
constexpr int STR_WALKST_CSAW_R    = 0x85D3A8;  // "walkst_csaw_right"
constexpr int STR_SPRINT_CIVI      = 0x85D938;  // "SPRINT_civi"
constexpr int STR_FATSPRINT        = 0x85D8F8;  // "fatsprint"
constexpr int STR_MUSCLESPRINT     = 0x85D8B8;  // "musclesprint"
constexpr int STR_RUN_CSAW_R       = 0x85D3BC;  // "run_csaw_right"
constexpr int STR_WALK_CSAW_R      = 0x85D3CC;  // "walk_csaw_right"
constexpr int STR_ROADCROSS        = 0x85D920;  // "roadcross"

// ============================================================================
// ANIMATION SLOT ADDRESSES IN ms_aAnimAssocDefinitions
// ============================================================================

// --- ROCKET LAUNCHER SPRINT SLOTS (run_rocket -> LIMP) ---
constexpr int ROCKET_SPRINT_NORMAL = 0x8A8AF4;
constexpr int ROCKET_SPRINT_FAT    = 0x8A8B0C;
constexpr int ROCKET_SPRINT_MUSCLE = 0x8A8B24;

// --- ARMED (2-handed) SPRINT SLOTS (run_armed -> walkst_csaw_right) ---
constexpr int ARMED_SPRINT_NORMAL  = 0x8A8B3C;
constexpr int ARMED_SPRINT_FAT     = 0x8A8B54;
constexpr int ARMED_SPRINT_MUSCLE  = 0x8A8B6C;

// --- 1-HANDED ARMED SPRINT SLOTS (run_player -> proper sprint anims) ---
constexpr int ARMED1_SPRINT_NORMAL = 0x8A8B84;  // -> SPRINT_civi
constexpr int ARMED1_SPRINT_FAT    = 0x8A8B9C;  // -> fatsprint
constexpr int ARMED1_SPRINT_MUSCLE = 0x8A8BB4;  // -> musclesprint

// --- CHAINSAW SPRINT SLOTS (run_csaw -> run_csaw_right) ---
constexpr int CSAW_SPRINT_NORMAL   = 0x8A8BCC;
constexpr int CSAW_SPRINT_FAT      = 0x8A8BE4;
constexpr int CSAW_SPRINT_MUSCLE   = 0x8A8BFC;

// --- 1-HANDED ARMED RUN SLOTS (run_player -> walk_csaw_right) ---
constexpr int ARMED1_RUN_NORMAL    = 0x8A8B80;
constexpr int ARMED1_RUN_FAT       = 0x8A8B98;
constexpr int ARMED1_RUN_MUSCLE    = 0x8A8BB0;

// --- IDLE SLOTS -> roadcross (various weapon groups, fat/muscle bodies) ---
constexpr int FAT_IDLE_1           = 0x8A8ACC;  // Player fat
constexpr int FAT_IDLE_2           = 0x8A8B14;  // Rocket fat
constexpr int FAT_IDLE_3           = 0x8A8B5C;  // Armed fat
constexpr int FAT_IDLE_4           = 0x8A8BA4;  // 1Armed fat
constexpr int FAT_IDLE_5           = 0x8A8BEC;  // Chainsaw fat

constexpr int MUSCLE_IDLE_1        = 0x8A8AE4;  // Player muscle
constexpr int MUSCLE_IDLE_2        = 0x8A8B2C;  // Rocket muscle
constexpr int MUSCLE_IDLE_3        = 0x8A8B74;  // Armed muscle
constexpr int MUSCLE_IDLE_4        = 0x8A8BBC;  // 1Armed muscle
constexpr int MUSCLE_IDLE_5        = 0x8A8C04;  // Chainsaw muscle

}
