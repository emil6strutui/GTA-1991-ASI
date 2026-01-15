#include "CTaskSimpleGrabbed.h"
#include "CTaskSimpleGrab.h"

#include <plugin.h>
#include <CAnimManager.h>
#include <CAnimBlendAssociation.h>
#include <CAnimBlendHierarchy.h>
#include <CStreaming.h>
#include <CStreamingInfo.h>
#include <CTimer.h>

using namespace plugin;

// ============================================================================
// Custom task type ID (outside normal range to avoid conflicts)
// ============================================================================
constexpr eTaskType TASK_SIMPLE_GRABBED = (eTaskType)9002;

// ============================================================================
// Our own no-op callback - MUST use this instead of nullptr or game's default
// The game's "default" callback at 0x4D6190 actually dereferences the data ptr!
// ============================================================================
static void NoOpAnimCallback(CAnimBlendAssociation*, void*)
{
    // Intentionally empty - does nothing, doesn't touch any pointers
}

// ============================================================================
// Helper: Convert IFP block index to model ID for streaming
// ============================================================================
static int IFPToModelId(int blockIndex)
{
    return 25575 + blockIndex;
}

// ============================================================================
// Helper: Check if a model is loaded
// ============================================================================
static bool IsModelLoaded(int modelId)
{
    return CStreaming::ms_aInfoForModel[modelId].m_nLoadState == 1; // LOADSTATE_LOADED
}

// ============================================================================
// Constructor
// ============================================================================
CTaskSimpleGrabbed::CTaskSimpleGrabbed(CPed* pGrabber, CTaskSimpleGrab* pGrabberTask)
    : CTaskSimple(plugin::dummy)
{
    m_pParentTask = nullptr;
    m_state = eGrabbedState::GRABBED;
    m_pGrabber = pGrabber;
    m_pGrabberTask = pGrabberTask;
    m_pAnim = nullptr;
    m_bAnimsReferenced = false;
    
    // Default offset (will be set by grabber)
    m_fOffsetForward = 0.7f;
    m_fOffsetRight = 0.0f;
    m_fOffsetZ = 0.0f;
}

// ============================================================================
// Destructor
// ============================================================================
CTaskSimpleGrabbed::~CTaskSimpleGrabbed()
{
    // CRITICAL: Detach callback BEFORE blend out to prevent crash
    // Must use NoOpAnimCallback, NOT nullptr (game always calls the callback)
    if (m_pAnim)
    {
        m_pAnim->SetDeleteCallback(NoOpAnimCallback, nullptr);
        m_pAnim->m_fBlendDelta = -4.0f;
        m_pAnim = nullptr;
    }

    // Release animation reference if we have one
    if (m_bAnimsReferenced)
    {
        int blockIndex = CAnimManager::GetAnimationBlockIndex(ANIM_BLOCK_NAME);
        if (blockIndex >= 0)
        {
            CAnimManager::RemoveAnimBlockRef(blockIndex);
        }
    }

    // Notify grabber that we're gone (if they didn't release us)
    if (m_pGrabberTask && m_state != eGrabbedState::RELEASED)
    {
        m_pGrabberTask->OnVictimLost();
    }
}

// ============================================================================
// CTask Interface
// ============================================================================

CTask* CTaskSimpleGrabbed::Clone()
{
    // Grabbed tasks shouldn't be cloned
    return new CTaskSimpleGrabbed(m_pGrabber, m_pGrabberTask);
}

CTask* CTaskSimpleGrabbed::GetSubTask()
{
    return nullptr; // Simple tasks have no subtasks
}

bool CTaskSimpleGrabbed::IsSimple()
{
    return true;
}

eTaskType CTaskSimpleGrabbed::GetId()
{
    return TASK_SIMPLE_GRABBED;
}

void CTaskSimpleGrabbed::StopTimer(CEvent* event)
{
    // Nothing to do - we don't use timers
}

bool CTaskSimpleGrabbed::MakeAbortable(CPed* ped, eAbortPriority priority, CEvent* event)
{
    // CRITICAL: Detach callback BEFORE blend out to prevent crash
    if (m_pAnim)
    {
        m_pAnim->SetDeleteCallback(NoOpAnimCallback, nullptr);
        m_pAnim->m_fBlendDelta = -8.0f; // Fast blend out
        m_pAnim = nullptr;
    }

    // Notify grabber we're escaping
    if (m_pGrabberTask)
    {
        m_pGrabberTask->OnVictimLost();
        m_pGrabberTask = nullptr;
    }

    m_state = eGrabbedState::FINISHED;
    return true;
}

// ============================================================================
// CTaskSimple Interface
// ============================================================================

bool CTaskSimpleGrabbed::ProcessPed(CPed* ped)
{
    // Check if finished
    if (m_state == eGrabbedState::FINISHED)
    {
        return true; // Task complete
    }

    // Check if released by grabber
    if (m_state == eGrabbedState::RELEASED)
    {
        // Blend out animation
        if (m_pAnim)
        {
            m_pAnim->m_fBlendDelta = -4.0f;
            if (m_pAnim->m_fBlendAmount <= 0.0f)
            {
                m_state = eGrabbedState::FINISHED;
                return true;
            }
        }
        else
        {
            m_state = eGrabbedState::FINISHED;
            return true;
        }
        return false;
    }

    // Check if grabber is still valid
    if (!m_pGrabber || m_pGrabber->m_fHealth <= 0.0f)
    {
        OnReleased();
        return false;
    }

    // Load animations if not loaded
    if (!m_bAnimsReferenced)
    {
        if (!LoadAnimations())
        {
            // Wait for animations
            return false;
        }
    }

    // Start grabbed animation if not started
    if (!m_pAnim)
    {
        StartGrabbedAnimation(ped);
    }

    // Sync position to grabber
    SyncPositionToGrabber(ped);

    return false; // Continue task
}

