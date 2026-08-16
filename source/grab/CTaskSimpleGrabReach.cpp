#include "CTaskSimpleGrabReach.h"
#include "GrabAnimations.h"

#include <plugin.h>
#include <CAnimManager.h>

using namespace plugin;

CTaskSimpleGrabReach::CTaskSimpleGrabReach(GrabContextPtr context)
    : m_pContext(std::move(context))
{
}

CTaskSimpleGrabReach::CTaskSimpleGrabReach(const CTaskSimpleGrabReach& other)
    : m_pContext(nullptr)
    , m_bFinished(true)
    , m_bAnimFinished(true)
    , m_bStarted(false)
{
}

CTaskSimpleGrabReach::~CTaskSimpleGrabReach()
{
    Cleanup();
}

bool CTaskSimpleGrabReach::MakeAbortable(CPed* ped, eAbortPriority priority, CEvent* event)
{
    const auto phase = m_pContext ? m_pContext->GetPhase() : CGrabContext::eGrabPhase::FINISHED;

    // Reject outside interruptions mid-reach, but release/finish must be able
    // to blend back to a base anim because TaskManager ignores this return.
    if (priority != ABORT_PRIORITY_IMMEDIATE
        && !m_bFinished
        && phase != CGrabContext::eGrabPhase::RELEASING
        && phase != CGrabContext::eGrabPhase::FINISHED) {
        return false;
    }

    GrabAnimations::AbortAnimation(ped, m_pAnim, priority);
    GrabAnimations::UnloadAnimations(m_bAnimsReferenced);
    m_bFinished = true;
    
    return true;
}

bool CTaskSimpleGrabReach::ProcessPed(CPed* ped)
{
    if (m_bFinished) {
        return true;
    }

    // Validate context
    if (!m_pContext || !m_pContext->IsValid()) {
        Cleanup();
        m_bFinished = true;
        return true;
    }

    const auto phase = m_pContext->GetPhase();
    if (phase != CGrabContext::eGrabPhase::REACHING) {
        m_bFinished = true;
        return true;
    }

    // Check for timeout
    if (m_pContext->IsTimedOut()) {
        Cleanup();
        m_pContext->Abort();
        m_bFinished = true;
        return true;
    }

    if (m_bAnimFinished) {
        ped->m_fHeadingGoal = ped->m_fHeadingCurrent;
        m_bFinished = true;
        return true;
    }

    // Load animations if needed
    if (!m_bAnimsReferenced && !GrabAnimations::LoadAnimations(m_bAnimsReferenced)) {
        return false;  // Still loading
    }

    // Start animation on first process
    if (!m_bStarted) {
        StartAnimation(ped);
        m_bStarted = true;
    }

    // Lock ped rotation during reach
    ped->m_fHeadingGoal = ped->m_fHeadingCurrent;

    return false;
}

void CTaskSimpleGrabReach::StartAnimation(CPed* ped)
{
    if (!ped || !ped->m_pRwClump || !m_pContext) {
        m_bFinished = true;
        return;
    }

    CAnimBlendHierarchy* hier = GrabAnimations::GetAnimation(GrabAnimations::ANIM_GRAB);
    if (!hier) {
        m_bFinished = true;
        return;
    }

    float skipAmount = m_pContext->GetAnimationSkip();
    m_pAnim = CAnimManager::BlendAnimation(
        ped->m_pRwClump, 
        hier, 
        ANIMATION_FREEZE_LAST_FRAME,
        GrabAnimations::START_BLEND_DELTA
    );

    if (m_pAnim) {
        m_pAnim->ReferenceAnimBlock();
        
        // Skip forward in animation based on distance
        if (skipAmount > 0.0f && m_pAnim->m_pHierarchy) {
            float skipTime = skipAmount * m_pAnim->m_pHierarchy->m_fTotalTime;
            m_pAnim->SetCurrentTime(skipTime);
            m_pAnim->UpdateBlend(0.0f);
        }
        
        m_pAnim->SetFinishCallback(AnimFinishedCB, this);
    } else {
        m_bFinished = true;
    }
}

void CTaskSimpleGrabReach::Cleanup()
{
    GrabAnimations::CleanupAnimation(m_pAnim);
    GrabAnimations::UnloadAnimations(m_bAnimsReferenced);
}

void CTaskSimpleGrabReach::AnimFinishedCB(CAnimBlendAssociation*, void* data)
{
    auto* task = static_cast<CTaskSimpleGrabReach*>(data);
    if (!task) {
        return;
    }

    task->m_pAnim = nullptr;
    task->m_bAnimFinished = true;

    // Signal to context that grabber reach is complete
    if (task->m_pContext) {
        task->m_pContext->OnGrabberReachComplete();
    }
}
