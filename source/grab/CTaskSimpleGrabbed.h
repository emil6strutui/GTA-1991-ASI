#pragma once

#include <plugin.h>
#include <CTask.h>
#include <CPed.h>
#include <CAnimBlendAssociation.h>

#include "CTaskUtilityLineUpPedWithPed.h"

// Forward declaration
class CTaskSimpleGrab;

// ============================================================================
// CTaskSimpleGrabbed - Victim's task when grabbed
// 
// Assigned to victim by CTaskSimpleGrab when attachment occurs.
// Syncs victim position to grabber and plays grabbed animation.
// ============================================================================

class CTaskSimpleGrabbed : public CTaskSimple
{
public:
    // ========== State ==========
    enum class eGrabbedState : unsigned char
    {
        GRABBED,       // Being held, syncing position
        RELEASED,      // Grabber released us
        FINISHED       // Task complete
    };

    // ========== Configuration ==========
    // Animation names (must match IFP)
    static constexpr const char* ANIM_BLOCK_NAME = "fight_a";
    static constexpr const char* ANIM_GRABBED = "Fight_grabbed";
    static constexpr const char* ANIM_GRABBED_IDLE = "Fight_grabbed_idle";
    
    // How much forward distance the grab animation covers (root motion)
    static constexpr float ANIM_FORWARD_DISTANCE = 0.1f;

private:
    // ========== Members ==========
    eGrabbedState m_state;
    CPed* m_pGrabber;                       // The ped grabbing us
    CPed* m_pVictimPed;                     // Ourselves (for collision restore in destructor)
    CTaskSimpleGrab* m_pGrabberTask;        // Grabber's task (for coordination)
    CAnimBlendAssociation* m_pAnim;         // Current animation
    bool m_bAnimsReferenced;                // Whether we've added anim block ref
    bool m_bCollisionDisabled;              // Whether we've disabled collision
    
    // Line-up utility for animation-synced positioning (owned by grabber task)
    CTaskUtilityLineUpPedWithPed* m_pLineUpUtility;
    
    // Animation skip (0-1, synced with grabber's skip based on distance)
    float m_fAnimationSkip;
    
    // Whether to start directly with idle animation (for snap case)
    bool m_bStartWithIdle;

public:
    // ========== Constructor/Destructor ==========
    CTaskSimpleGrabbed(CPed* pGrabber, CTaskSimpleGrab* pGrabberTask);
    ~CTaskSimpleGrabbed() override;

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

    // ========== Grabbed-specific ==========
    CPed* GetGrabber() const { return m_pGrabber; }
    eGrabbedState GetState() const { return m_state; }
    
    // Called by grabber task when releasing
    void OnReleased();
    
    // Set line-up utility for position synchronization (owned by grabber task, NOT us)
    void SetLineUpUtility(CTaskUtilityLineUpPedWithPed* utility) { m_pLineUpUtility = utility; }
    
    // Set animation skip amount (synced with grabber)
    void SetAnimationSkip(float skip);
    
    // Set to start directly with idle animation (for snap case)
    void SetStartWithIdle(bool startWithIdle);
    
    // Called by grabber when performing an action (jab, throw, etc.)
    void PlayReactionAnimation(const char* animName);

private:
    // ========== Internal Methods ==========
    bool LoadAnimations();
    void StartGrabbedAnimation(CPed* ped);
    
    // ========== Animation Callbacks ==========
    static void OnAnimDeleted(CAnimBlendAssociation* anim, void* data);
    static void OnReactionAnimFinished(CAnimBlendAssociation* anim, void* data);
};
