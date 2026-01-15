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
// No-op callback - MUST use this instead of nullptr
// ============================================================================
void CTaskSimpleGrab::NoOpAnimCallback(CAnimBlendAssociation*, void*)
{
    // Intentionally empty - does nothing, doesn't touch any pointers
}

// ============================================================================
// Constructor
// ============================================================================
CTaskSimpleGrab::CTaskSimpleGrab()
    : CTaskSimple(plugin::dummy)
{
    m_pParentTask = nullptr;
    m_state = eGrabState::INIT;
    m_pVictim = nullptr;
    m_pVictimTask = nullptr;
    m_pAnim = nullptr;
    m_bAnimsReferenced = false;
    m_fGrabDistance = 0.0f;
}

// ============================================================================
// Destructor
// ============================================================================
CTaskSimpleGrab::~CTaskSimpleGrab()
{
    // CRITICAL: Detach callback BEFORE blending out to prevent callback to destroyed object
    if (m_pAnim)
    {
        m_pAnim->SetFinishCallback(NoOpAnimCallback, nullptr);
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

CTask* CTaskSimpleGrab::Clone()
{
    return new CTaskSimpleGrab();
}

CTask* CTaskSimpleGrab::GetSubTask()
{
    return nullptr;
}

bool CTaskSimpleGrab::IsSimple()
{
    return true;
}

eTaskType CTaskSimpleGrab::GetId()
{
    return TASK_SIMPLE_GRAB;
}

void CTaskSimpleGrab::StopTimer(CEvent* event)
{
    // Nothing to do
}

bool CTaskSimpleGrab::MakeAbortable(CPed* ped, eAbortPriority priority, CEvent* event)
{
    if (m_pAnim)
    {
        m_pAnim->SetFinishCallback(NoOpAnimCallback, nullptr);
        m_pAnim->m_fBlendDelta = -8.0f;
        m_pAnim = nullptr;
    }

    if (m_pVictimTask)
    {
        m_pVictimTask->OnReleased();
        m_pVictimTask = nullptr;
    }

    m_state = eGrabState::FINISHED;
    return true;
}

// ============================================================================
// CTaskSimple Interface - Main Logic
// ============================================================================

bool CTaskSimpleGrab::ProcessPed(CPed* ped)
{
    // Already finished?
    if (m_state == eGrabState::FINISHED)
    {
        return true;
    }

    // Load animations if not loaded
    if (!m_bAnimsReferenced)
    {
        if (!LoadAnimations())
        {
            return false; // Wait for animations
        }
    }

    // ========== INIT STATE: Check for victim immediately ==========
    if (m_state == eGrabState::INIT)
    {
        float distance = 0.0f;
        CPed* victim = FindValidVictim(ped, &distance);

        if (victim)
        {
            // VICTIM FOUND: Start both animations with distance-based skip
            StartGrabWithVictim(ped, victim, distance);
            m_state = eGrabState::ATTACHED;
        }
        else
        {
            // NO VICTIM: Start animation, will abort at 40%
            StartGrabNoVictim(ped);
            m_state = eGrabState::NO_VICTIM;
        }
        return false;
    }

    // ========== NO_VICTIM STATE: Play to 40% then abort ==========
    if (m_state == eGrabState::NO_VICTIM)
    {
        if (m_pAnim && m_pAnim->m_pHierarchy)
        {
            float progress = m_pAnim->m_fCurrentTime / m_pAnim->m_pHierarchy->m_fTotalTime;
            
            if (progress >= ABORT_PROGRESS)
            {
                // Reached 40%, abort
                AbortGrab();
            }
        }
        return false;
    }

    // ========== ATTACHED STATE: Playing grab animation ==========
    if (m_state == eGrabState::ATTACHED)
    {
        // Animation finish is handled by callback, which transitions to HOLDING
        return false;
    }

    // ========== HOLDING STATE: Holding victim, waiting for input ==========
    if (m_state == eGrabState::HOLDING)
    {
        // TODO: Check for player input (attack, release, etc.)
        // TODO: Play grab idle animation
        // For now, just continue holding
        return false;
    }

    // ========== ABORTING STATE: Wait for blend out ==========
    if (m_state == eGrabState::ABORTING)
    {
        if (!m_pAnim || m_pAnim->m_fBlendAmount <= 0.0f)
        {
            m_pAnim = nullptr;
            m_state = eGrabState::FINISHED;
            return true;
        }
        return false;
    }

    return false;
}

bool CTaskSimpleGrab::SetPedPosition(CPed* ped)
{
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
        return false;
    }

    int modelId = IFPToModelId(blockIndex);

    if (!IsModelLoaded(modelId))
    {
        CStreaming::RequestModel(modelId, KEEP_IN_MEMORY);
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

CPed* CTaskSimpleGrab::FindValidVictim(CPed* grabber, float* outDistance)
{
    if (!grabber)
    {
        return nullptr;
    }

    CVector grabberPos = grabber->GetPosition();
    float grabberHeading = grabber->m_fCurrentRotation;

    short numFound = 0;
    CEntity* entities[16];

    CWorld::FindObjectsInRange(
        grabberPos,
        GRAB_RANGE,
        true,
        &numFound,
        16,
        entities,
        false, false, true, false, false
    );

    CPed* bestVictim = nullptr;
    float bestScore = -1.0f;
    float bestDistance = 0.0f;

    for (int i = 0; i < numFound; i++)
    {
        CEntity* entity = entities[i];
        if (!entity || entity == grabber)
        {
            continue;
        }

        if (entity->m_nType != 3) // ENTITY_TYPE_PED
        {
            continue;
        }

        CPed* ped = (CPed*)entity;

        if (ped->m_fHealth <= 0.0f)
        {
            continue;
        }

        if (ped->m_pVehicle)
        {
            continue;
        }

        CVector toVictim = ped->GetPosition() - grabberPos;
        float angleToVictim = atan2f(-toVictim.x, toVictim.y);
        float angleDiff = grabberHeading - angleToVictim;

        while (angleDiff > 3.14159f) angleDiff -= 6.28318f;
        while (angleDiff < -3.14159f) angleDiff += 6.28318f;

        float angleDiffDeg = fabsf(angleDiff) * (180.0f / 3.14159f);

        if (angleDiffDeg > GRAB_ANGLE)
        {
            continue;
        }

        float dist = toVictim.Magnitude2D();
        float score = (GRAB_RANGE - dist) * (GRAB_ANGLE - angleDiffDeg);

        if (score > bestScore)
        {
            bestScore = score;
            bestVictim = ped;
            bestDistance = dist;
        }
    }

    if (outDistance && bestVictim)
    {
        *outDistance = bestDistance;
    }

    return bestVictim;
}

float CTaskSimpleGrab::CalculateAnimSkip(float distance) const
{
    // Calculate how much of the "reach" animation to skip based on distance
    // - Very close (distance ~0): Skip most of reach, start near REACH_END_PROGRESS
    // - At max range: Start from 0 (full animation)
    
    // Clamp distance to valid range
    float clampedDist = (distance > GRAB_RANGE) ? GRAB_RANGE : distance;
    if (clampedDist < 0.0f) clampedDist = 0.0f;
    
    // Distance ratio: 0 = touching, 1 = max range
    float distanceRatio = clampedDist / GRAB_RANGE;
    
    // Inverse: 1 = touching (skip a lot), 0 = max range (skip nothing)
    float skipRatio = 1.0f - distanceRatio;
    
    // Scale by reach end progress
    // Close: skipAmount = REACH_END_PROGRESS (start near end of reach)
    // Far: skipAmount = 0 (start from beginning)
    float skipAmount = skipRatio * REACH_END_PROGRESS;
    
    return skipAmount;
}

void CTaskSimpleGrab::StartGrabWithVictim(CPed* grabber, CPed* victim, float distance)
{
    if (!grabber || !victim || !grabber->m_pRwClump || !victim->m_pRwClump)
    {
        return;
    }

    m_pVictim = victim;
    m_fGrabDistance = distance;

    // Get animation block
    CAnimBlock* animBlock = CAnimManager::GetAnimationBlock(ANIM_BLOCK_NAME);
    if (!animBlock)
    {
        return;
    }

    // Calculate skip amount based on distance
    float skipAmount = CalculateAnimSkip(distance);

    // ========== START PLAYER ANIMATION ==========
    CAnimBlendHierarchy* grabHier = CAnimManager::GetAnimation(ANIM_GRAB, animBlock);
    if (grabHier)
    {
        // Use high blend delta for fast blend-in
        float blendDelta = 8.0f;
        
        // If very close, use even faster blend (more snappy)
        if (skipAmount > 0.4f)
        {
            blendDelta = 16.0f;
        }
        
        m_pAnim = CAnimManager::BlendAnimation(grabber->m_pRwClump, grabHier, 0x10, blendDelta);
        
        if (m_pAnim)
        {
            // Skip animation forward based on distance
            float skipTime = skipAmount * m_pAnim->m_pHierarchy->m_fTotalTime;
            m_pAnim->m_fCurrentTime = skipTime;
            
            // Set finish callback
            m_pAnim->SetFinishCallback(OnAnimFinish, this);
        }
    }

    // ========== CREATE VICTIM TASK AND START VICTIM ANIMATION ==========
    m_pVictimTask = new CTaskSimpleGrabbed(grabber, this);
    m_pVictimTask->SetOffset(VICTIM_OFFSET_FORWARD, 0.0f, VICTIM_OFFSET_Z);
    
    // Pass the skip amount to the victim task so it can sync
    m_pVictimTask->SetAnimationSkip(skipAmount);

    // Assign to victim's PHYSICAL_RESPONSE slot (highest priority)
    CTaskManager* taskMgr = &victim->m_pIntelligence->m_TaskMgr;
    taskMgr->SetTask((CTask*)m_pVictimTask, TASK_PRIMARY_PHYSICAL_RESPONSE, false);
}

void CTaskSimpleGrab::StartGrabNoVictim(CPed* grabber)
{
    if (!grabber || !grabber->m_pRwClump)
    {
        return;
    }

    CAnimBlock* animBlock = CAnimManager::GetAnimationBlock(ANIM_BLOCK_NAME);
    if (!animBlock)
    {
        return;
    }

    CAnimBlendHierarchy* grabHier = CAnimManager::GetAnimation(ANIM_GRAB, animBlock);
    if (grabHier)
    {
        // Normal blend-in speed
        m_pAnim = CAnimManager::BlendAnimation(grabber->m_pRwClump, grabHier, 0x10, 8.0f);
        
        if (m_pAnim)
        {
            // No callback needed - we'll abort at 40% manually
            m_pAnim->SetFinishCallback(NoOpAnimCallback, nullptr);
        }
    }
}

void CTaskSimpleGrab::AbortGrab()
{
    if (m_pAnim)
    {
        m_pAnim->SetFinishCallback(NoOpAnimCallback, nullptr);
        m_pAnim->m_fBlendDelta = -8.0f; // Fast blend out
    }

    m_state = eGrabState::ABORTING;
}

void CTaskSimpleGrab::FinishGrab()
{
    m_state = eGrabState::FINISHED;
}

void CTaskSimpleGrab::OnVictimLost()
{
    m_pVictim = nullptr;
    m_pVictimTask = nullptr;
    m_state = eGrabState::FINISHED;
}

// ============================================================================
// Animation Callbacks
// ============================================================================

void CTaskSimpleGrab::OnAnimFinish(CAnimBlendAssociation* anim, void* data)
{
    CTaskSimpleGrab* task = static_cast<CTaskSimpleGrab*>(data);
    if (!task)
    {
        return;
    }

    // CRITICAL: Clear pointer FIRST
    task->m_pAnim = nullptr;

    // If we have a victim, transition to holding state
    // Otherwise finish the grab
    if (task->m_pVictim && task->m_pVictimTask)
    {
        task->m_state = eGrabState::HOLDING;
        // TODO: Start grab idle animation here
    }
    else
    {
        task->FinishGrab();
    }
}
