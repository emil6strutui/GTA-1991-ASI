#include "CTaskSimpleGrab.h"
#include "CTaskSimpleGrabbed.h"

#include <plugin.h>
#include <CWorld.h>
#include <CAnimManager.h>
#include <CAnimBlendAssociation.h>
#include <CAnimBlendHierarchy.h>
#include <CStreaming.h>
#include <CStreamingInfo.h>
#include <CTimer.h>
#include <CGeneral.h>

using namespace plugin;

// ============================================================================
// Custom task type ID (outside normal range to avoid conflicts)
// ============================================================================
constexpr eTaskType TASK_SIMPLE_GRAB = (eTaskType)9001;

// ============================================================================
// Our own no-op callback - MUST use this instead of nullptr or game's default
// The game's "default" callback at 0x4D6190 actually dereferences the data ptr!
// ============================================================================
static void NoOpAnimCallback(CAnimBlendAssociation *, void *)
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
    return CStreaming::ms_aInfoForModel[modelId].m_nLoadState == LOADSTATE_LOADED;
}

// ============================================================================
// Constructor
// ============================================================================
CTaskSimpleGrab::CTaskSimpleGrab()
    : CTaskSimple(plugin::dummy)
{
    m_pParentTask = nullptr;
    m_state = eGrabState::REACHING;
    m_pVictim = nullptr;
    m_pVictimTask = nullptr;
    m_pAnim = nullptr;
    m_bAnimsReferenced = false;
    m_bMidpointChecked = false;
}

// ============================================================================
// Destructor
// ============================================================================
CTaskSimpleGrab::~CTaskSimpleGrab()
{
    // Release animation reference if we have one
    if (m_bAnimsReferenced)
    {
        int blockIndex = CAnimManager::GetAnimationBlockIndex(ANIM_BLOCK_NAME);
        if (blockIndex >= 0)
        {
            CAnimManager::RemoveAnimBlockRef(blockIndex);
        }
    }

    // CRITICAL: Detach callback BEFORE blending out to prevent callback to destroyed object
    if (m_pAnim)
    {
        m_pAnim->SetFinishCallback(NoOpAnimCallback, nullptr);
        m_pAnim->m_fBlendDelta = -4.0f;
        m_pAnim = nullptr;
    }

    // Notify victim task that we're gone
    if (m_pVictimTask)
    {
        m_pVictimTask->OnReleased();
        m_pVictimTask = nullptr;
    }
}

// ============================================================================
// CTask Interface
// ============================================================================

CTask *CTaskSimpleGrab::Clone()
{
    // Grab tasks shouldn't be cloned - they're unique per grab instance
    return new CTaskSimpleGrab();
}

CTask *CTaskSimpleGrab::GetSubTask()
{
    return nullptr; // Simple tasks have no subtasks
}

bool CTaskSimpleGrab::IsSimple()
{
    return true;
}

eTaskType CTaskSimpleGrab::GetId()
{
    return TASK_SIMPLE_GRAB;
}

void CTaskSimpleGrab::StopTimer(CEvent *event)
{
    // Nothing to do - we don't use timers
}

bool CTaskSimpleGrab::MakeAbortable(CPed *ped, eAbortPriority priority, CEvent *event)
{
    // CRITICAL: Detach callback FIRST, then blend out
    if (m_pAnim)
    {
        m_pAnim->SetFinishCallback(NoOpAnimCallback, nullptr);
        m_pAnim->m_fBlendDelta = -8.0f; // Fast blend out
        m_pAnim = nullptr;
    }

    // Release victim if we have one
    if (m_pVictimTask)
    {
        m_pVictimTask->OnReleased();
        m_pVictimTask = nullptr;
    }

    m_state = eGrabState::FINISHED;
    return true;
}

// ============================================================================
// CTaskSimple Interface
// ============================================================================

