#include "CTaskSimpleGrabbedEscape.h"
#include "CGrabSystem.h"
#include "GrabAnimations.h"

#include <plugin.h>
#include <CAnimManager.h>
#include <CAnimBlendHierarchy.h>
#include <numbers>
#include <cmath>
#include <utility>

CTaskSimpleGrabbedEscape::CTaskSimpleGrabbedEscape(GrabContextPtr context)
    : m_pContext(std::move(context))
{
    if (m_pContext) {
        m_pContext->AcquireVictimCollisionDisable();
        m_bCollisionDisabled = true;
    }
}

CTaskSimpleGrabbedEscape::CTaskSimpleGrabbedEscape(const CTaskSimpleGrabbedEscape& other)
    : m_pContext(nullptr)
    , m_bAnimsReferenced(false)
    , m_bFinished(true)
    , m_bAnimFinished(true)
    , m_bStarted(false)
    , m_bCollisionDisabled(false)
    , m_bImpactTriggered(false)
{
}

CTaskSimpleGrabbedEscape::~CTaskSimpleGrabbedEscape()
{
    Cleanup();
}

bool CTaskSimpleGrabbedEscape::MakeAbortable(CPed* ped, eAbortPriority priority, CEvent* event)
{
    (void)ped;
    (void)event;

    const auto phase = m_pContext ? m_pContext->GetPhase() : CGrabContext::eGrabPhase::FINISHED;

    if (priority != ABORT_PRIORITY_IMMEDIATE
        && !m_bFinished
        && phase != CGrabContext::eGrabPhase::RELEASING
        && phase != CGrabContext::eGrabPhase::FINISHED) {
        return false;
    }

    Finish();
    return true;
}

bool CTaskSimpleGrabbedEscape::ProcessPed(CPed* ped)
{
    if (m_bFinished) {
        return true;
    }

    if (!m_pContext || !m_pContext->ArePedsValid()) {
        Finish();
        return true;
    }

    if (m_pContext->GetPhase() != CGrabContext::eGrabPhase::ACTION
        || m_pContext->GetCurrentAction() != CGrabContext::eGrabAction::ESCAPE) {
        Finish();
        return true;
    }

    if (m_bAnimFinished) {
        if (!m_bImpactTriggered) {
            TriggerEscapeImpact(ped);
        }
        Finish();
        return true;
    }

    if (!m_bAnimsReferenced && !GrabAnimations::LoadAnimations(m_bAnimsReferenced)) {
        return false;
    }

    if (!m_bStarted) {
        StartAnimation(ped);
        m_bStarted = true;
    }

    CheckImpactTrigger(ped);

    if (m_bImpactTriggered) {
        Finish();
        return true;
    }

    return false;
}

bool CTaskSimpleGrabbedEscape::SetPedPosition(CPed* ped)
{
    (void)ped;
    return false;
}

void CTaskSimpleGrabbedEscape::StartAnimation(CPed* ped)
{
    if (!ped || !ped->m_pRwClump) {
        TriggerEscapeImpact(ped);
        m_bFinished = true;
        return;
    }

    CAnimBlendHierarchy* hier = GrabAnimations::GetAnimation(GrabAnimations::ANIM_GRABBED_BREAK);
    if (!hier) {
        TriggerEscapeImpact(ped);
        m_bFinished = true;
        return;
    }

    m_pAnim = CAnimManager::BlendAnimation(
        ped->m_pRwClump,
        hier,
        ANIMATION_IS_PARTIAL | ANIMATION_IS_BLEND_AUTO_REMOVE | ANIMATION_IS_FINISH_AUTO_REMOVE,
        4.0f
    );

    if (m_pAnim) {
        m_pAnim->ReferenceAnimBlock();
        m_pAnim->m_fSpeed = GrabAnimations::ESCAPE_BREAK_ANIM_SPEED;

        if (const auto grabberAnim = GrabAnimations::FindAssociation(m_pContext ? m_pContext->GetGrabber() : nullptr, GrabAnimations::ANIM_GRAB_BREAK)) {
            m_pAnim->SyncAnimation(grabberAnim);
        }

        m_pAnim->SetFinishCallback(AnimFinishedCB, this);
    } else {
        TriggerEscapeImpact(ped);
        m_bFinished = true;
    }
}

