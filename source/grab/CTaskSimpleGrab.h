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
// Flow:
// 1. Player presses R -> this task is assigned
// 2. Plays "Fight_grab" animation
// 3. At midpoint (via callback):
//    - If valid victim found: attach victim, assign CTaskSimpleGrabbed
//    - If no victim: abort animation
// 4. Animation completes -> task finishes
// ============================================================================

class CTaskSimpleGrab : public CTaskSimple
{
public:
    // ========== State ==========
    enum class eGrabState : unsigned char
    {
        REACHING,      // Playing first half of grab anim, looking for victim
        ATTACHED,      // Victim attached, playing second half
        ABORTING,      // No victim found, aborting animation
        FINISHED       // Task complete
    };

    // ========== Configuration ==========
    static constexpr float GRAB_RANGE = 1.5f;           // Max distance to grab victim
    static constexpr float GRAB_ANGLE = 45.0f;          // Max angle (degrees) to victim
    static constexpr float MIDPOINT_TIME = 0.5f;        // Normalized time (0-1) for midpoint
    static constexpr float VICTIM_OFFSET_FORWARD = 0.7f; // Victim offset in front of player
    static constexpr float VICTIM_OFFSET_Z = 0.0f;      // Victim Z offset
    
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
    bool m_bMidpointChecked;                // Whether we've done midpoint victim check

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
    void StartGrabAnimation(CPed* ped);
    CPed* FindValidVictim(CPed* grabber);
    void AttachVictim(CPed* grabber, CPed* victim);
    void AbortGrab(CPed* ped);
    void FinishGrab();
    
    // ========== Animation Callbacks ==========
    static void OnAnimMidpoint(CAnimBlendAssociation* anim, void* data);
    static void OnAnimFinish(CAnimBlendAssociation* anim, void* data);
};