bool CTaskSimpleGrab::ProcessPed(CPed *ped)
{
    // Check if finished
    if (m_state == eGrabState::FINISHED)
    {
        return true; // Task complete
    }

    // Load animations if not loaded
    if (!m_bAnimsReferenced)
    {
        if (!LoadAnimations())
        {
            // Animations not ready yet, wait
            return false;
        }
    }

    // Start grab animation if not started
    if (!m_pAnim)
    {
        StartGrabAnimation(ped);
        if (!m_pAnim)
        {
            // Failed to start animation
            m_state = eGrabState::FINISHED;
            return true;
        }
    }

    // Check midpoint for victim attachment
    if (m_state == eGrabState::REACHING && !m_bMidpointChecked)
    {
        // Get animation progress (0.0 to 1.0)
        float progress = 0.0f;
        if (m_pAnim && m_pAnim->m_pHierarchy)
        {
            progress = m_pAnim->m_fCurrentTime / m_pAnim->m_pHierarchy->m_fTotalTime;
        }

        // Check at midpoint
        if (progress >= MIDPOINT_TIME)
        {
            m_bMidpointChecked = true;

            // Try to find and attach victim
            CPed *victim = FindValidVictim(ped);
            if (victim)
            {
                AttachVictim(ped, victim);
                m_state = eGrabState::ATTACHED;
            }
            else
            {
                // No valid victim - abort early
                AbortGrab(ped);
            }
        }
    }

    // If we're aborting, check if animation is done blending out
    if (m_state == eGrabState::ABORTING)
    {
        if (!m_pAnim || m_pAnim->m_fBlendAmount <= 0.0f)
        {
            m_pAnim = nullptr;
            m_state = eGrabState::FINISHED;
            return true;
        }
    }

    // Note: Animation finish is handled by callback (OnAnimFinish)
    // which sets m_state = FINISHED

    return false; // Continue task
}

bool CTaskSimpleGrab::SetPedPosition(CPed *ped)
{
    // We don't override ped position
    return false;
}

// ============================================================================
// Internal Methods
// ============================================================================

bool CTaskSimpleGrab::LoadAnimations()
{
    int blockIndex = CAnimManager::GetAnimationBlockIndex(ANIM_BLOCK_NAME);
    if (blockIndex < 0)
    {
        return false; // Block not found
    }

    int modelId = IFPToModelId(blockIndex);

    // Check if loaded
    if (!IsModelLoaded(modelId))
    {
        // Request the animation block
        CStreaming::RequestModel(modelId, KEEP_IN_MEMORY);
        CStreaming::LoadAllRequestedModels(false);

        // Still not loaded? Wait for next frame
        if (!IsModelLoaded(modelId))
        {
            return false;
        }
    }

    // Add reference so it doesn't get unloaded
    CAnimManager::AddAnimBlockRef(blockIndex);
    m_bAnimsReferenced = true;

    return true;
}

void CTaskSimpleGrab::StartGrabAnimation(CPed *ped)
{
    if (!ped || !ped->m_pRwClump)
    {
        return;
    }

    // If we already have an animation, detach its callback first
    if (m_pAnim)
    {
        m_pAnim->SetFinishCallback(NoOpAnimCallback, nullptr);
        m_pAnim = nullptr;
    }

    int blockIndex = CAnimManager::GetAnimationBlockIndex(ANIM_BLOCK_NAME);
    if (blockIndex < 0)
    {
        return;
    }

    CAnimBlock *animBlock = CAnimManager::GetAnimationBlock(ANIM_BLOCK_NAME);
    if (!animBlock)
    {
        return;
    }

    // Get the animation hierarchy
    CAnimBlendHierarchy *hier = CAnimManager::GetAnimation(ANIM_GRAB, animBlock);
    if (!hier)
    {
        return;
    }

    // Blend in the grab animation
    m_pAnim = CAnimManager::BlendAnimation(ped->m_pRwClump, hier, 0x10, 8.0f);

    // Set finish callback - fires when animation reaches end
    if (m_pAnim)
    {
        m_pAnim->SetFinishCallback(OnAnimFinish, this);
    }
}