void CTaskSimpleGrabbedEscape::CheckImpactTrigger(CPed* ped)
{
    if (m_bImpactTriggered || !m_pAnim) {
        return;
    }

    const float totalTime = m_pAnim->m_pHierarchy->m_fTotalTime;
    const float impactTime = totalTime * 0.6f;

    if (impactTime < m_pAnim->m_fCurrentTime
        && (m_pAnim->m_fCurrentTime - m_pAnim->fTimeStep) <= impactTime) {
        TriggerEscapeImpact(ped);
    }
}

void CTaskSimpleGrabbedEscape::TriggerEscapeImpact(CPed* ped)
{
    if (m_bImpactTriggered || !m_pContext || !ped) {
        return;
    }

    CPed* grabber = m_pContext->GetGrabber();
    if (!grabber || grabber == ped || grabber->m_fHealth <= 0.0f) {
        return;
    }

    if (m_bCollisionDisabled) {
        m_pContext->ReleaseVictimCollisionDisable();
        m_bCollisionDisabled = false;
    }

    m_bImpactTriggered = true;
    m_pContext->OnEscapeImpact();
    CGrabSystem::RegisterEscapedVictim(ped);
    m_pContext->Release(CGrabContext::eGrabEndReason::VICTIM_ESCAPED);
}

void CTaskSimpleGrabbedEscape::Finish()
{
    if (m_bFinished) {
        return;
    }

    GrabAnimations::BlendOutAnimation(m_pAnim, -8.0f);
    GrabAnimations::UnloadAnimations(m_bAnimsReferenced);

    if (m_bCollisionDisabled && m_pContext) {
        m_pContext->ReleaseVictimCollisionDisable();
        m_bCollisionDisabled = false;
    }

    m_bAnimFinished = true;
    m_bFinished = true;

    if (m_pContext) {
        m_pContext->OnVictimActionComplete();
    }
}

void CTaskSimpleGrabbedEscape::Cleanup()
{
    if (m_bCollisionDisabled && m_pContext) {
        m_pContext->ReleaseVictimCollisionDisable();
        m_bCollisionDisabled = false;
    }

    GrabAnimations::CleanupAnimation(m_pAnim);
    GrabAnimations::UnloadAnimations(m_bAnimsReferenced);
}

void CTaskSimpleGrabbedEscape::PositionVictim(CPed* ped) const
{
    if (!ped || !m_pContext) {
        return;
    }

    CPed* grabber = m_pContext->GetGrabber();
    if (!grabber) {
        return;
    }

    CVector grabberPos = grabber->GetPosition();
    float heading = grabber->m_fCurrentRotation;

    float sinH = std::sin(heading);
    float cosH = std::cos(heading);

    CVector victimPos;
    victimPos.x = grabberPos.x + (-sinH * GrabAnimations::FINAL_OFFSET_FORWARD);
    victimPos.y = grabberPos.y + (cosH * GrabAnimations::FINAL_OFFSET_FORWARD);
    victimPos.z = grabberPos.z + GrabAnimations::OFFSET_Z;

    ped->SetPosn(victimPos);

    constexpr float pi = std::numbers::pi_v<float>;
    constexpr float twoPi = 2.0f * pi;

    float victimHeading = heading + pi;
    while (victimHeading > pi) victimHeading -= twoPi;
    while (victimHeading < -pi) victimHeading += twoPi;

    ped->m_fCurrentRotation = victimHeading;
    ped->m_fAimingRotation = victimHeading;
}

void CTaskSimpleGrabbedEscape::AnimFinishedCB(CAnimBlendAssociation*, void* data)
{
    auto* task = static_cast<CTaskSimpleGrabbedEscape*>(data);
    if (!task) {
        return;
    }

    task->m_pAnim = nullptr;
    task->m_bAnimFinished = true;
}
