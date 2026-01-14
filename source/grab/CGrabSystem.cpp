#include "CGrabSystem.h"
#include "CTaskSimpleGrab.h"
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
#include <cstdlib>

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
// AUDIO EVENT IDs (from gta-reversed eAudioEvents.h)
// ============================================================================
enum eAudioEvents : int {
    AE_PED_SWING              = 60,   // Weapon swing sound
    AE_PED_HIT_HIGH           = 61,   // High hit with weapon
    AE_PED_HIT_LOW            = 62,   // Low hit with weapon
    AE_PED_HIT_GROUND         = 63,   // Ground hit
    AE_PED_HIT_GROUND_KICK    = 64,   // Ground kick
    AE_PED_HIT_HIGH_UNARMED   = 65,   // High punch (unarmed)
    AE_PED_HIT_LOW_UNARMED    = 66,   // Low punch (unarmed)
    AE_PED_HIT_MARTIAL_PUNCH  = 67,   // Martial arts punch
    AE_PED_HIT_MARTIAL_KICK   = 68,   // Martial arts kick
    AE_PED_KNOCK_DOWN         = 121,  // Knocked down sound
};

// ============================================================================
// SPEECH CONTEXT IDs (from gta-reversed PedSpeechContexts.h)
// ============================================================================
enum eGlobalSpeechContext : short {
    CTX_GLOBAL_FIGHT           = 89,   // Attack grunt when fighting
    CTX_GLOBAL_PAIN_LOW        = 345,  // Low pain grunt
    CTX_GLOBAL_PAIN_HIGH       = 344,  // High pain scream
    CTX_GLOBAL_PAIN_DEATH_LOW  = 343,  // Low death sound
    CTX_GLOBAL_PAIN_DEATH_HIGH = 342,  // High death sound
};

// ============================================================================
// CRIME TYPES
// Uses eCrimeType from plugin-sdk, plus additional values from gta-reversed
// ============================================================================
#include <eCrimeType.h>

// Additional crime types not in plugin-sdk (from gta-reversed eCrimeType.h)
// CRIME_DAMAGED_PED = 2 is already in plugin-sdk
constexpr int CRIME_DAMAGED_COP = 3;   // Note: plugin-sdk has this as FIRE_WEAPON_HIT_PED, but it's actually DAMAGED_COP in game
constexpr int CRIME_STAB_PED    = 18;  // Melee weapon kill on ped  
constexpr int CRIME_STAB_COP    = 19;  // Melee weapon kill on cop

// ============================================================================
// FUNCTION POINTERS (addresses from gta-reversed)
// ============================================================================

// CCrime::ReportCrime - reports crime to police system
// Address: 0x532010
// Uses int for crime type to allow both enum values and extended constants
typedef void(__cdecl* CCrime_ReportCrime_t)(int crimeType, CEntity* victim, CPed* criminal);
static CCrime_ReportCrime_t CCrime_ReportCrime = reinterpret_cast<CCrime_ReportCrime_t>(0x532010);

// CAEPedAudioEntity::AddAudioEvent - plays ped audio events (punches, etc)
// Address: 0x4E2BB0
// Note: CAEPedAudioEntity exists in plugin-sdk but AddAudioEvent method is not declared
typedef void(__thiscall* AddAudioEvent_t)(CAEPedAudioEntity* audioEntity, int event, float volume, float speed, 
                                          CPhysical* physical, int surfaceId, int a7, unsigned int maxVol);
static AddAudioEvent_t AddAudioEvent = reinterpret_cast<AddAudioEvent_t>(0x4E2BB0);

// Note: CPed::Say is available in plugin-sdk, use ped->Say() directly
// Note: CPed::m_pedAudio is available in plugin-sdk, use &ped->m_pedAudio directly

// ============================================================================
// EVENT SYSTEM - For making peds react after release
// ============================================================================

// CEventAcquaintancePedHate - makes ped hate and react to another ped
// Constructor at 0x420E70, size 0x18 (24 bytes)
// When added to event group, ped will fight back or flee based on personality
struct CEventAcquaintancePedHate {
    void* vtable;           // 0x00 - Virtual table pointer
    int refCount;           // 0x04 - Reference count
    float timeActive;       // 0x08
    bool responseTaskSet;   // 0x0C
    char pad[3];            // 0x0D
    int taskType;           // 0x10 - Task to perform (TASK_NONE = let AI decide)
    CPed* targetPed;        // 0x14 - Ped to hate
};
static_assert(sizeof(CEventAcquaintancePedHate) == 0x18, "CEventAcquaintancePedHate size mismatch");

// Constructor: CEventAcquaintancePedHate::CEventAcquaintancePedHate(CPed* ped)
typedef CEventAcquaintancePedHate* (__thiscall* EventAcquaintancePedHate_Ctor_t)(CEventAcquaintancePedHate* self, CPed* ped);
static EventAcquaintancePedHate_Ctor_t EventAcquaintancePedHate_Ctor = reinterpret_cast<EventAcquaintancePedHate_Ctor_t>(0x420E70);

