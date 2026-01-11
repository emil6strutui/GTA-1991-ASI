#include "CStoriesSprinting.h"
#include "CStoriesSprintingConfig.h"

#include <plugin.h>
#include <CPed.h>
#include <CPlayerPed.h>

using namespace plugin;
using namespace SprintingAddresses;

CStoriesSprintingConfig SprintConfig;

namespace CStoriesSprinting {

// ============================================================================
// PROCESSANIMGROUPS HOOK
// ============================================================================
// Hook into CPlayerPed::ProcessAnimGroups (0x6098F0) to add 1-handed weapons
// to the PLAYERBBBAT animation group (like baseball bat, shovel, etc.)

// Hook address - right before weapon type switch starts
constexpr uintptr_t HOOK_ADDR = 0x6099A2;
// Jump destination for PLAYERBBBAT case (sets edi = 0x3F)
constexpr uintptr_t PLAYERBBBAT_DEST = 0x609A0C;
// Continue to next check in original code
constexpr uintptr_t CONTINUE_ADDR = 0x6099A7;

// Weapon type IDs we want to add to PLAYERBBBAT group
constexpr int WEAPON_PISTOL = 22;
constexpr int WEAPON_PISTOL_SILENCED = 23;
constexpr int WEAPON_DESERT_EAGLE = 24;
constexpr int WEAPON_MICRO_UZI = 28;
constexpr int WEAPON_MP5 = 29;
constexpr int WEAPON_TEC9 = 32;

// Naked hook function - injects our weapon checks before the original switch
static void __declspec(naked) ProcessAnimGroupsHook() {
    __asm {
        // edi contains the weapon type at this point
        // Check our 1-handed guns FIRST (before original checks)

        cmp edi, WEAPON_PISTOL           // 22
        jz go_playerbbbat
        cmp edi, WEAPON_PISTOL_SILENCED  // 23
        jz go_playerbbbat
        cmp edi, WEAPON_DESERT_EAGLE     // 24
        jz go_playerbbbat
        cmp edi, WEAPON_MICRO_UZI        // 28
        jz go_playerbbbat
        cmp edi, WEAPON_MP5              // 29
        jz go_playerbbbat
        cmp edi, WEAPON_TEC9             // 32
        jz go_playerbbbat

        // Not our weapon - execute original check (cmp edi, 5 = BASEBALLBAT)
        cmp edi, 5
        jz go_playerbbbat

        // Continue to next original comparison (cmp edi, 6 = SHOVEL)
        push CONTINUE_ADDR
        ret

    go_playerbbbat:
        // Jump to the PLAYERBBBAT case handler
        push PLAYERBBBAT_DEST
        ret
    }
}

// Install the ProcessAnimGroups hook
static void InstallAnimGroupHook() {
    // Replace the first instruction of the weapon switch (cmp edi, 5; jz ...)
    // with a jump to our hook. We need 5 bytes minimum for a JMP.
    // Original bytes at 0x6099A2: 83 FF 05 (cmp edi, 5) + 74 65 (jz +0x65)
    injector::MakeJMP(HOOK_ADDR, (void*)ProcessAnimGroupsHook, true);
}

static void PatchRocketSprint() {
    if (!SprintConfig.enableRocketSprint) return;

    // ROCKET LAUNCHER: Sprint -> LIMP
    patch::SetInt(ROCKET_SPRINT_NORMAL, STR_LIMP);
    patch::SetInt(ROCKET_SPRINT_FAT,    STR_LIMP);
    patch::SetInt(ROCKET_SPRINT_MUSCLE, STR_LIMP);
}

static void PatchArmedSprint() {
    if (!SprintConfig.enableArmedSprint) return;

    // ARMED (2-handed): Sprint -> walkst_csaw_right
    patch::SetInt(ARMED_SPRINT_NORMAL,  STR_WALKST_CSAW_R);
    patch::SetInt(ARMED_SPRINT_FAT,     STR_WALKST_CSAW_R);
    patch::SetInt(ARMED_SPRINT_MUSCLE,  STR_WALKST_CSAW_R);
}

static void Patch1HandedSprint() {
    if (!SprintConfig.enable1HandedSprint) return;

    // 1-HANDED ARMED: Sprint -> proper sprint animations
    patch::SetInt(ARMED1_SPRINT_NORMAL, STR_SPRINT_CIVI);
    patch::SetInt(ARMED1_SPRINT_FAT,    STR_FATSPRINT);
    patch::SetInt(ARMED1_SPRINT_MUSCLE, STR_MUSCLESPRINT);
}

static void PatchChainsawSprint() {
    if (!SprintConfig.enableChainsawSprint) return;

    // CHAINSAW: Sprint -> run_csaw_right
    patch::SetInt(CSAW_SPRINT_NORMAL,   STR_RUN_CSAW_R);
    patch::SetInt(CSAW_SPRINT_FAT,      STR_RUN_CSAW_R);
    patch::SetInt(CSAW_SPRINT_MUSCLE,   STR_RUN_CSAW_R);
}

static void Patch1HandedRun() {
    if (!SprintConfig.enable1HandedRun) return;

    // 1-HANDED ARMED: Run -> walk_csaw_right
    patch::SetInt(ARMED1_RUN_NORMAL,    STR_WALK_CSAW_R);
    patch::SetInt(ARMED1_RUN_FAT,       STR_WALK_CSAW_R);
    patch::SetInt(ARMED1_RUN_MUSCLE,    STR_WALK_CSAW_R);
}

static void PatchFatMuscleIdle() {
    if (!SprintConfig.enableFatMuscleIdle) return;

    // FAT IDLE SLOTS -> roadcross
    patch::SetInt(FAT_IDLE_1, STR_ROADCROSS);
    patch::SetInt(FAT_IDLE_2, STR_ROADCROSS);
    patch::SetInt(FAT_IDLE_3, STR_ROADCROSS);
    patch::SetInt(FAT_IDLE_4, STR_ROADCROSS);
    patch::SetInt(FAT_IDLE_5, STR_ROADCROSS);

    // MUSCLE IDLE SLOTS -> roadcross
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
    // Hook CPlayerPed::ProcessAnimGroups to add 1-handed weapons to PLAYERBBBAT group
    if (SprintConfig.enable1HandedAnimGroup) {
        InstallAnimGroupHook();
    }

    // Static animation patches (data section, no timing needed)
    // These modify which animations are used for each group
    PatchRocketSprint();
    PatchArmedSprint();
    Patch1HandedSprint();
    PatchChainsawSprint();
    Patch1HandedRun();
    PatchFatMuscleIdle();
}

}
