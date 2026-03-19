#include "CTaskSimpleGrabbedReach.h"
#include "GrabAnimations.h"

#include <plugin.h>
#include <CAnimManager.h>
#include <numbers>
#include <cmath>
#include <algorithm>

CTaskSimpleGrabbedReach::CTaskSimpleGrabbedReach(GrabContextPtr context)
    : m_pContext(std::move(context))
{
    if (m_pContext) {
        m_pContext->AcquireVictimCollisionDisable();
        m_bCollisionDisabled = true;
    }
}

CTaskSimpleGrabbedReach::CTaskSimpleGrabbedReach(const CTaskSimpleGrabbedReach& other)
    : m_pContext(other.m_pContext)
    , m_bFinished(other.m_bFinished)
    , m_bAnimFinished(other.m_bAnimFinished)
    , m_bStarted(other.m_bStarted)
{
    if (m_pContext) {
        m_pContext->AcquireVictimCollisionDisable();
        m_bCollisionDisabled = true;
    }
}

CTaskSimpleGrabbedReach::~CTaskSimpleGrabbedReach()
{
    Cleanup();
}

bool CTaskSimpleGrabbedReach::MakeAbortable(CPed* ped, eAbortPriority priority, CEvent* event)
{
    // During reach, only immediate priority can abort
    if (priority != ABORT_PRIORITY_IMMEDIATE && !m_bFinished) {
        return false;
    }

    Cleanup();
    m_bFinished = true;
    
    return true;
}

bool CTaskSimpleGrabbedReach::ProcessPed(CPed* ped)
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

    // Check for timeout
    if (m_pContext->IsTimedOut()) {
        Cleanup();
        m_pContext->Abort();
        m_bFinished = true;
        return true;
    }

    if (m_pContext->GetPhase() != CGrabContext::eGrabPhase::REACHING) {
        m_bFinished = true;
        return true;
    }

    if (m_bAnimFinished) {
        return false;
    }

    // Load animations if needed
    if (!m_bAnimsReferenced && !GrabAnimations::LoadAnimations(m_bAnimsReferenced)) {
        return false;
    }

    // Start animation on first process
    if (!m_bStarted) {
        StartAnimation(ped);
        m_bStarted = true;
    }
    return false;
}

bool CTaskSimpleGrabbedReach::SetPedPosition(CPed* ped)
{
    if (!ped || !m_pContext) {
        return false;
    }

    // Always control position during reach phase
    PositionVictim(ped);
    return true;
}

void CTaskSimpleGrabbedReach::StartAnimation(CPed* ped)
{
    if (!ped || !ped->m_pRwClump) {
        m_bFinished = true;
        return;
    }

    CAnimBlendHierarchy* hier = GrabAnimations::GetAnimation(GrabAnimations::ANIM_GRABBED);
    if (!hier) {
        m_bFinished = true;
        return;
    }

    float skipAmount = m_pContext ? m_pContext->GetAnimationSkip() : 0.0f;

    m_pAnim = CAnimManager::BlendAnimation(
        ped->m_pRwClump, 
        hier, 
        ANIMATION_IGNORE_ROOT_TRANSLATION, 
        8.0f
    );

    if (m_pAnim) {
        m_pAnim->ReferenceAnimBlock();
        
        // Skip forward based on grabber distance
        if (skipAmount > 0.0f && m_pAnim->m_pHierarchy) {
            float skipTime = skipAmount * m_pAnim->m_pHierarchy->m_fTotalTime;
            m_pAnim->SetCurrentTime(skipTime);
        }
        
        m_pAnim->SetDeleteCallback(GrabAnimations::DefaultAnimCB, nullptr);
        m_pAnim->SetFinishCallback(AnimFinishedCB, this);
    } else {
        m_bFinished = true;
    }
}

void CTaskSimpleGrabbedReach::Cleanup()
{
    if (m_bCollisionDisabled && m_pContext) {
        m_pContext->ReleaseVictimCollisionDisable();
        m_bCollisionDisabled = false;
    }

    GrabAnimations::CleanupAnimation(m_pAnim);
    GrabAnimations::UnloadAnimations(m_bAnimsReferenced);
}

void CTaskSimpleGrabbedReach::PositionVictim(CPed* ped) const
{
    if (!ped || !m_pContext) {
        return;
    }

    CPed* grabber = m_pContext->GetGrabber();
    if (!grabber) {
        return;
    }

    // Calculate offset based on animation progress
    float offsetForward = GrabAnimations::FINAL_OFFSET_FORWARD;
    
    if (m_pAnim && m_pAnim->m_pHierarchy) {
        float totalTime = m_pAnim->m_pHierarchy->m_fTotalTime;
        float progress = (totalTime > 0.0f) ? (m_pAnim->m_fCurrentTime / totalTime) : 1.0f;
        progress = std::clamp(progress + 0.4f, 0.0f, 1.0f);
        offsetForward = GrabAnimations::START_OFFSET_FORWARD + 
                        (GrabAnimations::FINAL_OFFSET_FORWARD - GrabAnimations::START_OFFSET_FORWARD) * progress;
    }

    CVector grabberPos = grabber->GetPosition();
    float heading = grabber->m_fCurrentRotation;
    
    float sinH = std::sin(heading);
    float cosH = std::cos(heading);
    
    CVector victimPos;
    victimPos.x = grabberPos.x + (-sinH * offsetForward);
    victimPos.y = grabberPos.y + (cosH * offsetForward);
    victimPos.z = grabberPos.z + GrabAnimations::OFFSET_Z;
    
    ped->SetPosn(victimPos);
    
    // Face opposite direction to grabber
    constexpr float pi = std::numbers::pi_v<float>;
    constexpr float twoPi = 2.0f * pi;
    
    float victimHeading = heading + pi;
    while (victimHeading > pi) victimHeading -= twoPi;
    while (victimHeading < -pi) victimHeading += twoPi;
    
    ped->m_fCurrentRotation = victimHeading;
    ped->m_fAimingRotation = victimHeading;
}

void CTaskSimpleGrabbedReach::AnimFinishedCB(CAnimBlendAssociation*, void* data)
{
    auto* task = static_cast<CTaskSimpleGrabbedReach*>(data);
    if (!task) {
        return;
    }

    task->m_pAnim = nullptr;
    task->m_bAnimFinished = true;

    // Signal completion to context
    if (task->m_pContext) {
        task->m_pContext->OnVictimReachComplete();
    }
}