// CEventGroup::Add - adds event to ped's event group
// Address: 0x4AB420
// Note: CEventGroup exists in plugin-sdk but Add method is not declared
// Access via ped->m_pIntelligence->m_eventGroup (available in plugin-sdk)
typedef void* (__thiscall* EventGroup_Add_t)(CEventGroup* eventGroup, void* event, bool valid);
static EventGroup_Add_t EventGroup_Add = reinterpret_cast<EventGroup_Add_t>(0x4AB420);

// NOTE: RpAnimBlendClumpRemoveAllAssociations is already declared in plugin-sdk common.h

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
    unsigned int g_windupStartTime = 0;     // When windup phase started
    CAnimBlendAssociation* g_pPlayerAnim = nullptr;
    CAnimBlendAssociation* g_pVictimAnim = nullptr;
    CAnimBlock* g_pFightAnimBlock = nullptr;
    bool g_bKeyWasPressed = false;
    eGrabAction g_currentAction = GRAB_ACTION_NONE;

    // Track when animations finish via callbacks
    bool g_bPlayerAnimFinished = false;
    bool g_bVictimAnimFinished = false;
    
    // Target position for victim during attach phase
    CVector g_victimTargetPos;
    float g_victimTargetHeading = 0.0f;
}

// Forward declarations
namespace Internal {
    void StartWindupPhase();
    void StartAttachPhase();
    void StartHoldAnimation();
    void StartActionAnimation(eGrabAction action);
    void StartReleaseAnimation();
    CPed* FindGrabTarget(CPlayerPed* player);
    void UpdateGrabbedPedPosition();
    void LerpVictimToPosition(float deltaTime);
    float GetDistanceToTargetPosition();
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

// Get forward vector from ped heading (GTA uses: forward.x = -sin(heading), forward.y = cos(heading))
static CVector GetPedForward(CPed* ped) {
    float heading = ped->m_fCurrentRotation;
    CVector forward;
    forward.x = -sinf(heading);
    forward.y = cosf(heading);
    forward.z = 0.0f;
    return forward;
}

// Dot product for 2D vectors (ignoring Z)
static float DotProduct2D(const CVector& a, const CVector& b) {
    return a.x * b.x + a.y * b.y;
}

// Check if player is facing toward the target (using dot product like stealth kill)
// Returns true if player's forward vector points toward target
static bool IsPlayerFacingTarget(CPed* player, CPed* target, float minDot = 0.5f) {
    CVector playerPos = player->GetPosition();
    CVector targetPos = target->GetPosition();
    
    // Direction from player to target
    CVector toTarget;
    toTarget.x = targetPos.x - playerPos.x;
    toTarget.y = targetPos.y - playerPos.y;
    toTarget.z = 0.0f;
    
    // Normalize
    float length = sqrtf(toTarget.x * toTarget.x + toTarget.y * toTarget.y);
    if (length < 0.001f) return false;
    toTarget.x /= length;
    toTarget.y /= length;
    
    // Get player's forward vector
    CVector playerForward = GetPedForward(player);
    
    // Dot product: 1.0 = same direction, 0.0 = perpendicular, -1.0 = opposite
    float dot = DotProduct2D(playerForward, toTarget);
    
    return dot >= minDot;  // minDot 0.5 = within ~60 degrees of facing
}

// Check if target is facing toward player (mutual facing check)
// This ensures they're facing each other for grab
static bool IsTargetFacingPlayer(CPed* player, CPed* target, float minDot = 0.0f) {
    CVector playerPos = player->GetPosition();
    CVector targetPos = target->GetPosition();
    
    // Direction from target to player
    CVector toPlayer;
    toPlayer.x = playerPos.x - targetPos.x;
    toPlayer.y = playerPos.y - targetPos.y;
    toPlayer.z = 0.0f;
    
    // Normalize
    float length = sqrtf(toPlayer.x * toPlayer.x + toPlayer.y * toPlayer.y);
    if (length < 0.001f) return false;
    toPlayer.x /= length;
    toPlayer.y /= length;
    
    // Get target's forward vector
    CVector targetForward = GetPedForward(target);
    
    // Dot product check
    float dot = DotProduct2D(targetForward, toPlayer);
    
    return dot >= minDot;  // minDot 0.0 = within 90 degrees (not facing away)
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
// CRIME AND AUDIO HELPERS
// ============================================================================

// Check if a ped is valid for audio operations
// Must have valid pointer, be alive, and have valid RwClump
static bool IsPedValidForAudio(CPed* ped) {
    if (!ped) return false;
    if (!ped->m_pRwClump) return false;
    if (ped->m_ePedState == PEDSTATE_DEAD) return false;
    if (ped->m_fHealth <= 0.0f) return false;
    return true;
}

// Get the audio entity from a ped (use plugin-sdk m_pedAudio member)
static CAEPedAudioEntity* GetPedAudioEntity(CPed* ped) {
    if (!ped) return nullptr;
    return &ped->m_pedAudio;
}

// Play a hit/punch sound effect
static void PlayHitSound(CPed* attacker, CPed* victim, eAudioEvents soundEvent) {
    // Validate attacker (who plays the sound)
    if (!IsPedValidForAudio(attacker)) {
        DebugLog("PlayHitSound: attacker invalid, skipping");
        return;
    }
    
    CAEPedAudioEntity* audioEntity = GetPedAudioEntity(attacker);
    if (audioEntity) {
        // Pass attacker as the physical - AddAudioEvent crashes with nullptr!
        // AddAudioEvent(audioEntity, event, volume, speed, physical, surface, a7, maxVol)
        AddAudioEvent(audioEntity, soundEvent, 0.0f, 1.0f, attacker, 0, 0, 0);
        DebugLog("PlayHitSound: event=%d attacker=%p", soundEvent, attacker);
    }
}

// Make a ped say something (grunt, scream, etc)
static void MakePedSay(CPed* ped, eGlobalSpeechContext context) {
    // Validate ped before calling Say
    if (!IsPedValidForAudio(ped)) {
        DebugLog("MakePedSay: ped invalid, skipping");
        return;
    }
    
    // Use plugin-sdk CPed::Say method directly
    // Say(speechContext, delay, probability, overrideSilence, forceAudible, isFrontEnd)
    ped->Say(context, 0, 1.0f, 0, 0, 0);
    DebugLog("MakePedSay: ped=%p context=%d", ped, context);
}

// Report a crime to the police system
static void ReportGrabCrime(CPed* attacker, CPed* victim, bool isLethal) {
    if (!attacker || !victim) return;
    
    // Check victim is still a valid entity (not deleted)
    if (!victim->m_pRwClump) {
        DebugLog("ReportGrabCrime: victim invalid, skipping");
        return;
    }
    
    // Determine crime type based on victim type and lethality
    int crimeType;
    
    // Check if victim is a cop (PED_TYPE_COP = 6 in ePedType)
    bool isCop = (victim->m_nPedType == 6); // PED_TYPE_COP
    
    if (isLethal) {
        crimeType = isCop ? CRIME_STAB_COP : CRIME_STAB_PED;
    } else {
        crimeType = isCop ? CRIME_DAMAGED_COP : CRIME_DAMAGED_PED;
    }
    
    // Report the crime
    CCrime_ReportCrime(crimeType, victim, attacker);
    DebugLog("ReportGrabCrime: type=%d victim=%p (cop=%d) attacker=%p", crimeType, victim, isCop, attacker);
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
// ANIMATION CLEANUP (from gta-reversed CTaskSimpleAnim::~CTaskSimpleAnim)
// ============================================================================

// Animation flag for auto-removal (from gta-reversed AnimBlendAssociation.h)
// When blend amount reaches 0, animation is automatically deleted
constexpr int ANIM_FLAG_BLEND_AUTO_REMOVE = 0x4;

// Default animation callback - does nothing, used to detach our callbacks
static void DefaultAnimCB(CAnimBlendAssociation* anim, void* data) {
    // Empty - just a placeholder to replace our callbacks
}

// Properly clean up an animation before playing a new one
// This prevents crashes from stale callbacks
static void CleanupAnimation(CAnimBlendAssociation*& anim) {
    if (!anim) return;
    
    // Only detach our callback - don't mess with flags or blend delta!
    // Setting ANIM_FLAG_BLEND_AUTO_REMOVE causes use-after-free crashes
    // when RpAnimBlendClumpUpdateAnimations iterates and frees during update.
    // Just detach callback and let BlendAnimation naturally replace the old anim.
    anim->SetFinishCallback(DefaultAnimCB, nullptr);
    
    DebugLog("CleanupAnimation: detached callback from anim=%p", anim);
    
    // Clear our reference (animation still exists, just not tracked by us)
    anim = nullptr;
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

    // CRITICAL: Stop all non-partial (full-body) animations like stealth kill does
    // This ensures our grabbed animation can take over cleanly
    victim->StopNonPartialAnims();
    
    // Set victim state (like arrest does)
    victim->SetPedState(PEDSTATE_ARRESTED);

    // Set flags to prevent AI behavior
    victim->bStayInSamePlace = true;
    victim->bKindaStayInSamePlace = true;
    victim->bDontFight = true;
    victim->bIsBeingArrested = true;
    
    // Disable collision so player and victim don't push each other during grab animations
    victim->bUsesCollision = false;

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
    
    DebugLog("SetupVictimForGrab: stopped anims, cleared tasks, set state");
}

// Release victim from grab state
static void ReleaseVictimFromGrab(CPed* victim) {
    // CRITICAL: Clean up our animation first to detach callbacks
    // This prevents crashes from stale callback pointers
    CleanupAnimation(g_pVictimAnim);
    
    if (!victim) return;
    
    // Check if victim still has a valid RwClump (not deleted)
    if (!victim->m_pRwClump) {
        DebugLog("ReleaseVictimFromGrab: victim RwClump invalid, skipping");
        return;
    }

    // DON'T use RpAnimBlendClumpRemoveAllAssociations - it crashes in DoFootLanded!
    // The ped needs at least a base animation playing at all times.
    // CleanupAnimation already sets blend delta to fade out our grab anims.
    // The ped's task system will blend in proper idle animation.
    DebugLog("ReleaseVictimFromGrab: letting grab anims blend out naturally");

    // Clear the animation moving shift so victim stops sliding
    victim->m_vecAnimMovingShiftLocal.x = 0.0f;
    victim->m_vecAnimMovingShiftLocal.y = 0.0f;

    // Re-enable collision
    victim->bUsesCollision = true;

    // Clear flags that were set during grab
    victim->bStayInSamePlace = false;
    victim->bKindaStayInSamePlace = false;
    victim->bDontFight = false;
    victim->bIsBeingArrested = false;

    // Clear our task and trigger reaction (only if intelligence is valid)
    if (victim->m_pIntelligence) {
        CTaskManager* taskMgr = &victim->m_pIntelligence->m_TaskMgr;
        taskMgr->SetTask(nullptr, TASK_PRIMARY_PRIMARY, false);
        
        // Trigger a "hate player" event so the ped reacts naturally
        // This makes them either fight back or flee based on their personality
        // Like what happens after you punch someone in a fist fight
        CPlayerPed* player = FindPlayerPed(0);
        if (player && victim->m_ePedState != PEDSTATE_DEAD && victim->m_ePedState != PEDSTATE_DIE) {
            // Create event on stack (0x18 bytes)
            CEventAcquaintancePedHate hateEvent;
            memset(&hateEvent, 0, sizeof(hateEvent));
            
            // Call the constructor to properly initialize the event
            EventAcquaintancePedHate_Ctor(&hateEvent, player);
            
            // Add to victim's event group - this triggers fight/flee AI
            // Use plugin-sdk m_eventGroup member directly
            EventGroup_Add(&victim->m_pIntelligence->m_eventGroup, &hateEvent, false);
            
            DebugLog("ReleaseVictimFromGrab: added hate event, victim should react");
        }
    }

    // Restore to idle state (only if not dead)
    if (victim->m_ePedState != PEDSTATE_DEAD && victim->m_ePedState != PEDSTATE_DIE) {
        victim->SetPedState(PEDSTATE_IDLE);
    }
}

// ============================================================================
// PLAYER TASK MANAGEMENT
// ============================================================================

static void SetupPlayerForGrab(CPlayerPed* player) {
    if (!player || !player->m_pIntelligence) return;

    CTaskManager* taskMgr = &player->m_pIntelligence->m_TaskMgr;

    // Stop physics movement (but don't clear tasks - camera depends on them!)
    player->m_vecMoveSpeed.Set(0.0f, 0.0f, 0.0f);
    player->m_vecTurnSpeed.Set(0.0f, 0.0f, 0.0f);
    
    // Set attack state
    player->SetPedState(PEDSTATE_ATTACK);

    // Use custom grab task - only set PRIMARY_PRIMARY
    // Don't clear other tasks as camera system depends on them
    CTaskSimpleGrab* grabTask = new CTaskSimpleGrab();
    taskMgr->SetTask(grabTask, TASK_PRIMARY_PRIMARY, false);
    
    // Clear secondary attack to prevent combo punches
    taskMgr->SetTask(nullptr, TASK_SECONDARY_ATTACK, false);
    
    DebugLog("SetupPlayerForGrab: set grab task, stopped physics movement");
}

static void ReleasePlayerFromGrab(CPlayerPed* player) {
    // CRITICAL: Clean up our animation first to detach callbacks
    CleanupAnimation(g_pPlayerAnim);
    
    if (!player) return;
    
    // Check if player still has valid RwClump
    if (!player->m_pRwClump) {
        DebugLog("ReleasePlayerFromGrab: player RwClump invalid");
        return;
    }

    // DON'T use RpAnimBlendClumpRemoveAllAssociations - it crashes in DoFootLanded!
    // CleanupAnimation already sets blend delta to fade out our grab anims.
    // The player's task system will blend in proper idle animation.
    DebugLog("ReleasePlayerFromGrab: letting grab anims blend out naturally");

    // Clear the grab task we set
    if (player->m_pIntelligence) {
        CTaskManager* taskMgr = &player->m_pIntelligence->m_TaskMgr;
        taskMgr->SetTask(nullptr, TASK_PRIMARY_PRIMARY, false);
    }

    // Restore player state
    player->SetPedState(PEDSTATE_IDLE);
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

        // Check 1: Player must be facing the target (dot product >= 0.5, within ~60 degrees)
        if (!IsPlayerFacingTarget(player, ped, 0.5f)) {
            DebugLog("FindGrabTarget: ped=%p rejected - player not facing target", ped);
            continue;
        }

        // Check 2: Target should be somewhat facing player (not completely turned away)
        // Using minDot 0.0 means target must be within 90 degrees of facing player
        if (!IsTargetFacingPlayer(player, ped, 0.0f)) {
            DebugLog("FindGrabTarget: ped=%p rejected - target facing away", ped);
            continue;
        }

        float angle = GetAngleBetweenPeds(player, ped);
        if (angle < bestAngle) {
            bestAngle = angle;
            bestTarget = ped;
        }
    }

    return bestTarget;
}

// Calculate target position and heading for victim (where they should end up)
void CalculateVictimTargetPosition(CPlayerPed* player, CPed* victim) {
    if (!player || !victim) return;

    CVector playerPos = player->GetPosition();
    CVector victimPos = victim->GetPosition();

    // Calculate direction from player to victim
    CVector toVictim;
    toVictim.x = victimPos.x - playerPos.x;
    toVictim.y = victimPos.y - playerPos.y;
    toVictim.z = 0.0f;

    // Calculate heading that faces the victim (GTA heading: atan2(-x, y))
    float headingToVictim = atan2f(-toVictim.x, toVictim.y);

    // Calculate target position: grabOffset distance in front of player
    g_victimTargetPos.x = playerPos.x + (-sinf(headingToVictim)) * GrabConfig.grabOffset;
    g_victimTargetPos.y = playerPos.y + cosf(headingToVictim) * GrabConfig.grabOffset;
    g_victimTargetPos.z = victimPos.z;  // Keep same Z

    // Calculate target heading: facing player (opposite direction)
    g_victimTargetHeading = headingToVictim + 3.14159f;
    // Normalize to [-PI, PI]
    while (g_victimTargetHeading > 3.14159f) g_victimTargetHeading -= 6.28318f;
    while (g_victimTargetHeading < -3.14159f) g_victimTargetHeading += 6.28318f;

    DebugLog("CalculateVictimTargetPosition: target=(%.2f, %.2f) heading=%.2f",
             g_victimTargetPos.x, g_victimTargetPos.y, g_victimTargetHeading);
}

// Get distance from victim's current position to target position
float GetDistanceToTargetPosition() {
    if (!g_pGrabbedPed) return 999.0f;
    
    CVector victimPos = g_pGrabbedPed->GetPosition();
    float dx = g_victimTargetPos.x - victimPos.x;
    float dy = g_victimTargetPos.y - victimPos.y;
    return sqrtf(dx * dx + dy * dy);
}

// Smoothly move victim toward target position
void LerpVictimToPosition(float deltaTime) {
    if (!g_pGrabbedPed) return;
    
    CVector victimPos = g_pGrabbedPed->GetPosition();
    
    // Calculate direction to target
    float dx = g_victimTargetPos.x - victimPos.x;
    float dy = g_victimTargetPos.y - victimPos.y;
    float dist = sqrtf(dx * dx + dy * dy);
    
    if (dist < 0.01f) {
        // Close enough, snap to position
        g_pGrabbedPed->SetPosn(g_victimTargetPos);
        return;
    }
    
    // Lerp speed scales with distance for smooth deceleration
    float speed = GrabConfig.attachLerpSpeed * deltaTime;
    if (speed > dist) speed = dist;  // Don't overshoot
    
    // Move toward target
    float nx = dx / dist;  // Normalized direction
    float ny = dy / dist;
    
    CVector newPos = victimPos;
    newPos.x += nx * speed;
    newPos.y += ny * speed;
    g_pGrabbedPed->SetPosn(newPos);
    
    // Also lerp heading
    float currentHeading = g_pGrabbedPed->m_fCurrentRotation;
    float headingDiff = g_victimTargetHeading - currentHeading;
    
    // Normalize heading diff to [-PI, PI]
    while (headingDiff > 3.14159f) headingDiff -= 6.28318f;
    while (headingDiff < -3.14159f) headingDiff += 6.28318f;
    
    float headingSpeed = 8.0f * deltaTime;
    if (fabsf(headingDiff) < headingSpeed) {
        g_pGrabbedPed->m_fCurrentRotation = g_victimTargetHeading;
    } else {
        g_pGrabbedPed->m_fCurrentRotation += (headingDiff > 0 ? headingSpeed : -headingSpeed);
    }
    g_pGrabbedPed->m_fAimingRotation = g_pGrabbedPed->m_fCurrentRotation;
}

// Phase 1: Player starts grab wind-up animation, victim NOT attached yet
void StartWindupPhase() {
    CPlayerPed* player = GetPlayer();
    if (!player) return;

    DebugLog("StartWindupPhase: beginning grab wind-up (target=%p)", g_pGrabbedPed);

    // If we have a target, calculate where they should end up and face them
    if (g_pGrabbedPed) {
        CalculateVictimTargetPosition(player, g_pGrabbedPed);

        // Rotate player to face victim immediately
        CVector playerPos = player->GetPosition();
        CVector victimPos = g_pGrabbedPed->GetPosition();
        CVector toVictim;
        toVictim.x = victimPos.x - playerPos.x;
        toVictim.y = victimPos.y - playerPos.y;
        float headingToVictim = atan2f(-toVictim.x, toVictim.y);
        player->m_fCurrentRotation = headingToVictim;
        player->m_fAimingRotation = headingToVictim;
    }
    // If no target, player keeps current facing (whiff in current direction)

    // Setup player (stops movement, sets task)
    SetupPlayerForGrab(player);

    // Reset animation flags
    g_bPlayerAnimFinished = false;
    g_bVictimAnimFinished = false;

    // Start player grab animation with high blend delta to quickly override
    // any other playing animations (like melee approach walk)
    g_pPlayerAnim = PlayAnimationWithCallback(
        player,
        GrabAnims::GRAB_INIT,
        false,  // Not looped
        32.0f,  // Very fast blend to override other anims
        PlayerAnimFinishedCB,
        nullptr
    );

    // Record when windup started
    g_windupStartTime = CTimer::m_snTimeInMilliseconds;
    g_grabState = GRAB_STATE_WINDUP;
    
    if (g_pGrabbedPed) {
        DebugLog("StartWindupPhase: player anim started, victim will attach in %.2fs", GrabConfig.windupDuration);
    } else {
        DebugLog("StartWindupPhase: whiff animation started (no target)");
    }
}

// Phase 2: Attach victim - start their animation and begin pulling them in
void StartAttachPhase() {
    CPlayerPed* player = GetPlayer();
    if (!player || !g_pGrabbedPed) return;

    DebugLog("StartAttachPhase: attaching victim");

    // IMPORTANT: Recalculate victim target position based on where player is NOW
    // Player may have moved during windup (e.g., from melee approach momentum)
    CalculateVictimTargetPosition(player, g_pGrabbedPed);

    // Check distance - if already close enough, we can skip to hold faster
    float dist = GetDistanceToTargetPosition();
    bool isClose = (dist < GrabConfig.instantAttachDist);
    
    DebugLog("StartAttachPhase: victim distance=%.2f, isClose=%d", dist, isClose);

    // Setup victim (stops them, sets state)
    SetupVictimForGrab(g_pGrabbedPed);

    // Rotate victim to face player
    g_pGrabbedPed->m_fCurrentRotation = g_victimTargetHeading;
    g_pGrabbedPed->m_fAimingRotation = g_victimTargetHeading;

    // Reset victim animation flag
    g_bVictimAnimFinished = false;

    if (isClose) {
        // Victim is already close - snap to position and go to hold
        g_pGrabbedPed->SetPosn(g_victimTargetPos);
        DebugLog("StartAttachPhase: victim close, snapping to hold");
        StartHoldAnimation();
        return;
    }

    // Start victim's grabbed animation (StopNonPartialAnims was already called)
    g_pVictimAnim = PlayAnimationWithCallback(
        g_pGrabbedPed,
        GrabAnims::GRABBED_INIT,
        false,
        8.0f,   // Same blend delta as stealth kill
        VictimAnimFinishedCB,
        nullptr
    );

    g_grabState = GRAB_STATE_ATTACHING;
}

void StartHoldAnimation() {
    CPlayerPed* player = GetPlayer();
    if (!player || !g_pGrabbedPed) return;

    // CRITICAL: Clean up old animations before playing new ones
    // This prevents crashes from stale callbacks
    CleanupAnimation(g_pPlayerAnim);
    CleanupAnimation(g_pVictimAnim);

    // Reset flags for new animations
    g_bPlayerAnimFinished = false;
    g_bVictimAnimFinished = false;

    // Play looped hold animations
    g_pPlayerAnim = PlayAnimationWithCallback(
        player,
        GrabAnims::GRAB_IDLE,
        true,   // Looped
        8.0f,
        PlayerAnimFinishedCB,
        nullptr
    );

    g_pVictimAnim = PlayAnimationWithCallback(
        g_pGrabbedPed,
        GrabAnims::GRABBED_IDLE,
        true,
        8.0f,
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

    // CRITICAL: Clean up old animations before playing new ones
    CleanupAnimation(g_pPlayerAnim);
    CleanupAnimation(g_pVictimAnim);

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

    // CRITICAL: Clean up old animations before playing new ones
    CleanupAnimation(g_pPlayerAnim);
    CleanupAnimation(g_pVictimAnim);

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

    CPlayerPed* player = GetPlayer();
    if (!player) return;

    float damage = 0.0f;
    eAudioEvents hitSound = AE_PED_HIT_HIGH_UNARMED;  // Default punch sound
    eGlobalSpeechContext painContext = CTX_GLOBAL_PAIN_LOW;
    bool isLethal = false;

    switch (action) {
        case GRAB_ACTION_JAB:
            damage = static_cast<float>(GrabConfig.grabDamageJab);
            hitSound = AE_PED_HIT_HIGH_UNARMED;
            painContext = CTX_GLOBAL_PAIN_LOW;
            break;
        case GRAB_ACTION_UPPERCUT:
            damage = static_cast<float>(GrabConfig.grabDamageUppercut);
            hitSound = AE_PED_HIT_LOW_UNARMED;  // Body hit for uppercut/stomach
            painContext = CTX_GLOBAL_PAIN_HIGH;
            break;
        case GRAB_ACTION_THROW:
            damage = static_cast<float>(GrabConfig.grabDamageThrow);
            hitSound = AE_PED_HIT_GROUND;
            painContext = CTX_GLOBAL_PAIN_HIGH;
            break;
        case GRAB_ACTION_KNOCKOUT:
            damage = static_cast<float>(GrabConfig.grabDamageKnockout);
            hitSound = AE_PED_KNOCK_DOWN;
            painContext = CTX_GLOBAL_PAIN_HIGH;
            isLethal = true;  // Knockout counts as serious crime
            break;
        default:
            break;
    }

    if (damage > 0.0f) {
        // Apply damage
        g_pGrabbedPed->m_fHealth -= damage;
        if (g_pGrabbedPed->m_fHealth < 0.0f) {
            g_pGrabbedPed->m_fHealth = 0.0f;
            isLethal = true;  // Victim died, upgrade crime
        }

        // Play hit sound from player
        PlayHitSound(player, g_pGrabbedPed, hitSound);

        // Player attack grunt (optional, for immersion)
        MakePedSay(player, CTX_GLOBAL_FIGHT);

        // Victim pain sound
        if (g_pGrabbedPed->m_fHealth > 0.0f) {
            MakePedSay(g_pGrabbedPed, painContext);
        } else {
            // Death sound if victim died
            MakePedSay(g_pGrabbedPed, CTX_GLOBAL_PAIN_DEATH_HIGH);
        }

        // Report crime to police system
        ReportGrabCrime(player, g_pGrabbedPed, isLethal);

        DebugLog("ApplyGrabDamage: action=%d damage=%.1f health=%.1f isLethal=%d", 
                 action, damage, g_pGrabbedPed->m_fHealth, isLethal);
    }
}

void UpdateGrabbedPedPosition() {
    CPlayerPed* player = GetPlayer();
    if (!player || !g_pGrabbedPed) return;

    CVector playerPos = player->GetPosition();
    CVector victimPos = g_pGrabbedPed->GetPosition();
    
    // Calculate direction from player to victim
    CVector toVictim;
    toVictim.x = victimPos.x - playerPos.x;
    toVictim.y = victimPos.y - playerPos.y;
    
    // Calculate heading player should have to face victim
    float headingToVictim = atan2f(-toVictim.x, toVictim.y);
    
    // LOCK player heading to face victim during grab
    player->m_fCurrentRotation = headingToVictim;
    player->m_fAimingRotation = headingToVictim;
    
    // Calculate target position for victim (in front of player)
    float offsetX = -sinf(headingToVictim) * GrabConfig.grabOffset;
    float offsetY = cosf(headingToVictim) * GrabConfig.grabOffset;

    CVector targetPos;
    targetPos.x = playerPos.x + offsetX;
    targetPos.y = playerPos.y + offsetY;
    targetPos.z = playerPos.z;

    // Calculate distance from victim to target position
    CVector toTarget;
    toTarget.x = targetPos.x - victimPos.x;
    toTarget.y = targetPos.y - victimPos.y;
    toTarget.z = 0.0f;

    float distance = sqrtf(toTarget.x * toTarget.x + toTarget.y * toTarget.y);

    // Use m_vecAnimMovingShiftLocal for smooth movement (like stealth kill)
    if (distance > 0.02f) {
        // Convert world-space direction to local-space (relative to victim's facing)
        float victimHeading = g_pGrabbedPed->m_fCurrentRotation;
        float cosH = cosf(victimHeading);
        float sinH = sinf(victimHeading);

        // Rotate world direction into local space
        float localX = toTarget.x * cosH + toTarget.y * sinH;
        float localY = -toTarget.x * sinH + toTarget.y * cosH;

        // Apply shift with speed limit
        float shiftSpeed = CTimer::ms_fTimeStep * 0.08f;
        g_pGrabbedPed->m_vecAnimMovingShiftLocal.x = std::min(shiftSpeed, std::abs(localX)) * (localX > 0 ? 1.0f : -1.0f);
        g_pGrabbedPed->m_vecAnimMovingShiftLocal.y = std::min(shiftSpeed, std::abs(localY)) * (localY > 0 ? 1.0f : -1.0f);
    } else {
        g_pGrabbedPed->m_vecAnimMovingShiftLocal.x = 0.0f;
        g_pGrabbedPed->m_vecAnimMovingShiftLocal.y = 0.0f;
    }

    // Keep victim facing player (opposite direction)
    float headingToPlayer = headingToVictim + 3.14159f;
    g_pGrabbedPed->m_fCurrentRotation = headingToPlayer;
    g_pGrabbedPed->m_fAimingRotation = headingToPlayer;
}

bool CheckVictimEscape() {
    if (!g_pGrabbedPed) return true;
    if (!IsPedAlive(g_pGrabbedPed)) return true;

    CPlayerPed* player = GetPlayer();
    if (!player) return true;

    // Check if too far away
    float distance = GetDistanceBetweenPeds(player, g_pGrabbedPed);
    if (distance > GrabConfig.escapeDistance) return true;

    // Calculate time-based escape chance
    // Escape chance increases linearly from escapeChanceStart to escapeChanceEnd
    // over the course of escapeRampUpMs milliseconds
    unsigned int timeHeld = CTimer::m_snTimeInMilliseconds - g_grabStartTime;
    
    // Calculate interpolation factor (0.0 to 1.0, clamped)
    float t = static_cast<float>(timeHeld) / static_cast<float>(GrabConfig.escapeRampUpMs);
    if (t > 1.0f) t = 1.0f;
    
    // Lerp between start and end escape chance
    float escapeChance = GrabConfig.escapeChanceStart + 
                         (GrabConfig.escapeChanceEnd - GrabConfig.escapeChanceStart) * t;
    
    // Roll random chance (0.0 to 1.0)
    float roll = static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
    
    if (roll < escapeChance) {
        DebugLog("CheckVictimEscape: victim escaped! time=%ums, chance=%.3f, roll=%.3f",
                 timeHeld, escapeChance, roll);
        return true;
    }
    
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

    // NOTE: Hard timeout removed - escape chance system handles gradual release
    // Victim escape chance increases over time in CheckVictimEscape()

    switch (g_grabState) {
        case GRAB_STATE_NONE:
            if (IsKeyJustPressed(GRAB_KEY)) {
                TryInitiateGrab();
            }
            break;

        case GRAB_STATE_WINDUP:
            {
                // Player is doing wind-up animation, victim not attached yet
                // Stop physics movement (animation handles the rest via high blend)
                player->m_vecMoveSpeed.Set(0.0f, 0.0f, 0.0f);
                
                // Check if we have a target (not a whiff)
                if (g_pGrabbedPed) {
                    // Check if windup duration has passed
                    unsigned int elapsed = CTimer::m_snTimeInMilliseconds - g_windupStartTime;
                    unsigned int windupMs = static_cast<unsigned int>(GrabConfig.windupDuration * 1000.0f);
                    
                    if (elapsed >= windupMs) {
                        // Time to attach victim
                        DebugLog("WINDUP -> ATTACHING: windup complete (%ums)", elapsed);
                        Internal::StartAttachPhase();
                    }
                } else {
                    // No target (whiff) - wait for animation to finish
                    if (g_bPlayerAnimFinished) {
                        DebugLog("WINDUP -> NONE: whiff animation finished");
                        ForceReleaseGrab();
                    }
                }
                
                // Allow cancellation
                if (IsKeyJustPressed(GRAB_KEY)) {
                    DebugLog("WINDUP -> NONE: cancelled by player");
                    ForceReleaseGrab();
                }
            }
            break;

        case GRAB_STATE_ATTACHING:
            {
                // Check if victim died - release immediately
                if (!g_pGrabbedPed || !IsPedAlive(g_pGrabbedPed)) {
                    DebugLog("ATTACHING -> NONE: victim died");
                    ForceReleaseGrab();
                    break;
                }
                
                // Lerping victim to target position while their animation plays
                float deltaTime = CTimer::ms_fTimeStep / 50.0f;  // Convert to seconds
                Internal::LerpVictimToPosition(deltaTime);
                
                // Stop physics movement
                player->m_vecMoveSpeed.Set(0.0f, 0.0f, 0.0f);
                
                // Check if victim reached target and animations done
                float dist = Internal::GetDistanceToTargetPosition();
                bool positionReached = (dist < 0.05f);
                
                // Wait for both position and player animation to be ready
                if (positionReached && g_bPlayerAnimFinished) {
                    DebugLog("ATTACHING -> HOLDING: victim in position, player anim done");
                    Internal::StartHoldAnimation();
                }
                // Also check if victim animation finished (as backup transition)
                else if (g_bPlayerAnimFinished && g_bVictimAnimFinished) {
                    DebugLog("ATTACHING -> HOLDING: both anims finished");
                    // Snap victim to position if not there yet
                    if (!positionReached && g_pGrabbedPed) {
                        g_pGrabbedPed->SetPosn(g_victimTargetPos);
                    }
                    Internal::StartHoldAnimation();
                }
                
                // Allow cancellation
                if (IsKeyJustPressed(GRAB_KEY)) {
                    DebugLog("ATTACHING -> NONE: cancelled by player");
                    ForceReleaseGrab();
                }
            }
            break;

        case GRAB_STATE_HOLDING:
            // Check if victim died - release immediately
            if (!g_pGrabbedPed || !IsPedAlive(g_pGrabbedPed)) {
                DebugLog("HOLDING -> NONE: victim died");
                ForceReleaseGrab();
                break;
            }
            
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
            {
                // Only update victim position if they're still alive
                if (g_pGrabbedPed && IsPedAlive(g_pGrabbedPed)) {
                    Internal::UpdateGrabbedPedPosition();
                }
                
                // Check if player animation finished (don't wait for victim if they died)
                bool victimDead = !g_pGrabbedPed || !IsPedAlive(g_pGrabbedPed);
                bool canFinish = g_bPlayerAnimFinished && (g_bVictimAnimFinished || victimDead);
                
                if (canFinish) {
                    DebugLog("PERFORMING -> finished: action=%d, victimDead=%d",
                        g_currentAction, victimDead);
                    // Always release after action if victim died, or if throw/knockout
                    if (victimDead || g_currentAction == GRAB_ACTION_THROW || g_currentAction == GRAB_ACTION_KNOCKOUT) {
                        ForceReleaseGrab();
                    } else {
                        DebugLog("PERFORMING -> HOLDING: returning to hold");
                        Internal::StartHoldAnimation();
                    }
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

    // Try to find a target
    CPed* target = Internal::FindGrabTarget(player);
    
    if (target) {
        DebugLog("TryInitiateGrab: Found target=%p, initiating grab", target);
        g_pGrabbedPed = target;
    } else {
        // No target - still play the grab animation (whiff)
        DebugLog("TryInitiateGrab: No target found, playing whiff animation");
        g_pGrabbedPed = nullptr;
    }
    
    g_grabStartTime = CTimer::m_snTimeInMilliseconds;
    Internal::StartWindupPhase();

    return true;  // Always return true - animation plays regardless
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
    g_windupStartTime = 0;
    g_pPlayerAnim = nullptr;
    g_pVictimAnim = nullptr;
    g_currentAction = GRAB_ACTION_NONE;
    g_bPlayerAnimFinished = false;
    g_bVictimAnimFinished = false;
}

} // namespace CGrabSystem
