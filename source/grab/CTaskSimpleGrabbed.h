#pragma once

#include <plugin.h>
#include <CTask.h>
#include <CPed.h>
#include <CAnimBlendAssociation.h>

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

private:
    // ========== Members ==========
    eGrabbedState m_state;
    CPed* m_pGrabber;                       // The ped grabbing us
    CTaskSimpleGrab* m_pGrabberTask;        // Grabber's task (for coordination)
    CAnimBlendAssociation* m_pAnim;         // Current animation
    bool m_bAnimsReferenced;                // Whether we've added anim block ref
    
    // Position offset from grabber (set when attached)
    float m_fOffsetForward;
    float m_fOffsetRight;
    float m_fOffsetZ;

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
    
    // Set position offset from grabber
    void SetOffset(float forward, float right, float z);

private:
    // ========== Internal Methods ==========
    bool LoadAnimations();
    void StartGrabbedAnimation(CPed* ped);
    void SyncPositionToGrabber(CPed* ped);
    
    // ========== Animation Callbacks ==========
    static void OnAnimDeleted(CAnimBlendAssociation* anim, void* data);
};
