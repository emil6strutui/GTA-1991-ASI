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
    m_pVictimPed = nullptr;  // Will be set in first ProcessPed call
    m_pGrabberTask = pGrabberTask;
    m_pAnim = nullptr;
    m_bAnimsReferenced = false;
    m_bCollisionDisabled = false;
    
    // Default offset (will be set by grabber)
    m_fOffsetForward = 0.7f;
    m_fOffsetRight = 0.0f;
    m_fOffsetZ = 0.0f;
    
    // Animation skip (synced with grabber)
    m_fAnimationSkip = 0.0f;
    
    // Start position not set yet
    m_bStartPositionSet = false;
}

// ============================================================================
// Destructor
// ============================================================================
CTaskSimpleGrabbed::~CTaskSimpleGrabbed()
{
    // Restore collision if we disabled it
    if (m_bCollisionDisabled && m_pVictimPed)
    {
        m_pVictimPed->bCollidable = true;
        m_bCollisionDisabled = false;
    }

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
    // Restore collision
    if (m_bCollisionDisabled && ped)
    {
        ped->bCollidable = true;
        m_bCollisionDisabled = false;
    }

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

    // First frame: save ped pointer and disable collision
    if (!m_pVictimPed && ped)
    {
        m_pVictimPed = ped;
        
        // Disable collision so victim doesn't interfere with grab
        if (ped->bCollidable)
        {
            ped->bCollidable = false;
            m_bCollisionDisabled = true;
        }
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
    // Restore collision
    if (m_bCollisionDisabled && m_pVictimPed)
    {
        m_pVictimPed->bCollidable = true;
        m_bCollisionDisabled = false;
    }

    m_state = eGrabbedState::RELEASED;
    m_pGrabberTask = nullptr;
}

void CTaskSimpleGrabbed::SetOffset(float forward, float right, float z)
{
    m_fOffsetForward = forward;
    m_fOffsetRight = right;
    m_fOffsetZ = z;
}

void CTaskSimpleGrabbed::SetAnimationSkip(float skip)
{
    m_fAnimationSkip = skip;
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

    // Calculate blend delta based on skip amount
    // Higher skip = faster blend (snappier for close grabs)
    float blendDelta = 8.0f;
    if (m_fAnimationSkip > 0.4f)
    {
        blendDelta = 16.0f;
    }

    // Blend in with high priority to override any other animations
    m_pAnim = CAnimManager::BlendAnimation(ped->m_pRwClump, hier, 0x10, blendDelta);
    
    if (m_pAnim)
    {
        // Skip animation forward to sync with grabber
        float skipTime = m_fAnimationSkip * m_pAnim->m_pHierarchy->m_fTotalTime;
        m_pAnim->m_fCurrentTime = skipTime;
        
        // Use SetDeleteCallback - fires when anim is deleted for ANY reason
        // This is safer for victim since animation can be interrupted by combat/damage
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
    
    // Calculate direction vectors
    float sinH = sinf(grabberHeading);
    float cosH = cosf(grabberHeading);
    
    // Always face the grabber (opposite direction)
    float victimHeading = grabberHeading + 3.14159f; // 180 degrees opposite
    while (victimHeading > 3.14159f) victimHeading -= 6.28318f;
    while (victimHeading < -3.14159f) victimHeading += 6.28318f;
    
    ped->m_fCurrentRotation = victimHeading;
    ped->m_fAimingRotation = victimHeading;

    // Calculate final grab position (where victim ends up when fully grabbed)
    CVector finalPos;
    finalPos.x = grabberPos.x - sinH * m_fOffsetForward + cosH * m_fOffsetRight;
    finalPos.y = grabberPos.y + cosH * m_fOffsetForward + sinH * m_fOffsetRight;
    finalPos.z = grabberPos.z + m_fOffsetZ;

    // FIRST FRAME: Set the start position based on animation skip
    // The animation has root motion that moves victim forward (towards grabber)
    // So we place victim at a distance where the remaining animation will bring them to finalPos
    if (!m_bStartPositionSet)
    {
        m_bStartPositionSet = true;
        
        // Calculate how much animation movement remains
        // remaining_ratio = 1.0 - skip (if skip=0.6, only 40% of movement left)
        float remainingRatio = 1.0f - m_fAnimationSkip;
        float remainingDistance = remainingRatio * ANIM_FORWARD_DISTANCE;
        
        // Start position = final position + remaining distance (away from grabber)
        // "Away from grabber" is in the direction the grabber is facing
        CVector startPos;
        startPos.x = finalPos.x - sinH * remainingDistance;
        startPos.y = finalPos.y + cosH * remainingDistance;
        startPos.z = finalPos.z;
        
        ped->SetPosn(startPos);
        return;
    }

    // SUBSEQUENT FRAMES: Check if animation is complete
    float animProgress = 1.0f; // Default to complete if no anim
    if (m_pAnim && m_pAnim->m_pHierarchy && m_pAnim->m_pHierarchy->m_fTotalTime > 0.0f)
    {
        animProgress = m_pAnim->m_fCurrentTime / m_pAnim->m_pHierarchy->m_fTotalTime;
    }
    
    // Once animation is complete (or nearly complete), lock to final position
    // This ensures perfect positioning at the end
    if (animProgress >= 0.95f)
    {
        ped->SetPosn(finalPos);
    }
    // Otherwise: let animation root motion handle movement
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
