#include "CGrabSystem.h"
#include <plugin.h>
#include <CWorld.h>
#include <CPed.h>
#include <CPlayerPed.h>
#include <CPad.h>
#include <CTimer.h>
#include <CAnimManager.h>
#include <CAnimBlendAssociation.h>
#include <CAnimBlendHierarchy.h>
#include <CStreaming.h>
#include <CStreamingInfo.h>
#include <CPedIntelligence.h>
#include <CTaskManager.h>
#include <CTask.h>
#include <CTaskSimpleStandStill.h>
#include <ePedState.h>
#include <common.h>
#include <cstdio>

// ============================================================================
// DEBUG LOGGING
// ============================================================================
#define GRAB_DEBUG 1

#if GRAB_DEBUG
static FILE* g_debugLog = nullptr;

static void DebugLog(const char* fmt, ...) {
    if (!g_debugLog) {
        g_debugLog = fopen("grab_debug.log", "w");
    }
    if (g_debugLog) {
        va_list args;
        va_start(args, fmt);
        fprintf(g_debugLog, "[%u] ", CTimer::m_snTimeInMilliseconds);
        vfprintf(g_debugLog, fmt, args);
        fprintf(g_debugLog, "\n");
        fflush(g_debugLog);
        va_end(args);
    }
}
#else
#define DebugLog(...) ((void)0)
#endif

// ============================================================================
// APPROACH BASED ON GTA-REVERSED ANALYSIS:
//
// From studying TaskSimpleStealthKill, TaskSimpleBeHit, TaskSimpleArrestPed:
// 1. Animations are played with CAnimManager::BlendAnimation()
// 2. Finish callbacks track when animations complete (SetFinishCallback)
// 3. One ped's task can set tasks on another ped via GetTaskManager().SetTask()
// 4. Animation duration comes from CAnimBlendHierarchy::m_fTotalTime
// 5. Higher blend delta = faster blend in, overrides lower priority anims
// ============================================================================

// IFP model ID conversion (IFP resources start at 25575)
constexpr int IFPToModelId(int blockIndex) { return 25575 + blockIndex; }

// Helper to check if model is loaded
static bool IsModelLoaded(int modelId) {
    return CStreaming::ms_aInfoForModel[modelId].m_nLoadState == LOADSTATE_LOADED;
}

using namespace plugin;

CGrabConfig GrabConfig;

namespace CGrabSystem {

// ============================================================================
// INTERNAL STATE
// ============================================================================
namespace {
    eGrabState g_grabState = GRAB_STATE_NONE;
    CPed* g_pGrabbedPed = nullptr;
    unsigned int g_grabStartTime = 0;
    CAnimBlendAssociation* g_pPlayerAnim = nullptr;
    CAnimBlendAssociation* g_pVictimAnim = nullptr;
    CAnimBlock* g_pFightAnimBlock = nullptr;
    bool g_bKeyWasPressed = false;
    eGrabAction g_currentAction = GRAB_ACTION_NONE;