CPed *CTaskSimpleGrab::FindValidVictim(CPed *grabber)
{
    if (!grabber)
    {
        return nullptr;
    }

    CVector grabberPos = grabber->GetPosition();
    float grabberHeading = grabber->m_fCurrentRotation;

    // Find nearby peds
    short numFound = 0;
    CEntity *entities[16];

    CWorld::FindObjectsInRange(
        grabberPos,
        GRAB_RANGE,
        true, // 2D search
        &numFound,
        16,
        entities,
        false, // buildings
        false, // vehicles
        true,  // PEDS
        false, // objects
        false  // dummies
    );

    CPed *bestVictim = nullptr;
    float bestScore = -1.0f;

    for (int i = 0; i < numFound; i++)
    {
        CEntity *entity = entities[i];
        if (!entity || entity == grabber)
        {
            continue;
        }

        // Must be a ped
        if (entity->m_nType != 3) // ENTITY_TYPE_PED = 3
        {
            continue;
        }

        CPed *ped = (CPed *)entity;

        // Skip dead/dying peds
        if (ped->m_fHealth <= 0.0f)
        {
            continue;
        }

        // Skip peds in vehicles
        if (ped->m_pVehicle)
        {
            continue;
        }

        // Check angle - victim should be in front of grabber
        CVector toVictim = ped->GetPosition() - grabberPos;
        float angleToVictim = atan2f(-toVictim.x, toVictim.y); // SA uses this convention
        float angleDiff = grabberHeading - angleToVictim;

        // Normalize angle to -PI to PI
        while (angleDiff > 3.14159f)
            angleDiff -= 6.28318f;
        while (angleDiff < -3.14159f)
            angleDiff += 6.28318f;

        float angleDiffDeg = fabsf(angleDiff) * (180.0f / 3.14159f);

        if (angleDiffDeg > GRAB_ANGLE)
        {
            continue; // Not in front
        }

        // Score by distance and angle (prefer closer and more centered)
        float dist = toVictim.Magnitude2D();
        float score = (GRAB_RANGE - dist) * (GRAB_ANGLE - angleDiffDeg);

        if (score > bestScore)
        {
            bestScore = score;
            bestVictim = ped;
        }
    }

    return bestVictim;
}

void CTaskSimpleGrab::AttachVictim(CPed *grabber, CPed *victim)
{
    if (!grabber || !victim)
    {
        return;
    }

    m_pVictim = victim;

    // Create and assign victim task
    m_pVictimTask = new CTaskSimpleGrabbed(grabber, this);

    // Set offset (face to face)
    m_pVictimTask->SetOffset(VICTIM_OFFSET_FORWARD, 0.0f, VICTIM_OFFSET_Z);

    // Assign to victim's PHYSICAL_RESPONSE slot (highest priority)
    CTaskManager *taskMgr = &victim->m_pIntelligence->m_TaskMgr;
    taskMgr->SetTask((CTask *)m_pVictimTask, TASK_PRIMARY_PHYSICAL_RESPONSE, false);
}

void CTaskSimpleGrab::AbortGrab(CPed *ped)
{
    // Detach callback FIRST, then blend out
    if (m_pAnim)
    {
        m_pAnim->SetFinishCallback(NoOpAnimCallback, nullptr);
        m_pAnim->m_fBlendDelta = -8.0f; // Fast blend out
        // Note: Don't null m_pAnim yet - we check blend amount in ProcessPed
    }

    m_state = eGrabState::ABORTING;
}

void CTaskSimpleGrab::FinishGrab()
{
    m_state = eGrabState::FINISHED;
}

void CTaskSimpleGrab::OnVictimLost()
{
    // Victim died, escaped, or task was aborted
    m_pVictim = nullptr;
    m_pVictimTask = nullptr;

    // End the grab
    m_state = eGrabState::FINISHED;
}

// ============================================================================
// Animation Callbacks
// ============================================================================

void CTaskSimpleGrab::OnAnimFinish(CAnimBlendAssociation *anim, void *data)
{
    CTaskSimpleGrab *task = static_cast<CTaskSimpleGrab *>(data);
    if (!task)
    {
        return;
    }

    // CRITICAL: Clear pointer FIRST (animation may be deleted after callback returns)
    task->m_pAnim = nullptr;

    // Then update state
    task->FinishGrab();
}
