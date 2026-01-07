#include "AnimPatches.h"

#include <plugin.h>
#include <Events.h>

using namespace plugin;

//Credits:

//All animations for Sprinting- SlingShot753
namespace AnimPatches {

// ============================================================================
// ANIMATION STRING ADDRESSES (from .rdata section)
// ============================================================================
// need to add these:
// - inside anim/ped.ifp
// - inside muscular.ifp and fat.ifp from gta3.img
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

// --- ROCKET LAUNCHER SPRINT SLOTS (run_rocket → LIMP) ---
constexpr int ROCKET_SPRINT_NORMAL = 0x8A8AF4;
constexpr int ROCKET_SPRINT_FAT    = 0x8A8B0C;
constexpr int ROCKET_SPRINT_MUSCLE = 0x8A8B24;

// --- ARMED (2-handed) SPRINT SLOTS (run_armed → walkst_csaw_right) ---
constexpr int ARMED_SPRINT_NORMAL  = 0x8A8B3C;
constexpr int ARMED_SPRINT_FAT     = 0x8A8B54;
constexpr int ARMED_SPRINT_MUSCLE  = 0x8A8B6C;

// --- 1-HANDED ARMED SPRINT SLOTS (run_player → proper sprint anims) ---
constexpr int ARMED1_SPRINT_NORMAL = 0x8A8B84;  // → SPRINT_civi
constexpr int ARMED1_SPRINT_FAT    = 0x8A8B9C;  // → fatsprint
constexpr int ARMED1_SPRINT_MUSCLE = 0x8A8BB4;  // → musclesprint

// --- CHAINSAW SPRINT SLOTS (run_csaw → run_csaw_right) ---
constexpr int CSAW_SPRINT_NORMAL   = 0x8A8BCC;
constexpr int CSAW_SPRINT_FAT      = 0x8A8BE4;
constexpr int CSAW_SPRINT_MUSCLE   = 0x8A8BFC;

// --- 1-HANDED ARMED RUN SLOTS (run_player → walk_csaw_right) ---
constexpr int ARMED1_RUN_NORMAL    = 0x8A8B80;
constexpr int ARMED1_RUN_FAT       = 0x8A8B98;
constexpr int ARMED1_RUN_MUSCLE    = 0x8A8BB0;

// --- IDLE SLOTS → roadcross (various weapon groups, fat/muscle bodies) ---
// Fat body idle slots
constexpr int FAT_IDLE_1           = 0x8A8ACC;  // Player fat
constexpr int FAT_IDLE_2           = 0x8A8B14;  // Rocket fat
constexpr int FAT_IDLE_3           = 0x8A8B5C;  // Armed fat
constexpr int FAT_IDLE_4           = 0x8A8BA4;  // 1Armed fat
constexpr int FAT_IDLE_5           = 0x8A8BEC;  // Chainsaw fat

// Muscle body idle slots
constexpr int MUSCLE_IDLE_1        = 0x8A8AE4;  // Player muscle
constexpr int MUSCLE_IDLE_2        = 0x8A8B2C;  // Rocket muscle
constexpr int MUSCLE_IDLE_3        = 0x8A8B74;  // Armed muscle
constexpr int MUSCLE_IDLE_4        = 0x8A8BBC;  // 1Armed muscle
constexpr int MUSCLE_IDLE_5        = 0x8A8C04;  // Chainsaw muscle

// ============================================================================
// PATCH FUNCTIONS
// ============================================================================

static void PatchAllAnimations() {
    // ========================================
    // ROCKET LAUNCHER: Sprint → LIMP
    // ========================================
    patch::SetInt(ROCKET_SPRINT_NORMAL, STR_LIMP);
    patch::SetInt(ROCKET_SPRINT_FAT,    STR_LIMP);
    patch::SetInt(ROCKET_SPRINT_MUSCLE, STR_LIMP);

    // ========================================
    // ARMED (2-handed): Sprint → walkst_csaw_right
    // ========================================
    patch::SetInt(ARMED_SPRINT_NORMAL,  STR_WALKST_CSAW_R);
    patch::SetInt(ARMED_SPRINT_FAT,     STR_WALKST_CSAW_R);
    patch::SetInt(ARMED_SPRINT_MUSCLE,  STR_WALKST_CSAW_R);

    // ========================================
    // 1-HANDED ARMED: Sprint → proper sprint animations
    // ========================================
    patch::SetInt(ARMED1_SPRINT_NORMAL, STR_SPRINT_CIVI);
    patch::SetInt(ARMED1_SPRINT_FAT,    STR_FATSPRINT);
    patch::SetInt(ARMED1_SPRINT_MUSCLE, STR_MUSCLESPRINT);

    // ========================================
    // CHAINSAW: Sprint → run_csaw_right
    // ========================================
    patch::SetInt(CSAW_SPRINT_NORMAL,   STR_RUN_CSAW_R);
    patch::SetInt(CSAW_SPRINT_FAT,      STR_RUN_CSAW_R);
    patch::SetInt(CSAW_SPRINT_MUSCLE,   STR_RUN_CSAW_R);

    // ========================================
    // 1-HANDED ARMED: Run → walk_csaw_right
    // ========================================
    patch::SetInt(ARMED1_RUN_NORMAL,    STR_WALK_CSAW_R);
    patch::SetInt(ARMED1_RUN_FAT,       STR_WALK_CSAW_R);
    patch::SetInt(ARMED1_RUN_MUSCLE,    STR_WALK_CSAW_R);

    // ========================================
    // FAT/MUSCLE IDLE SLOTS → roadcross
    // ========================================
    // Fat body
    patch::SetInt(FAT_IDLE_1, STR_ROADCROSS);
    patch::SetInt(FAT_IDLE_2, STR_ROADCROSS);
    patch::SetInt(FAT_IDLE_3, STR_ROADCROSS);
    patch::SetInt(FAT_IDLE_4, STR_ROADCROSS);
    patch::SetInt(FAT_IDLE_5, STR_ROADCROSS);

    // Muscle body
    patch::SetInt(MUSCLE_IDLE_1, STR_ROADCROSS);
    patch::SetInt(MUSCLE_IDLE_2, STR_ROADCROSS);
    patch::SetInt(MUSCLE_IDLE_3, STR_ROADCROSS);
    patch::SetInt(MUSCLE_IDLE_4, STR_ROADCROSS);
    patch::SetInt(MUSCLE_IDLE_5, STR_ROADCROSS);
}

// ============================================================================
// INSTALL HOOKS
// ============================================================================

void InstallHooks() {
    // Patch animation table immediately (data section, no timing needed)
    PatchAllAnimations();
}

} // namespace AnimPatches
