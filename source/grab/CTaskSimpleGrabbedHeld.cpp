#include "CTaskSimpleGrabbedHeld.h"
#include "GrabAnimations.h"

#include <plugin.h>
#include <CAnimManager.h>
#include <numbers>
#include <cmath>

CTaskSimpleGrabbedHeld::CTaskSimpleGrabbedHeld(GrabContextPtr context)
    : m_pContext(std::move(context))
{
    if (m_pContext) {
        m_pContext->AcquireVictimCollisionDisable();
        m_bCollisionDisabled = true;
    }
}

CTaskSimpleGrabbedHeld::CTaskSimpleGrabbedHeld(const CTaskSimpleGrabbedHeld& other)
    : m_pContext(nullptr)
    , m_bFinished(true)
{
}

CTaskSimpleGrabbedHeld::~CTaskSimpleGrabbedHeld()
{
    Cleanup();
}

bool CTaskSimpleGrabbedHeld::MakeAbortable(CPed* ped, eAbortPriority priority, CEvent* event)
{
    const auto phase = m_pContext ? m_pContext->GetPhase() : CGrabContext::eGrabPhase::FINISHED;

    if (m_pContext && phase == CGrabContext::eGrabPhase::ACTION) {
        if (m_pContext->IsEscapePendingOrInProgress()) {
            GrabAnimations::AbortAnimation(ped, m_pAnim, ABORT_PRIORITY_URGENT);
        } else {
            GrabAnimations::ReleaseAnimation(m_pAnim);
        }

        if (m_bCollisionDisabled && m_pContext) {
            m_pContext->ReleaseVictimCollisionDisable();
            m_bCollisionDisabled = false;
        }

        GrabAnimations::UnloadAnimations(m_bAnimsReferenced);
        m_bFinished = true;
        return true;
    }

    if (priority != ABORT_PRIORITY_IMMEDIATE
        && phase != CGrabContext::eGrabPhase::RELEASING
        && phase != CGrabContext::eGrabPhase::FINISHED) {
        return false;
    }

    GrabAnimations::AbortAnimation(ped, m_pAnim, priority);
    if (m_bCollisionDisabled && m_pContext) {
        m_pContext->ReleaseVictimCollisionDisable();
        m_bCollisionDisabled = false;
    }
    GrabAnimations::UnloadAnimations(m_bAnimsReferenced);

    m_bFinished = true;
    return true;
}

bool CTaskSimpleGrabbedHeld::ProcessPed(CPed* ped)
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

    // Phase transitions (release, action) are handled by the complex task's
    // ControlSubTask before ProcessPed runs. This task just plays the idle anim.

    // Load animations if needed
    if (!m_bAnimsReferenced && !GrabAnimations::LoadAnimations(m_bAnimsReferenced)) {
        return false;
    }

    // Start idle animation if not playing
    if (!m_pAnim) {
        StartIdleAnimation(ped);
    }

    return false;
}

bool CTaskSimpleGrabbedHeld::SetPedPosition(CPed* ped)
{
    if (!ped || !m_pContext) {
        return false;
    }

    // Always control position during held phase
    PositionVictim(ped);
    return true;
}

void CTaskSimpleGrabbedHeld::StartIdleAnimation(CPed* ped)
{
    if (!ped || !ped->m_pRwClump) {
        return;
    }

    CAnimBlendHierarchy* hier = GrabAnimations::GetAnimation(GrabAnimations::ANIM_GRABBED_IDLE);
    if (!hier) {
        return;
    }

    // BlendAnimation reuses an existing association for this hierarchy when one
    // is already present, otherwise it creates a new association.
    m_pAnim = CAnimManager::BlendAnimation(
        ped->m_pRwClump,
        hier,
        ANIMATION_LOOPED | ANIMATION_FREEZE_TRANSLATION,
        8.0f
    );

    if (m_pAnim) {
        m_pAnim->ReferenceAnimBlock();
    }
}

void CTaskSimpleGrabbedHeld::Cleanup()
{
    if (m_bCollisionDisabled && m_pContext) {
        m_pContext->ReleaseVictimCollisionDisable();
        m_bCollisionDisabled = false;
    }

    GrabAnimations::BlendOutAnimation(m_pAnim);
    GrabAnimations::UnloadAnimations(m_bAnimsReferenced);
}

void CTaskSimpleGrabbedHeld::PositionVictim(CPed* ped) const
{
    if (!ped || !m_pContext) {
        return;
    }

    CPed* grabber = m_pContext->GetGrabber();
    if (!grabber) {
        return;
    }

    CVector grabberPos = grabber->GetPosition();
    float heading = grabber->m_fHeadingCurrent;

    float sinH = std::sin(heading);
    float cosH = std::cos(heading);

    CVector victimPos;
    victimPos.x = grabberPos.x + (-sinH * GrabAnimations::FINAL_OFFSET_FORWARD);
    victimPos.y = grabberPos.y + (cosH * GrabAnimations::FINAL_OFFSET_FORWARD);
    victimPos.z = grabberPos.z + GrabAnimations::OFFSET_Z;

    ped->SetPosn(victimPos);

    // Face opposite direction to grabber
    constexpr float pi = std::numbers::pi_v<float>;
    constexpr float twoPi = 2.0f * pi;

    float victimHeading = heading + pi;
    while (victimHeading > pi) victimHeading -= twoPi;
    while (victimHeading < -pi) victimHeading += twoPi;

    ped->m_fHeadingCurrent = victimHeading;
    ped->m_fHeadingGoal = victimHeading;
}
