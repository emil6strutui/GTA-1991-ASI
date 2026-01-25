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
    m_state = eGrabbedState::INIT;
    m_pGrabber = pGrabber;
    m_pVictimPed = nullptr;  // Will be set in first ProcessPed call
    m_pGrabberTask = pGrabberTask;
    m_pAnim = nullptr;
    m_bAnimsReferenced = false;
    m_bCollisionDisabled = false;
    
    // Animation skip (synced with grabber)
    m_fAnimationSkip = 0.0f;
    
    // Default: play grab animation, not idle
    m_bStartWithIdle = false;
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

   // R* pattern: Set BLEND_AUTO_REMOVE in destructor
   if (m_pAnim) {
        m_pAnim->SetDeleteCallback(NoOpAnimCallback, nullptr);
        m_pAnim->m_nFlags |= ANIMATION_FREEZE_LAST_FRAME;  // 0x04
        if (m_pAnim->m_fBlendAmount > 0.0f && m_pAnim->m_fBlendDelta >= 0.0f) {
            m_pAnim->m_fBlendDelta = -4.0f;
        }
        m_pAnim = nullptr;
    }

    // Notify grabber that we're gone (if they didn't release us)
    if (m_pGrabberTask && m_state != eGrabbedState::FINISHED)
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

    // R* pattern for custom animation blocks: just detach callback, NO blend delta
    if (m_pAnim)
    {
        m_pAnim->SetDeleteCallback(NoOpAnimCallback, nullptr);
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
    if (m_state == eGrabbedState::FINISHED)
    {
        return true;
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

    if (!m_pGrabber || m_pGrabber->m_fHealth <= 0.0f)
    {
        OnReleased();
        return true;
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

    return false; // Continue task
}

bool CTaskSimpleGrabbed::SetPedPosition(CPed* ped)
{
    // Only control position during INIT (grabbing) and GRABBED (holding) states
    if (!ped || !m_pGrabber)
    {
        return false;
    }
    
    // Calculate offset based on state
    float offsetForward = FINAL_OFFSET_FORWARD;  // Default to final position
    
    if (m_state == eGrabbedState::INIT && m_pAnim && m_pAnim->m_pHierarchy)
    {
        // During grab animation: lerp from start to final position based on anim progress
        float totalTime = m_pAnim->m_pHierarchy->m_fTotalTime;
        float progress = (totalTime > 0.0f) ? (m_pAnim->m_fCurrentTime / totalTime) : 1.0f;
        
        // Clamp progress to [0, 1]
        if (progress < 0.0f) progress = 0.0f;
        progress += 0.4;
        if (progress > 1.0f) progress = 1.0f;
        
        // Lerp: start + (end - start) * progress
        offsetForward = START_OFFSET_FORWARD + (FINAL_OFFSET_FORWARD - START_OFFSET_FORWARD) * progress;
    }
    else if (m_state == eGrabbedState::GRABBED)
    {
        // During idle/holding: fixed position at final offset
        offsetForward = FINAL_OFFSET_FORWARD;
    }
    else
    {
        // Other states (RELEASED, FINISHED): don't control position
        return false;
    }
    
    // Get grabber position and heading
    CVector grabberPos = m_pGrabber->GetPosition();
    float heading = m_pGrabber->m_fCurrentRotation;
    
    // Calculate world position from local offset
    // In GTA SA: +Y is forward, heading 0 = facing +Y
    // Forward direction: (-sin(heading), cos(heading))
    float sinH = sinf(heading);
    float cosH = cosf(heading);
    
    CVector victimPos;
    victimPos.x = grabberPos.x + (-sinH * offsetForward);
    victimPos.y = grabberPos.y + (cosH * offsetForward);
    victimPos.z = grabberPos.z + OFFSET_Z;
    
    ped->SetPosn(victimPos);
    
    // Face opposite to grabber (victim faces the grabber)
    float victimHeading = heading + 3.14159265f;
    // Normalize to [-PI, PI]
    while (victimHeading > 3.14159265f) victimHeading -= 6.28318530f;
    while (victimHeading < -3.14159265f) victimHeading += 6.28318530f;
    
    ped->m_fCurrentRotation = victimHeading;
    ped->m_fAimingRotation = victimHeading;
    
    return true;  // We handled the position - override animation root motion
}

// ============================================================================
// Grabbed-specific
// ============================================================================

void CTaskSimpleGrabbed::OnReleased()
{
    m_pGrabber = nullptr;
    m_pGrabberTask = nullptr;
    
    if (m_bCollisionDisabled && m_pVictimPed) {
        m_pVictimPed->bCollidable = true;
        m_bCollisionDisabled = false;
    }
    
    // >>> R* PATTERN: Blend in idle animation for full-body anims <<<
    if (m_pVictimPed && m_pVictimPed->m_pRwClump) {
        CAnimManager::BlendAnimation(
            m_pVictimPed->m_pRwClump, 
            m_pVictimPed->m_nAnimGroup,  // Ped's default anim group
            ANIM_DEFAULT_IDLE_STANCE,                 // Idle animation
            4.0f                          // Blend speed
        );
    }
    
    // Detach callback (animation will be cleaned up by BlendAnimation above)
    if (m_pAnim) {
        m_pAnim->SetDeleteCallback(NoOpAnimCallback, nullptr);
        // DON'T set BLEND_AUTO_REMOVE here - BlendAnimation handles it
        // DON'T set blend delta - BlendAnimation handles it
        m_pAnim = nullptr;
    }
    
    m_state = eGrabbedState::FINISHED;
}

// SetOffset removed - using CTaskUtilityLineUpPedWithPed for positioning

void CTaskSimpleGrabbed::SetAnimationSkip(float skip)
{
    m_fAnimationSkip = skip;
}

void CTaskSimpleGrabbed::SetStartWithIdle(bool startWithIdle)
{
    m_bStartWithIdle = startWithIdle;
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

    RpAnimBlendClumpSetBlendDeltas(ped->m_pRwClump, ANIMATION_PARTIAL, -8.0f);

    CAnimBlock* animBlock = CAnimManager::GetAnimationBlock(ANIM_BLOCK_NAME);
    if (!animBlock)
    {
        return;
    }

    // Choose animation based on start mode
    const char* animName = m_bStartWithIdle ? ANIM_GRABBED_IDLE : ANIM_GRABBED;
    
    CAnimBlendHierarchy* hier = CAnimManager::GetAnimation(animName, animBlock);
    if (!hier)
    {
        return;
    }

    // Calculate blend delta based on skip amount
    // Higher skip = faster blend (snappier for close grabs)
    float blendDelta = (m_fAnimationSkip > 0.4f) ? 16.0f : 8.0f;

    // Animation flags:
    // - Full-body grabbed animation (not partial)
    // - For idle: add ANIMATION_IS_LOOPED (0x02)

    int animFlags = m_bStartWithIdle ? ANIMATION_LOOPED : ANIMATION_FREEZE_TRANSLATION;

    animFlags |= ANIMATION_STARTED;

    // Blend in with high priority to override any other animations
    m_pAnim = CAnimManager::BlendAnimation(ped->m_pRwClump, hier, animFlags, blendDelta);
    
    if (m_pAnim)
    {
        // Make animation reference its own block (R* pattern)
        // This ensures block stays loaded while animation is playing
        m_pAnim->ReferenceAnimBlock();
        
        // Only skip animation for non-idle (idle starts from beginning and loops)
        if (!m_bStartWithIdle && m_fAnimationSkip > 0.0f && m_pAnim->m_pHierarchy)
        {
            // IMPORTANT: Use SetCurrentTime() to properly update blend node keyframe state
            float skipTime = m_fAnimationSkip * m_pAnim->m_pHierarchy->m_fTotalTime;
            m_pAnim->SetCurrentTime(skipTime);
        }
        
        // Use SetDeleteCallback - fires when anim is deleted for ANY reason
        // This is safer for victim since animation can be interrupted by combat/damage
        m_pAnim->SetDeleteCallback(OnAnimDeleted, this);

        if (animName == ANIM_GRABBED) {
            m_pAnim->SetFinishCallback(OnGrabbedAnimFinish, this);
        }
    }
}

// ============================================================================
// Animation Callbacks
// ============================================================================

void CTaskSimpleGrabbed::OnGrabbedAnimFinish(CAnimBlendAssociation* anim, void* data) {
    CTaskSimpleGrabbed* task = static_cast<CTaskSimpleGrabbed*>(data);
    if (!task)
    {
        return;
    }

    task->m_state = eGrabbedState::GRABBED;
}

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

// ============================================================================
// Reaction Animation (called by grabber when performing actions)
// ============================================================================

void CTaskSimpleGrabbed::PlayReactionAnimation(const char* animName)
{
    if (!m_pVictimPed || !m_pVictimPed->m_pRwClump || !animName)
    {
        return;
    }

    // Get animation block
    CAnimBlock* animBlock = CAnimManager::GetAnimationBlock(ANIM_BLOCK_NAME);
    if (!animBlock)
    {
        return;
    }

    // Get the reaction animation
    CAnimBlendHierarchy* hier = CAnimManager::GetAnimation(animName, animBlock);
    if (!hier)
    {
        return;
    }

    // Stop current animation (idle) - just detach callback, NO blend delta
    if (m_pAnim)
    {
        m_pAnim->SetDeleteCallback(NoOpAnimCallback, nullptr);
    }
    
    m_pAnim = CAnimManager::BlendAnimation(m_pVictimPed->m_pRwClump, hier, ANIMATION_PARTIAL | ANIMATION_UNLOCK_LAST_FRAME, 8.0f);
    if (m_pAnim)
    {
        // Make animation reference its own block (R* pattern)
        m_pAnim->ReferenceAnimBlock();
        
        // Use delete callback - safer for victim anims that may be interrupted
        m_pAnim->SetDeleteCallback(OnReactionAnimFinished, this);
    }
}

void CTaskSimpleGrabbed::OnReactionAnimFinished(CAnimBlendAssociation* anim, void* data)
{
    CTaskSimpleGrabbed* task = static_cast<CTaskSimpleGrabbed*>(data);
    if (!task)
    {
        return;
    }

    anim->m_fBlendDelta = -4.0f;

    // CRITICAL: Clear pointer FIRST
    task->m_pAnim = nullptr;

    // If still grabbed, return to idle animation
    if (task->m_state == eGrabbedState::GRABBED && task->m_pVictimPed)
    {
        // Set to play idle animation (not the initial grab animation)
        task->m_bStartWithIdle = true;
        task->StartGrabbedAnimation(task->m_pVictimPed);
    }
}
