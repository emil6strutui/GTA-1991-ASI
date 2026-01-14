#pragma once

#include <plugin.h>

// ============================================================================
// GRAB SYSTEM
// VCS-style grab mechanic for GTA San Andreas
// ============================================================================

class CPed;
class CPlayerPed;
class CAnimBlendAssociation;

// Grab states
enum eGrabState : unsigned char {
    GRAB_STATE_NONE = 0,
    GRAB_STATE_INITIATING,      // Playing grab initiation anim
    GRAB_STATE_HOLDING,         // Holding victim in grab
    GRAB_STATE_PERFORMING,      // Performing a grab action (jab, throw, etc.)
    GRAB_STATE_RELEASING,       // Releasing victim
    GRAB_STATE_VICTIM_ESCAPED   // Victim broke free
};

// Grab actions (while holding)
enum eGrabAction : unsigned char {
    GRAB_ACTION_NONE = 0,
    GRAB_ACTION_JAB,            // Punch while holding
    GRAB_ACTION_UPPERCUT,       // Uppercut (stomach hit)
    GRAB_ACTION_THROW,          // Throw forward
    GRAB_ACTION_RELEASE,        // Release victim
    GRAB_ACTION_KNOCKOUT        // Knockout blow
};

// Configuration
struct CGrabConfig {
    float grabRange = 1.5f;             // Max distance to initiate grab
    float grabAngle = 45.0f;            // Max angle (degrees) to target
    float grabOffset = 0.6f;            // Distance to position victim in front of player
    float escapeDistance = 2.0f;        // Distance at which victim escapes
    int grabDamageJab = 5;              // Damage per jab
    int grabDamageUppercut = 15;        // Damage per uppercut
    int grabDamageThrow = 10;           // Damage from throw
    int grabDamageKnockout = 30;        // Knockout damage
    float escapeChanceBase = 0.02f;     // Base chance per frame to escape
    unsigned int maxGrabDurationMs = 10000; // Max grab duration (ms)
};

extern CGrabConfig GrabConfig;

namespace CGrabSystem {

    // ============================================================================
    // PUBLIC INTERFACE
    // ============================================================================

    void Initialize();          // Call once at startup
    void InstallHooks();
    void Process();             // Called every frame

    // State queries
    bool IsGrabbing();
    bool IsBeingGrabbed(CPed* ped);
    eGrabState GetGrabState();
    CPed* GetGrabbedPed();

    // Actions
    bool TryInitiateGrab();     // Try to grab nearby ped
    void PerformGrabAction(eGrabAction action);
    void ReleaseGrab();
    void ForceReleaseGrab();    // Immediate release (e.g., player damaged)

    // ============================================================================
    // INTERNAL
    // ============================================================================
    namespace Internal {
        CPed* FindGrabTarget(CPlayerPed* player);
        void StartGrabAnimation();
        void StartHoldAnimation();
        void StartActionAnimation(eGrabAction action);
        void StartReleaseAnimation();
        void ApplyGrabDamage(eGrabAction action);
        void UpdateGrabbedPedPosition();
        bool CheckVictimEscape();
        void OnAnimationEnd(CAnimBlendAssociation* anim, void* data);
    }
}

// ============================================================================
// KEY BINDING
// ============================================================================
constexpr char GRAB_KEY = 'R';

// ============================================================================
// ANIMATION NAMES (from fight_a.ifp)
// ============================================================================
namespace GrabAnims {
    // Grabber (player) animations
    constexpr const char* GRAB_INIT     = "Fight_grab";
    constexpr const char* GRAB_IDLE     = "Fight_grab_idle";
    constexpr const char* GRAB_JAB      = "Fight_grab_jab";
    constexpr const char* GRAB_UPPERCUT = "Fight_grab_stom";      // Uppercut/stomach
    constexpr const char* GRAB_THROW    = "Fight_grab_fwd";
    constexpr const char* GRAB_BREAK    = "Fight_grab_break";     // When victim escapes
    constexpr const char* GRAB_KO_REL   = "Fight_grab_ko_re";     // Knockout release

    // Grabbed (victim) animations
    constexpr const char* GRABBED_INIT     = "Fight_grabbed";
    constexpr const char* GRABBED_IDLE     = "Fight_grabbed_idle";
    constexpr const char* GRABBED_JAB      = "Fight_grabbed_jab";
    constexpr const char* GRABBED_UPPERCUT = "Fight_grabbed_butt";  // Head back from uppercut
    constexpr const char* GRABBED_THROW    = "Fight_grabbed_fwd";
    constexpr const char* GRABBED_STRUGGLE = "Fight_grabbed_strug";
    constexpr const char* GRABBED_BREAK    = "Fight_grabbed_break"; // Breaking free
    constexpr const char* GRABBED_KO       = "Fight_grabbed_ko";
    constexpr const char* GRABBED_KO_REC   = "Fight_grabbed_ko_re"; // KO recovery
}
