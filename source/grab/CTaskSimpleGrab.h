#pragma once

#include <plugin.h>
#include <CTask.h>
#include <CPed.h>
#include <CAnimBlendAssociation.h>

// Forward declaration
class CTaskSimpleGrabbed;

// ============================================================================
// CTaskSimpleGrab - Player's grab task
// 
// New Flow:
// 1. Player presses R -> this task is assigned
// 2. IMMEDIATELY check for valid victim in range
// 3a. NO VICTIM: Play grab animation to 40%, then abort/blend out
// 3b. VICTIM FOUND: 
//     - Start BOTH player and victim animations simultaneously
//     - Skip animation based on distance:
//       * Very close (in face) = snap to hold instantly
//       * At max range = play full reach animation
//       * In between = interpolate skip amount
// ============================================================================

class CTaskSimpleGrab : public CTaskSimple
{
public:
    // ========== State ==========
    enum class eGrabState : unsigned char
    {
        INIT,          // Just started, need to check for victim
        NO_VICTIM,     // No victim found, playing to 40% then aborting
        ATTACHED,      // Victim attached, playing grab animation
        HOLDING,       // Grab animation done, holding victim (idle)
        ACTION,        // Performing an action (jab, throw, uppercut)
        ABORTING,      // Animation blending out
        FINISHED       // Task complete
    };

    // ========== Grab Actions ==========
    enum class eGrabAction : unsigned char
    {
        NONE,
        JAB,           // Quick punch while holding
        THROW,         // Forward + attack = throw victim forward
        UPPERCUT       // Back + attack = stomach punch/uppercut
    };

    // ========== Configuration ==========
    static constexpr float GRAB_RANGE = 1.5f;            // Max distance to grab victim
    static constexpr float GRAB_ANGLE = 45.0f;           // Max angle (degrees) to victim
    static constexpr float ABORT_PROGRESS = 0.4f;        // Play to 40% if no victim
    static constexpr float REACH_END_PROGRESS = 0.6f;    // Where "reach" portion ends (for skip calc)
    static constexpr float VICTIM_OFFSET_FORWARD = 1.0f; // Victim offset in front of player
    static constexpr float VICTIM_OFFSET_Z = 0.0f;       // Victim Z offset
    
    // Animation names (must be in loaded IFP)
    static constexpr const char* ANIM_BLOCK_NAME = "fight_a";
    static constexpr const char* ANIM_GRAB = "Fight_grab";

private:
    // ========== Members ==========
    eGrabState m_state;
    CPed* m_pVictim;                        // The ped we're grabbing (nullptr until attached)
    CTaskSimpleGrabbed* m_pVictimTask;      // Victim's task (for coordination)
    CAnimBlendAssociation* m_pAnim;         // Current animation
    bool m_bAnimsReferenced;                // Whether we've added anim block ref
    float m_fGrabDistance;                  // Distance to victim when grabbed (for animation skip)

public:
    // ========== Constructor/Destructor ==========
    CTaskSimpleGrab();
    ~CTaskSimpleGrab() override;

    // ========== CTask Interface ==========
    CTask* Clone() override;
    CTask* GetSubTask() override;
    bool IsSimple() override;
    eTaskType GetId() override;
    void StopTimer(CEvent* event) override;
    bool MakeAbortable(CPed* ped, eAbortPriority priority, CEvent* event) override;

    // ========== CTaskSimple Interface ==========
    bool ProcessPed(CPed* ped) override;
    bool SetPedPosition(CPed* ped) override;

    // ========== Grab-specific ==========
    CPed* GetVictim() const { return m_pVictim; }
    eGrabState GetState() const { return m_state; }
    
    // Called by victim task when victim escapes/dies
    void OnVictimLost();

private:
    // ========== Internal Methods ==========
    bool LoadAnimations();
    CPed* FindValidVictim(CPed* grabber, float* outDistance = nullptr);
    void StartGrabWithVictim(CPed* grabber, CPed* victim, float distance);
    void StartGrabNoVictim(CPed* grabber);
    void AbortGrab();
    void FinishGrab();
    
    // Calculate animation skip based on distance
    float CalculateAnimSkip(float distance) const;
    
    // ========== Animation Callbacks ==========
    static void NoOpAnimCallback(CAnimBlendAssociation* anim, void* data);
    static void OnAnimFinish(CAnimBlendAssociation* anim, void* data);
};