    // Track when animations finish via callbacks
    bool g_bPlayerAnimFinished = false;
    bool g_bVictimAnimFinished = false;
}

// Forward declarations
namespace Internal {
    void StartGrabAnimation();
    void StartHoldAnimation();
    void StartActionAnimation(eGrabAction action);
    void StartReleaseAnimation();
    CPed* FindGrabTarget(CPlayerPed* player);
    void UpdateGrabbedPedPosition();
    bool CheckVictimEscape();
    void ApplyGrabDamage(eGrabAction action);
}

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

static CPlayerPed* GetPlayer() {
    return FindPlayerPed(0);
}

static bool IsKeyJustPressed(char key) {
    return CPad::NewKeyState.standardKeys[key] && !CPad::OldKeyState.standardKeys[key];
}

static bool IsKeyPressed(char key) {
    return CPad::NewKeyState.standardKeys[key] != 0;
}

static float GetDistanceBetweenPeds(CPed* ped1, CPed* ped2) {
    CVector diff = ped1->GetPosition() - ped2->GetPosition();
    return diff.Magnitude();
}

static float GetAngleBetweenPeds(CPed* player, CPed* target) {
    CVector playerPos = player->GetPosition();
    CVector targetPos = target->GetPosition();
    CVector toTarget = targetPos - playerPos;

    float targetAngle = atan2f(-toTarget.x, toTarget.y);
    float playerHeading = player->m_fCurrentRotation;

    float angleDiff = targetAngle - playerHeading;

    while (angleDiff > 3.14159f) angleDiff -= 6.28318f;
    while (angleDiff < -3.14159f) angleDiff += 6.28318f;

    return fabsf(angleDiff) * (180.0f / 3.14159f);
}

static bool IsPedAlive(CPed* ped) {
    if (!ped) return false;
    if (ped->m_fHealth <= 0.0f) return false;
    if (ped->m_ePedState == PEDSTATE_DEAD) return false;
    if (ped->m_ePedState == PEDSTATE_DIE) return false;
    return true;
}

static bool CanGrabPed(CPed* target) {
    if (!target) return false;
    if (!IsPedAlive(target)) return false;
    if (target->IsPlayer()) return false;
    if (target->bInVehicle) return false;
    return true;
}

// ============================================================================
// ANIMATION LOADING
// ============================================================================

static int g_fightBlockIndex = -1;

static bool LoadGrabAnimations() {
    g_fightBlockIndex = CAnimManager::GetAnimationBlockIndex("fight_a");
    if (g_fightBlockIndex < 0) {
        return false;
    }

    int modelId = IFPToModelId(g_fightBlockIndex);

    if (!IsModelLoaded(modelId)) {
        CStreaming::RequestModel(modelId, KEEP_IN_MEMORY);
        CStreaming::LoadAllRequestedModels(false);
    }

    CAnimManager::AddAnimBlockRef(g_fightBlockIndex);
    g_pFightAnimBlock = CAnimManager::GetAnimationBlock("fight_a");

    return g_pFightAnimBlock != nullptr && g_pFightAnimBlock->bLoaded;
}

// ============================================================================
// ANIMATION CALLBACK SYSTEM
// Based on how gta-reversed's stealth kill and be-hit tasks work
// ============================================================================

// Callback when player animation finishes
static void PlayerAnimFinishedCB(CAnimBlendAssociation* anim, void* data) {
    DebugLog("PlayerAnimFinishedCB called - anim=%p (current=%p) currentTime=%.3f",
        anim, g_pPlayerAnim, anim ? anim->m_fCurrentTime : 0.0f);

    // Only mark finished if this callback is for our CURRENT animation
    // Old animations being blended out will also trigger callbacks - ignore those
    if (anim == g_pPlayerAnim) {
        DebugLog("  -> Player anim MATCHED, marking finished");
        g_bPlayerAnimFinished = true;
        g_pPlayerAnim = nullptr;
    } else {
        DebugLog("  -> Player anim MISMATCH, ignoring (old anim callback)");
    }
}

// Callback when victim animation finishes
static void VictimAnimFinishedCB(CAnimBlendAssociation* anim, void* data) {
    DebugLog("VictimAnimFinishedCB called - anim=%p (current=%p) currentTime=%.3f",
        anim, g_pVictimAnim, anim ? anim->m_fCurrentTime : 0.0f);

    // Only mark finished if this callback is for our CURRENT animation
    if (anim == g_pVictimAnim) {
        DebugLog("  -> Victim anim MATCHED, marking finished");
        g_bVictimAnimFinished = true;
        g_pVictimAnim = nullptr;
    } else {
        DebugLog("  -> Victim anim MISMATCH, ignoring (old anim callback)");
    }
}

// Get animation duration in milliseconds from hierarchy
static unsigned int GetAnimationDurationMs(CAnimBlendHierarchy* hier) {
    if (!hier) return 500; // Default fallback
    // m_fTotalTime is in seconds, convert to milliseconds
    return static_cast<unsigned int>(hier->m_fTotalTime * 1000.0f);
}

// ============================================================================
// ANIMATION FLAGS (from gta-reversed AnimBlendAssociation.h)
// ============================================================================
enum eAnimationFlags {
    ANIMATION_DEFAULT                = 0,
    ANIMATION_IS_PLAYING             = 1 << 0,  // 0x01
    ANIMATION_IS_LOOPED              = 1 << 1,  // 0x02
    ANIMATION_IS_BLEND_AUTO_REMOVE   = 1 << 2,  // 0x04
    ANIMATION_IS_FINISH_AUTO_REMOVE  = 1 << 3,  // 0x08
    ANIMATION_IS_PARTIAL             = 1 << 4,  // 0x10
    ANIMATION_IS_SYNCRONISED         = 1 << 5,  // 0x20
    ANIMATION_CAN_EXTRACT_VELOCITY   = 1 << 6,  // 0x40
    ANIMATION_CAN_EXTRACT_X_VELOCITY = 1 << 7,  // 0x80
    ANIMATION_SECONDARY_TASK_ANIM    = 1 << 12, // 0x1000
};

// ============================================================================
// CORE ANIMATION FUNCTIONS
// Based on gta-reversed's BlendAnimation approach with callbacks
// ============================================================================

// Play animation using direct BlendAnimation (like stealth kill does)
// Returns the animation association and sets up finish callback
static CAnimBlendAssociation* PlayAnimationWithCallback(
    CPed* ped,
    const char* animName,
    bool loop,
    float blendDelta,
    void (*callback)(CAnimBlendAssociation*, void*),
    void* callbackData
) {
    if (!ped || !ped->m_pRwClump || !g_pFightAnimBlock) {
        DebugLog("PlayAnim FAILED: ped=%p, clump=%p, block=%p", ped, ped ? ped->m_pRwClump : nullptr, g_pFightAnimBlock);
        return nullptr;
    }

    // Get animation hierarchy from our custom block
    CAnimBlendHierarchy* hier = CAnimManager::GetAnimation(animName, g_pFightAnimBlock);
    if (!hier) {
        DebugLog("PlayAnim FAILED: Animation '%s' not found in block", animName);
        return nullptr;
    }

    float animDuration = hier->m_fTotalTime;
    DebugLog("PlayAnim: '%s' duration=%.3fs loop=%d blendDelta=%.1f", animName, animDuration, loop, blendDelta);

    // Build flags
    int flags = ANIMATION_IS_PLAYING;
    if (loop) {
        flags |= ANIMATION_IS_LOOPED;
    }

    // Play the animation with high blend delta to override other anims
    CAnimBlendAssociation* anim = CAnimManager::BlendAnimation(
        ped->m_pRwClump,
        hier,
        flags,
        blendDelta
    );

    if (anim) {
        // Set finish callback to know when animation completes
        // This is how gta-reversed's tasks track animation completion
        anim->SetFinishCallback(callback, callbackData);

        // Force high blend amount to override any idle animations
        anim->m_fBlendAmount = 1.0f;

        // Ensure animation plays at normal speed
        anim->m_fSpeed = 1.0f;

        // Start from beginning
        anim->m_fCurrentTime = 0.0f;

        DebugLog("PlayAnim SUCCESS: anim=%p speed=%.2f blend=%.2f", anim, anim->m_fSpeed, anim->m_fBlendAmount);
    } else {
        DebugLog("PlayAnim FAILED: BlendAnimation returned nullptr");
    }

    return anim;
}

// ============================================================================
// VICTIM TASK MANAGEMENT
// Based on how TaskSimpleArrestPed manages the arrested ped
// ============================================================================

// Set up victim with a stand still task to prevent AI from moving
// Then play grabbed animation which overrides the idle anim
static void SetupVictimForGrab(CPed* victim) {
    if (!victim || !victim->m_pIntelligence) return;

    CTaskManager* taskMgr = &victim->m_pIntelligence->m_TaskMgr;

    // Clear ALL existing tasks first - this stops fighting, fleeing, etc.
    taskMgr->SetTask(nullptr, TASK_PRIMARY_PRIMARY, false);
    taskMgr->SetTask(nullptr, TASK_PRIMARY_EVENT_RESPONSE_TEMP, false);
    taskMgr->SetTask(nullptr, TASK_PRIMARY_EVENT_RESPONSE_NONTEMP, false);
    taskMgr->SetTask(nullptr, TASK_PRIMARY_PHYSICAL_RESPONSE, false);

    // Also clear secondary tasks
    taskMgr->SetTask(nullptr, TASK_SECONDARY_ATTACK, false);
    taskMgr->SetTask(nullptr, TASK_SECONDARY_DUCK, false);
    taskMgr->SetTask(nullptr, TASK_SECONDARY_PARTIAL_ANIM, false);
    victim->m_vecMoveSpeed.Set(0.0f, 0.0f, 0.0f);

    // Set victim state (like arrest does)
    victim->SetPedState(PEDSTATE_ARRESTED);

    // Set flags to prevent AI behavior
    victim->bStayInSamePlace = true;
    victim->bKindaStayInSamePlace = true;
    victim->bDontFight = true;
    victim->bIsBeingArrested = true;

    // Create stand still task - use FALSE for bUseAnimIdleStance
    // so it doesn't wait for idle to complete before finishing
    // Duration is very long since we control when grab ends
    CTaskSimpleStandStill* standTask = new CTaskSimpleStandStill(
        30000,  // 30 seconds max
        true,   // Looped
        false,  // DON'T use anim idle stance - critical!
        1.0f    // Low blend data so our grab anim overrides
    );

    // Set as primary task - this prevents victim's AI from taking over
    taskMgr->SetTask(standTask, TASK_PRIMARY_PRIMARY, false);
}

// Release victim from grab state
static void ReleaseVictimFromGrab(CPed* victim) {
    if (!victim) return;

    // Clear flags
    victim->bStayInSamePlace = false;
    victim->bKindaStayInSamePlace = false;
    victim->bDontFight = false;
    victim->bIsBeingArrested = false;

    // Clear our task
    if (victim->m_pIntelligence) {
        CTaskManager* taskMgr = &victim->m_pIntelligence->m_TaskMgr;
        taskMgr->SetTask(nullptr, TASK_PRIMARY_PRIMARY, false);
    }

    // Restore to idle state
    victim->SetPedState(PEDSTATE_IDLE);

    // Clear any remaining animation reference
    if (g_pVictimAnim) {
        g_pVictimAnim->m_fBlendDelta = -8.0f; // Blend out quickly
        g_pVictimAnim = nullptr;
    }
}

// ============================================================================
// PLAYER TASK MANAGEMENT
// ============================================================================

static void SetupPlayerForGrab(CPlayerPed* player) {
    if (!player || !player->m_pIntelligence) return;

    CTaskManager* taskMgr = &player->m_pIntelligence->m_TaskMgr;

    // Set attack state
    player->SetPedState(PEDSTATE_ATTACK);

    // NOTE: We deliberately do NOT set a CTaskSimpleStandStill task
    // This allows the player to rotate/aim with the mouse during grab
    // The animation will still play and override normal movement
    // We just clear secondary attack tasks to prevent combo punches
    taskMgr->SetTask(nullptr, TASK_SECONDARY_ATTACK, false);
    taskMgr->SetTask(nullptr, TASK_SECONDARY_DUCK, false);
}

static void ReleasePlayerFromGrab(CPlayerPed* player) {
    if (!player) return;

    // We didn't set a primary task, so nothing to clear
    // Just restore player state and blend out the grab animation
    player->SetPedState(PEDSTATE_IDLE);

    if (g_pPlayerAnim) {
        g_pPlayerAnim->m_fBlendDelta = -8.0f;
        g_pPlayerAnim = nullptr;
    }
}

// ============================================================================
// INTERNAL IMPLEMENTATION
// ============================================================================

namespace Internal {

CPed* FindGrabTarget(CPlayerPed* player) {
    if (!player) return nullptr;

    CVector playerPos = player->GetPosition();

    short numFound = 0;
    CEntity* entities[16];

    CWorld::FindObjectsInRange(
        playerPos,
        GrabConfig.grabRange,
        true,
        &numFound,
        16,
        entities,
        false, false, true, false, false
    );

    CPed* bestTarget = nullptr;
    float bestAngle = GrabConfig.grabAngle;

    for (int i = 0; i < numFound; i++) {
        CPed* ped = static_cast<CPed*>(entities[i]);

        if (!CanGrabPed(ped)) continue;
        if (ped == player) continue;

        float angle = GetAngleBetweenPeds(player, ped);
        if (angle < bestAngle) {
            bestAngle = angle;
            bestTarget = ped;
        }
    }

    return bestTarget;
}

void StartGrabAnimation() {
    CPlayerPed* player = GetPlayer();
    if (!player || !g_pGrabbedPed) return;

    // Reset animation finished flags
    g_bPlayerAnimFinished = false;
    g_bVictimAnimFinished = false;

    // Setup both peds (tasks and states)
    SetupPlayerForGrab(player);
    SetupVictimForGrab(g_pGrabbedPed);

    // Play grab initiation animations with callbacks
    // High blend delta (8.0) to quickly override any idle animations
    g_pPlayerAnim = PlayAnimationWithCallback(
        player,
        GrabAnims::GRAB_INIT,
        false,  // Not looped
        8.0f,   // Fast blend in
        PlayerAnimFinishedCB,
        nullptr
    );

    g_pVictimAnim = PlayAnimationWithCallback(
        g_pGrabbedPed,
        GrabAnims::GRABBED_INIT,
        false,
        8.0f,
        VictimAnimFinishedCB,
        nullptr
    );

    g_grabState = GRAB_STATE_INITIATING;
}

void StartHoldAnimation() {
    CPlayerPed* player = GetPlayer();
    if (!player || !g_pGrabbedPed) return;

    // Reset flags for new animations
    g_bPlayerAnimFinished = false;
    g_bVictimAnimFinished = false;

    // Play looped hold animations
    g_pPlayerAnim = PlayAnimationWithCallback(
        player,
        GrabAnims::GRAB_IDLE,
        true,   // Looped
        4.0f,
        PlayerAnimFinishedCB,
        nullptr
    );

    g_pVictimAnim = PlayAnimationWithCallback(
        g_pGrabbedPed,
        GrabAnims::GRABBED_IDLE,
        true,
        4.0f,
        VictimAnimFinishedCB,
        nullptr
    );

    g_grabState = GRAB_STATE_HOLDING;
}

void StartActionAnimation(eGrabAction action) {
    CPlayerPed* player = GetPlayer();
    if (!player || !g_pGrabbedPed) return;

    const char* actionName = "UNKNOWN";
    switch (action) {
        case GRAB_ACTION_JAB: actionName = "JAB"; break;
        case GRAB_ACTION_UPPERCUT: actionName = "UPPERCUT"; break;
        case GRAB_ACTION_THROW: actionName = "THROW"; break;
        case GRAB_ACTION_KNOCKOUT: actionName = "KNOCKOUT"; break;
        default: break;
    }
    DebugLog("StartActionAnimation: action=%s (%d)", actionName, action);

    g_currentAction = action;
    g_grabState = GRAB_STATE_PERFORMING;

    // Reset flags
    g_bPlayerAnimFinished = false;
    g_bVictimAnimFinished = false;

    const char* playerAnim = nullptr;
    const char* victimAnim = nullptr;

    switch (action) {
        case GRAB_ACTION_JAB:
            playerAnim = GrabAnims::GRAB_JAB;
            victimAnim = GrabAnims::GRABBED_JAB;
            break;
        case GRAB_ACTION_UPPERCUT:
            playerAnim = GrabAnims::GRAB_UPPERCUT;
            victimAnim = GrabAnims::GRABBED_UPPERCUT;
            break;
        case GRAB_ACTION_THROW:
            playerAnim = GrabAnims::GRAB_THROW;
            victimAnim = GrabAnims::GRABBED_THROW;
            break;
        case GRAB_ACTION_KNOCKOUT:
            playerAnim = GrabAnims::GRAB_KO_REL;
            victimAnim = GrabAnims::GRABBED_KO;
            break;
        default:
            return;
    }

    // Play action animations (not looped)
    g_pPlayerAnim = PlayAnimationWithCallback(
        player,
        playerAnim,
        false,
        8.0f,
        PlayerAnimFinishedCB,
        nullptr
    );

    g_pVictimAnim = PlayAnimationWithCallback(
        g_pGrabbedPed,
        victimAnim,
        false,
        8.0f,
        VictimAnimFinishedCB,
        nullptr
    );

    ApplyGrabDamage(action);
}

void StartReleaseAnimation() {
    g_grabState = GRAB_STATE_RELEASING;

    CPlayerPed* player = GetPlayer();
    if (player && g_pGrabbedPed) {
        g_bPlayerAnimFinished = false;
        g_bVictimAnimFinished = false;

        g_pPlayerAnim = PlayAnimationWithCallback(
            player,
            GrabAnims::GRAB_BREAK,
            false,
            8.0f,
            PlayerAnimFinishedCB,
            nullptr
        );

        g_pVictimAnim = PlayAnimationWithCallback(
            g_pGrabbedPed,
            GrabAnims::GRABBED_BREAK,
            false,
            8.0f,
            VictimAnimFinishedCB,
            nullptr
        );
    }

    ForceReleaseGrab();
}

void ApplyGrabDamage(eGrabAction action) {
    if (!g_pGrabbedPed) return;

    float damage = 0.0f;
    switch (action) {
        case GRAB_ACTION_JAB:
            damage = static_cast<float>(GrabConfig.grabDamageJab);
            break;
        case GRAB_ACTION_UPPERCUT:
            damage = static_cast<float>(GrabConfig.grabDamageUppercut);
            break;
        case GRAB_ACTION_THROW:
            damage = static_cast<float>(GrabConfig.grabDamageThrow);
            break;
        case GRAB_ACTION_KNOCKOUT:
            damage = static_cast<float>(GrabConfig.grabDamageKnockout);
            break;
        default:
            break;
    }

    if (damage > 0.0f) {
        g_pGrabbedPed->m_fHealth -= damage;
        if (g_pGrabbedPed->m_fHealth < 0.0f) {
            g_pGrabbedPed->m_fHealth = 0.0f;
        }
    }
}

void UpdateGrabbedPedPosition() {
    CPlayerPed* player = GetPlayer();
    if (!player || !g_pGrabbedPed) return;

    CVector playerPos = player->GetPosition();
    float playerHeading = player->m_fCurrentRotation;

    float offsetX = -sinf(playerHeading) * GrabConfig.grabOffset;
    float offsetY = cosf(playerHeading) * GrabConfig.grabOffset;

    CVector newPos = playerPos;
    newPos.x += offsetX;
    newPos.y += offsetY;

    g_pGrabbedPed->SetPosn(newPos);
    g_pGrabbedPed->m_fCurrentRotation = playerHeading + 3.14159f;
    g_pGrabbedPed->m_fAimingRotation = playerHeading + 3.14159f;
}

bool CheckVictimEscape() {
    if (!g_pGrabbedPed) return true;
    if (!IsPedAlive(g_pGrabbedPed)) return true;

    CPlayerPed* player = GetPlayer();
    if (!player) return true;

    float distance = GetDistanceBetweenPeds(player, g_pGrabbedPed);
    if (distance > GrabConfig.escapeDistance) return true;

    return false;
}

} // namespace Internal

// ============================================================================
// PUBLIC API
// ============================================================================

void InstallHooks() {
    Events::initGameEvent.Add([]() {
        LoadGrabAnimations();
    });

    Events::processScriptsEvent.Add([]() {
        Process();
    }, true);

    Events::reInitGameEvent.Add([]() {
        if (g_fightBlockIndex >= 0) {
            CAnimManager::RemoveAnimBlockRef(g_fightBlockIndex);
            g_fightBlockIndex = -1;
            g_pFightAnimBlock = nullptr;
        }

        LoadGrabAnimations();
    });
}

void Initialize() {
    
}

void Process() {
    CPlayerPed* player = GetPlayer();
    if (!player) return;

    if (player->bInVehicle) {
        if (g_grabState != GRAB_STATE_NONE) ForceReleaseGrab();
        return;
    }
    if (!IsPedAlive(player)) {
        if (g_grabState != GRAB_STATE_NONE) ForceReleaseGrab();
        return;
    }

    // Check grab timeout
    if (g_grabState != GRAB_STATE_NONE && g_grabStartTime > 0) {
        if (CTimer::m_snTimeInMilliseconds - g_grabStartTime > GrabConfig.maxGrabDurationMs) {
            ForceReleaseGrab();
            return;
        }
    }

    switch (g_grabState) {
        case GRAB_STATE_NONE:
            if (IsKeyJustPressed(GRAB_KEY)) {
                TryInitiateGrab();
            }
            break;

        case GRAB_STATE_INITIATING:
            if (g_pGrabbedPed) {
                Internal::UpdateGrabbedPedPosition();
            }
            // Check if BOTH animations finished (using callbacks)
            if (g_bPlayerAnimFinished && g_bVictimAnimFinished) {
                DebugLog("INITIATING -> HOLDING: both anims finished");
                Internal::StartHoldAnimation();
            }
            break;

        case GRAB_STATE_HOLDING:
            Internal::UpdateGrabbedPedPosition();

            if (Internal::CheckVictimEscape()) {
                DebugLog("HOLDING -> ESCAPED: victim escaped");
                g_grabState = GRAB_STATE_VICTIM_ESCAPED;
                ForceReleaseGrab();
                break;
            }

            if (IsKeyJustPressed(GRAB_KEY)) {
                DebugLog("HOLDING -> RELEASING: grab key pressed");
                ReleaseGrab();
            }
            // Check for directional grab actions
            else {
                // Check W key (forward) for throw
                if (IsKeyJustPressed('W')) {
                    DebugLog("HOLDING: W key pressed - performing throw");
                    PerformGrabAction(GRAB_ACTION_THROW);
                }
                // Check S key (backward) for uppercut
                else if (IsKeyJustPressed('S')) {
                    DebugLog("HOLDING: S key pressed - performing uppercut");
                    PerformGrabAction(GRAB_ACTION_UPPERCUT);
                }
                // Check attack button (LMB)
                else if (plugin::CallMethodAndReturn<unsigned char, 0x540390, CPad*, bool>(CPad::GetPad(0), false)) {
                    DebugLog("HOLDING: Attack button pressed - performing jab");
                    PerformGrabAction(GRAB_ACTION_JAB);
                }
            }
            break;

        case GRAB_STATE_PERFORMING:
            if (g_pGrabbedPed) {
                Internal::UpdateGrabbedPedPosition();
            }
            // Check if action animation finished
            if (g_bPlayerAnimFinished && g_bVictimAnimFinished) {
                DebugLog("PERFORMING -> finished: action=%d, playerAnimDone=%d victimAnimDone=%d",
                    g_currentAction, g_bPlayerAnimFinished, g_bVictimAnimFinished);
                if (g_currentAction == GRAB_ACTION_THROW || g_currentAction == GRAB_ACTION_KNOCKOUT) {
                    ForceReleaseGrab();
                } else {
                    DebugLog("PERFORMING -> HOLDING: returning to hold");
                    Internal::StartHoldAnimation();
                }
            }
            break;

        case GRAB_STATE_RELEASING:
            if (g_pGrabbedPed) {
                Internal::UpdateGrabbedPedPosition();
            }
            break;

        case GRAB_STATE_VICTIM_ESCAPED:
            ForceReleaseGrab();
            break;
    }
}

bool IsGrabbing() {
    return g_grabState != GRAB_STATE_NONE;
}

eGrabState GetGrabState() {
    return g_grabState;
}

CPed* GetGrabbedPed() {
    return g_pGrabbedPed;
}

bool TryInitiateGrab() {
    CPlayerPed* player = GetPlayer();
    if (!player) return false;

    if (!g_pFightAnimBlock) {
        DebugLog("TryInitiateGrab: Loading animations...");
        if (!LoadGrabAnimations()) {
            DebugLog("TryInitiateGrab: FAILED to load animations");
            return false;
        }
    }

    CPed* target = Internal::FindGrabTarget(player);
    if (!target) {
        DebugLog("TryInitiateGrab: No target found");
        return false;
    }

    DebugLog("TryInitiateGrab: Found target=%p, initiating grab", target);
    g_pGrabbedPed = target;
    g_grabStartTime = CTimer::m_snTimeInMilliseconds;

    Internal::StartGrabAnimation();

    return true;
}

void PerformGrabAction(eGrabAction action) {
    if (g_grabState != GRAB_STATE_HOLDING) return;
    if (!g_pGrabbedPed) return;

    Internal::StartActionAnimation(action);
}

void ReleaseGrab() {
    if (g_grabState == GRAB_STATE_NONE) return;
    Internal::StartReleaseAnimation();
}

void ForceReleaseGrab() {
    CPlayerPed* player = GetPlayer();
    if (player) {
        ReleasePlayerFromGrab(player);
    }
    if (g_pGrabbedPed) {
        ReleaseVictimFromGrab(g_pGrabbedPed);
    }

    g_grabState = GRAB_STATE_NONE;
    g_pGrabbedPed = nullptr;
    g_grabStartTime = 0;
    g_pPlayerAnim = nullptr;
    g_pVictimAnim = nullptr;
    g_currentAction = GRAB_ACTION_NONE;
    g_bPlayerAnimFinished = false;
    g_bVictimAnimFinished = false;
}

} // namespace CGrabSystem
