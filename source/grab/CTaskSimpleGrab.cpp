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
    m_currentAction = eGrabAction::NONE;
    m_pGrabber = nullptr;
    m_pVictim = nullptr;
    m_pVictimTask = nullptr;
    m_pAnim = nullptr;
    m_bAnimsReferenced = false;
    m_fGrabDistance = 0.0f;
    m_pLineUpUtility = nullptr;
}

// ============================================================================
// Destructor
// ============================================================================
CTaskSimpleGrab::~CTaskSimpleGrab()
{
    // Restore collision ignore on grabber
    if (m_pGrabber)
    {
        m_pGrabber->m_pEntityIgnoredCollision = nullptr;
    }
    
    // Restore collision ignore on victim
    if (m_pVictim)
    {
        m_pVictim->m_pEntityIgnoredCollision = nullptr;
    }

    // R* pattern for custom animation blocks (see CTaskSimpleGangDriveBy):
    // 1. Release block ref FIRST
    // 2. Then detach callback - NO blend delta!
    
    if (m_bAnimsReferenced)
    {
        int blockIndex = CAnimManager::GetAnimationBlockIndex(ANIM_BLOCK_NAME);
        if (blockIndex >= 0)
        {
            CAnimManager::RemoveAnimBlockRef(blockIndex);
        }
        m_bAnimsReferenced = false;
    }

    if (m_pAnim)
    {
        m_pAnim->SetFinishCallback(NoOpAnimCallback, nullptr);
        // NO blend delta - animation continues and cleans itself up
    }

    // Notify victim task that we're gone
    if (m_pVictimTask)
    {
        m_pVictimTask->OnReleased();
        m_pVictimTask = nullptr;
    }
    
    // Clean up line-up utility
    if (m_pLineUpUtility)
    {
        delete m_pLineUpUtility;
        m_pLineUpUtility = nullptr;
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
    // Restore collision ignore
    if (m_pGrabber)
    {
        m_pGrabber->m_pEntityIgnoredCollision = nullptr;
    }
    if (m_pVictim)
    {
        m_pVictim->m_pEntityIgnoredCollision = nullptr;
    }

    // R* pattern for custom animation blocks: just detach callback, NO blend delta
    if (m_pAnim)
    {
        m_pAnim->SetFinishCallback(NoOpAnimCallback, nullptr);
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
            if (distance < SNAP_DISTANCE)
            {
                // VERY CLOSE: Snap directly to holding state
                StartHoldingImmediate(ped, victim);
                m_state = eGrabState::HOLDING;
            }
            else
            {
                // VICTIM FOUND: Start both animations with distance-based skip
                StartGrabWithVictim(ped, victim, distance);
                m_state = eGrabState::ATTACHED;
            }
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
        // Lock rotation during animation (prevent mouse look)
        ped->m_fAimingRotation = ped->m_fCurrentRotation;
        
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
        // Lock rotation during animation (prevent mouse look)
        ped->m_fAimingRotation = ped->m_fCurrentRotation;
        
        // Animation finish is handled by callback, which transitions to HOLDING
        return false;
    }

    // ========== HOLDING STATE: Holding victim, waiting for input ==========
    if (m_state == eGrabState::HOLDING)
    {
        if (IsAttackPressed())
        {
            StartAction(eGrabAction::JAB);
        }
        return false;
    }

    // ========== ACTION STATE: Performing an action (jab, throw, etc.) ==========
    if (m_state == eGrabState::ACTION)
    {
        // Lock rotation during action
        ped->m_fAimingRotation = ped->m_fCurrentRotation;
        
        // Animation finish is handled by OnActionAnimFinish callback
        return false;
    }

    // ========== ABORTING STATE: Wait for blend out ==========
    if (m_state == eGrabState::ABORTING)
    {
        if (!m_pAnim || m_pAnim->m_fBlendAmount <= 0.0f)
        {
            m_pAnim = nullptr;
            m_pGrabber = nullptr;
            m_state = eGrabState::FINISHED;
            return true;
        }
        return false;
    }

    return false;
}

bool CTaskSimpleGrab::SetPedPosition(CPed* ped)
{
    // We don't control the grabber's position - only the victim's
    // The victim's position is controlled by CTaskSimpleGrabbed::SetPedPosition
    // via the line-up utility we create
    
    // However, during grab animation we want to prevent the grabber from moving
    // based on animation root motion. We achieve this by returning false and
    // letting ProcessPed lock the rotation.
    
    // Return false to let the game handle grabber position normally
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

void CTaskSimpleGrab::CreateLineUpUtility(float distance)
{
    // Clean up existing utility if any
    if (m_pLineUpUtility)
    {
        delete m_pLineUpUtility;
        m_pLineUpUtility = nullptr;
    }
    
    // Calculate start and end offsets in local space (relative to grabber)
    // Local space: X = right, Y = forward, Z = up
    
    // Start offset: Where victim starts (further away based on distance)
    // The victim starts at the actual distance from grabber
    CVector startOffset(0.0f, distance, 0.0f);
    
    // End offset: Where victim ends up when fully grabbed (holding position)
    CVector endOffset(0.0f, VICTIM_OFFSET_FORWARD, VICTIM_OFFSET_Z);
    
    // Create the utility with reach end progress matching our animation phase
    m_pLineUpUtility = new CTaskUtilityLineUpPedWithPed(
        startOffset,
        endOffset,
        REACH_END_PROGRESS  // Position locks at this animation progress
    );
}

void CTaskSimpleGrab::StartGrabWithVictim(CPed* grabber, CPed* victim, float distance)
{
    if (!grabber || !victim || !grabber->m_pRwClump || !victim->m_pRwClump)
    {
        return;
    }

    // Store pointers
    m_pGrabber = grabber;
    m_pVictim = victim;
    m_fGrabDistance = distance;

    // ========== 1. SET COLLISION IGNORE (both ways) ==========
    grabber->m_pEntityIgnoredCollision = victim;
    victim->m_pEntityIgnoredCollision = grabber;

    // ========== 2. CREATE LINE-UP UTILITY ==========
    // This handles animation-synced positioning of the victim
    CreateLineUpUtility(distance);

    // ========== 3. GET ANIMATION BLOCK ==========
    CAnimBlock* animBlock = CAnimManager::GetAnimationBlock(ANIM_BLOCK_NAME);
    if (!animBlock)
    {
        return;
    }

    // Calculate skip amount based on distance
    float skipAmount = CalculateAnimSkip(distance);

    // ========== 4. START PLAYER ANIMATION ==========
    CAnimBlendHierarchy* grabHier = CAnimManager::GetAnimation(ANIM_GRAB, animBlock);
    if (grabHier)
    {
        // Use high blend delta for fast blend-in
        // Very close = faster blend (snappier)
        float blendDelta = (skipAmount > 0.4f) ? 16.0f : 8.0f;
        
        // Animation flags:
        // - NOT using ANIMATION_PARTIAL (0x10) - this is a full-body animation
        // - ANIMATION_IS_BLEND_AUTO_REMOVE (0x04) - auto-delete when blended out
        // - No velocity extraction needed for grabber
        const int animFlags = 0x04;  // ANIMATION_IS_BLEND_AUTO_REMOVE
        
        m_pAnim = CAnimManager::BlendAnimation(grabber->m_pRwClump, grabHier, animFlags, blendDelta);
        
        if (m_pAnim)
        {
            // Make animation reference its own block (R* pattern)
            // This ensures block stays loaded while animation is playing
            m_pAnim->ReferenceAnimBlock();
            
            // Skip animation forward based on distance
            // IMPORTANT: Use SetCurrentTime() to properly update blend node keyframe state
            if (skipAmount > 0.0f && m_pAnim->m_pHierarchy)
            {
                float skipTime = skipAmount * m_pAnim->m_pHierarchy->m_fTotalTime;
                m_pAnim->SetCurrentTime(skipTime);
            }
            
            // Set finish callback
            m_pAnim->SetFinishCallback(OnAnimFinish, this);
        }
    }

    // ========== 5. CREATE VICTIM TASK ==========
    // Check if victim has valid intelligence (some ped types may not)
    if (!victim->m_pIntelligence)
    {
        // Can't assign task to victim without intelligence
        return;
    }
    
    m_pVictimTask = new CTaskSimpleGrabbed(grabber, this);
    
    // Pass the line-up utility to the victim task for position synchronization
    m_pVictimTask->SetLineUpUtility(m_pLineUpUtility);
    
    // Pass the skip amount to the victim task so it can sync its animation
    m_pVictimTask->SetAnimationSkip(skipAmount);

    // Assign to victim's PHYSICAL_RESPONSE slot (highest priority)
    CTaskManager* taskMgr = &victim->m_pIntelligence->m_TaskMgr;
    taskMgr->SetTask(m_pVictimTask, TASK_PRIMARY_PHYSICAL_RESPONSE, false);
}

void CTaskSimpleGrab::StartGrabNoVictim(CPed* grabber)
{
    if (!grabber || !grabber->m_pRwClump)
    {
        return;
    }

    m_pGrabber = grabber;

    CAnimBlock* animBlock = CAnimManager::GetAnimationBlock(ANIM_BLOCK_NAME);
    if (!animBlock)
    {
        return;
    }

    CAnimBlendHierarchy* grabHier = CAnimManager::GetAnimation(ANIM_GRAB, animBlock);
    if (grabHier)
    {
        // Animation flags:
        // - NOT using ANIMATION_PARTIAL - this is a full-body animation
        // - ANIMATION_IS_BLEND_AUTO_REMOVE (0x04) - auto-delete when blended out
        const int animFlags = 0x04;  // ANIMATION_IS_BLEND_AUTO_REMOVE
        
        m_pAnim = CAnimManager::BlendAnimation(grabber->m_pRwClump, grabHier, ANIMATION_UNLOCK_LAST_FRAME, 8.0f);
        
        if (m_pAnim)
        {
            // Make animation reference its own block (R* pattern)
            m_pAnim->ReferenceAnimBlock();
            
            // No callback needed - we'll abort at 40% manually
            m_pAnim->SetFinishCallback(NoOpAnimCallback, nullptr);
        }
    }
}

void CTaskSimpleGrab::AbortGrab()
{
    // R* pattern for custom animation blocks: just detach callback, NO blend delta
    if (m_pAnim)
    {
        if (m_pGrabber && m_pGrabber->m_pRwClump)
        {
            CAnimManager::BlendAnimation(
                m_pGrabber->m_pRwClump,
                m_pGrabber->m_nAnimGroup,  // Usually ANIM_GROUP_DEFAULT or ped's anim group
                ANIM_DEFAULT_IDLE_STANCE,
                1000.0f  // Positive blend delta for blend-in speed
            );
        }

        m_pAnim->SetFinishCallback(NoOpAnimCallback, nullptr);
        m_pAnim = nullptr;
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

void CTaskSimpleGrab::ReleaseVictim()
{
    // R* pattern for custom animation blocks: just detach callback, NO blend delta
    if (m_pAnim)
    {
        m_pAnim->SetFinishCallback(NoOpAnimCallback, nullptr);
        m_pAnim = nullptr;
    }

    // Notify victim task to release
    if (m_pVictimTask)
    {
        m_pVictimTask->OnReleased();
        m_pVictimTask = nullptr;
    }

    // Restore collision
    if (m_pGrabber)
    {
        m_pGrabber->m_pEntityIgnoredCollision = nullptr;
    }
    if (m_pVictim)
    {
        m_pVictim->m_pEntityIgnoredCollision = nullptr;
    }

    m_pVictim = nullptr;
    m_state = eGrabState::FINISHED;
}

void CTaskSimpleGrab::StartHoldingImmediate(CPed* grabber, CPed* victim)
{
    if (!grabber || !victim)
    {
        return;
    }

    // Store pointers
    m_pGrabber = grabber;
    m_pVictim = victim;
    m_fGrabDistance = 0.0f;

    // ========== 1. SET COLLISION IGNORE (both ways) ==========
    grabber->m_pEntityIgnoredCollision = victim;
    victim->m_pEntityIgnoredCollision = grabber;

    // ========== 2. CREATE LINE-UP UTILITY (already at end position) ==========
    // For immediate hold, start and end are the same (no interpolation needed)
    CreateLineUpUtility(0.0f);  // Distance 0 means already at holding position
    if (m_pLineUpUtility)
    {
        m_pLineUpUtility->LockPosition();  // Lock to end position immediately
    }

    // ========== 3. START IDLE ANIMATIONS ==========
    StartIdleAnimations(grabber);

    // ========== 4. CREATE VICTIM TASK ==========
    // Check if victim has valid intelligence (some ped types may not)
    if (!victim->m_pIntelligence)
    {
        // Can't assign task to victim without intelligence
        return;
    }
    
    m_pVictimTask = new CTaskSimpleGrabbed(grabber, this);
    m_pVictimTask->SetLineUpUtility(m_pLineUpUtility);  // Use line-up utility for positioning
    m_pVictimTask->SetAnimationSkip(1.0f);  // Skip to end = idle
    m_pVictimTask->SetStartWithIdle(true);  // Start with idle animation directly

    CTaskManager* taskMgr = &victim->m_pIntelligence->m_TaskMgr;
    taskMgr->SetTask((CTask*)m_pVictimTask, TASK_PRIMARY_PHYSICAL_RESPONSE, false);
}

void CTaskSimpleGrab::StartIdleAnimations(CPed* grabber)
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

    CAnimBlendHierarchy* idleHier = CAnimManager::GetAnimation(ANIM_GRAB_IDLE, animBlock);
    if (idleHier)
    {
        // Idle animation flags:
        // - ANIMATION_IS_LOOPED (0x02) - loops continuously
        // - ANIMATION_IS_BLEND_AUTO_REMOVE (0x04) - auto-delete when blended out
        // Note: Idle is a full-body animation, not partial
        const int animFlags = 0x02 | 0x04;  // ANIMATION_IS_LOOPED | ANIMATION_IS_BLEND_AUTO_REMOVE
        
        m_pAnim = CAnimManager::BlendAnimation(grabber->m_pRwClump, idleHier, animFlags, 8.0f);
        
        if (m_pAnim)
        {
            // Make animation reference its own block (R* pattern)
            m_pAnim->ReferenceAnimBlock();
            // No finish callback for looped animation
        }
    }
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
    if (task->m_pVictim && task->m_pVictimTask && task->m_pGrabber)
    {
        task->m_state = eGrabState::HOLDING;
        task->StartIdleAnimations(task->m_pGrabber);
    }
    else
    {
        task->FinishGrab();
    }
}

// ============================================================================
// Action Methods
// ============================================================================

bool CTaskSimpleGrab::IsAttackPressed() const
{
    return PadFix::MeleeAttackJustDown(CPad::GetPad(0), false) != 0;
}

void CTaskSimpleGrab::StartAction(eGrabAction action)
{
    if (!m_pGrabber || !m_pGrabber->m_pRwClump)
    {
        return;
    }

    // Get animation block
    CAnimBlock* animBlock = CAnimManager::GetAnimationBlock(ANIM_BLOCK_NAME);
    if (!animBlock)
    {
        return;
    }

    // Determine which animation to play based on action
    const char* grabberAnimName = nullptr;
    const char* victimAnimName = nullptr;
    
    switch (action)
    {
    case eGrabAction::JAB:
        grabberAnimName = ANIM_GRAB_JAB;
        victimAnimName = ANIM_GRABBED_JAB;
        break;
    // TODO: Add THROW, UPPERCUT cases here
    default:
        return;
    }

    // Get the grabber's animation
    CAnimBlendHierarchy* grabberHier = CAnimManager::GetAnimation(grabberAnimName, animBlock);
    if (!grabberHier)
    {
        return;
    }

    // Stop current idle animation - just detach callback, NO blend delta
    if (m_pAnim)
    {
        m_pAnim->SetFinishCallback(NoOpAnimCallback, nullptr);
    }

    // Start grabber's action animation (will replace the idle)
    m_pAnim = CAnimManager::BlendAnimation(m_pGrabber->m_pRwClump, grabberHier, ANIMATION_PARTIAL, 8.0f);
    if (m_pAnim)
    {
        // Make animation reference its own block (R* pattern)
        m_pAnim->ReferenceAnimBlock();
        m_pAnim->SetFinishCallback(OnActionAnimFinish, this);
    }

    // Tell victim to play their reaction animation
    if (m_pVictimTask && victimAnimName)
    {
        m_pVictimTask->PlayReactionAnimation(victimAnimName);
    }

    // Update state
    m_currentAction = action;
    m_state = eGrabState::ACTION;
}

void CTaskSimpleGrab::OnActionFinished()
{
    // Return to holding state and restart idle animations
    m_currentAction = eGrabAction::NONE;
    m_state = eGrabState::HOLDING;
    
    if (m_pGrabber)
    {
        StartIdleAnimations(m_pGrabber);
    }
}

void CTaskSimpleGrab::OnActionAnimFinish(CAnimBlendAssociation* anim, void* data)
{
    CTaskSimpleGrab* task = static_cast<CTaskSimpleGrab*>(data);
    if (!task)
    {
        return;
    }

    // CRITICAL: Clear pointer FIRST
    task->m_pAnim = nullptr;

    // Transition back to holding
    task->OnActionFinished();
}