bool CTaskSimpleGrabbed::SetPedPosition(CPed* ped)
{
    // We control ped position via SyncPositionToGrabber
    // Return true to indicate we're handling position
    if (m_state == eGrabbedState::GRABBED && m_pGrabber)
    {
        SyncPositionToGrabber(ped);
        return true;
    }
    return false;
}

// ============================================================================
// Grabbed-specific
// ============================================================================

void CTaskSimpleGrabbed::OnReleased()
{
    m_state = eGrabbedState::RELEASED;
    m_pGrabberTask = nullptr;
}

void CTaskSimpleGrabbed::SetOffset(float forward, float right, float z)
{
    m_fOffsetForward = forward;
    m_fOffsetRight = right;
    m_fOffsetZ = z;
}

// ============================================================================
// Internal Methods
// ============================================================================

bool CTaskSimpleGrabbed::LoadAnimations()
{
    int blockIndex = CAnimManager::GetAnimationBlockIndex(ANIM_BLOCK_NAME);
    if (blockIndex < 0)
    {
        return false;
    }

    int modelId = IFPToModelId(blockIndex);

    // Check if loaded
    if (!IsModelLoaded(modelId))
    {
        // Request the animation block
        CStreaming::RequestModel(modelId, 0x10); // KEEP_IN_MEMORY
        CStreaming::LoadAllRequestedModels(false);

        if (!IsModelLoaded(modelId))
        {
            return false;
        }
    }

    CAnimManager::AddAnimBlockRef(blockIndex);
    m_bAnimsReferenced = true;

    return true;
}

void CTaskSimpleGrabbed::StartGrabbedAnimation(CPed* ped)
{
    if (!ped || !ped->m_pRwClump)
    {
        return;
    }

    CAnimBlock* animBlock = CAnimManager::GetAnimationBlock(ANIM_BLOCK_NAME);
    if (!animBlock)
    {
        return;
    }

    CAnimBlendHierarchy* hier = CAnimManager::GetAnimation(ANIM_GRABBED, animBlock);
    if (!hier)
    {
        return;
    }

    // Blend in with high priority to override any other animations
    m_pAnim = CAnimManager::BlendAnimation(ped->m_pRwClump, hier, 0x10, 32.0f);
    
    // Use SetDeleteCallback - fires when anim is deleted for ANY reason
    // This is safer for victim since animation can be interrupted by combat/damage
    if (m_pAnim)
    {
        m_pAnim->SetDeleteCallback(OnAnimDeleted, this);
    }
}

void CTaskSimpleGrabbed::SyncPositionToGrabber(CPed* ped)
{
    if (!ped || !m_pGrabber)
    {
        return;
    }

    // Get grabber position and heading
    CVector grabberPos = m_pGrabber->GetPosition();
    float grabberHeading = m_pGrabber->m_fCurrentRotation;

    // Calculate offset position
    // Forward is in the direction grabber is facing
    float sinH = sinf(grabberHeading);
    float cosH = cosf(grabberHeading);

    CVector newPos;
    newPos.x = grabberPos.x - sinH * m_fOffsetForward + cosH * m_fOffsetRight;
    newPos.y = grabberPos.y + cosH * m_fOffsetForward + sinH * m_fOffsetRight;
    newPos.z = grabberPos.z + m_fOffsetZ;

    // Set victim position
    ped->SetPosn(newPos);

    // Face the grabber (opposite direction)
    float victimHeading = grabberHeading + 3.14159f; // 180 degrees opposite
    while (victimHeading > 3.14159f) victimHeading -= 6.28318f;
    while (victimHeading < -3.14159f) victimHeading += 6.28318f;

    ped->m_fCurrentRotation = victimHeading;
    ped->m_fAimingRotation = victimHeading;
}

// ============================================================================
// Animation Callbacks
// ============================================================================

void CTaskSimpleGrabbed::OnAnimDeleted(CAnimBlendAssociation* anim, void* data)
{
    CTaskSimpleGrabbed* task = static_cast<CTaskSimpleGrabbed*>(data);
    if (!task)
    {
        return;
    }

    // CRITICAL: Clear pointer FIRST - animation may be deleted after this callback
    task->m_pAnim = nullptr;

    // Animation was deleted/interrupted - mark state accordingly
    // If we're still supposed to be grabbed, the animation ended unexpectedly
    if (task->m_state == eGrabbedState::GRABBED)
    {
        // Animation was interrupted (maybe by damage, etc.) - release the grab
        task->m_state = eGrabbedState::RELEASED;
    }
    else if (task->m_state == eGrabbedState::RELEASED)
    {
        // Normal release - animation blending out, now finished
        task->m_state = eGrabbedState::FINISHED;
    }
}
