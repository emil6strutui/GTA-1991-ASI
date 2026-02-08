#include "CTaskSimpleGrabbedHit.h"
#include "GrabAnimations.h"

#include <plugin.h>
#include <CAnimManager.h>
#include <numbers>
#include <cmath>

CTaskSimpleGrabbedHit::CTaskSimpleGrabbedHit(GrabContextPtr context, CGrabContext::eGrabAction hitType)
    : m_pContext(std::move(context))
    , m_hitType(hitType)
{
}

CTaskSimpleGrabbedHit::CTaskSimpleGrabbedHit(const CTaskSimpleGrabbedHit& other)
    : m_pContext(other.m_pContext)
    , m_hitType(other.m_hitType)
    , m_bAnimsReferenced(false)
    , m_bFinished(other.m_bFinished)
    , m_bStarted(other.m_bStarted)
    , m_bCollisionDisabled(false)
{
}

CTaskSimpleGrabbedHit::~CTaskSimpleGrabbedHit()
{
    GrabAnimations::CleanupAnimation(m_pAnim);
    GrabAnimations::UnloadAnimations(m_bAnimsReferenced);
}

bool CTaskSimpleGrabbedHit::MakeAbortable(CPed* ped, eAbortPriority priority, CEvent* event)
{
    // Hit reactions are only abortable with immediate priority
    if (priority != ABORT_PRIORITY_IMMEDIATE && !m_bFinished) {
        return false;
    }

    Cleanup(ped);
    m_bFinished = true;
    
    if (m_pContext) {
        m_pContext->OnActionComplete();
    }
    
    return true;
}

bool CTaskSimpleGrabbedHit::ProcessPed(CPed* ped)
{
    if (m_bFinished) {
        return true;
    }

    // Validate context
    if (!m_pContext || !m_pContext->IsValid()) {
        Cleanup(ped);
        m_bFinished = true;
        return true;
    }

    // Maintain collision disable
    if (!m_bCollisionDisabled && ped && ped->bCollidable) {
        ped->bCollidable = false;
        m_bCollisionDisabled = true;
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

bool CTaskSimpleGrabbedHit::SetPedPosition(CPed* ped)
{
    if (!ped || !m_pContext) {
        return false;
    }

    // Always control position during hit
    PositionVictim(ped);
    return true;
}

void CTaskSimpleGrabbedHit::StartAnimation(CPed* ped)
{
    if (!ped || !ped->m_pRwClump) {
        m_bFinished = true;
        return;
    }

    const char* animName = GetAnimationName();
    if (!animName) {
        m_bFinished = true;
        return;
    }

    CAnimBlendHierarchy* hier = GrabAnimations::GetAnimation(animName);
    if (!hier) {
        m_bFinished = true;
        return;
    }

    m_pAnim = CAnimManager::BlendAnimation(
        ped->m_pRwClump, 
        hier, 
        ANIMATION_IS_PARTIAL | ANIMATION_IS_BLEND_AUTO_REMOVE | ANIMATION_IGNORE_ROOT_TRANSLATION,
        8.0f
    );

    if (m_pAnim) {
        m_pAnim->ReferenceAnimBlock();
        m_pAnim->SetFinishCallback(AnimFinishedCB, this);
    } else {
        m_bFinished = true;
    }
}

void CTaskSimpleGrabbedHit::Cleanup(CPed* ped)
{
    if (m_bCollisionDisabled && ped) {
        ped->bCollidable = true;
        m_bCollisionDisabled = false;
    }

    GrabAnimations::CleanupAnimation(m_pAnim);
    GrabAnimations::UnloadAnimations(m_bAnimsReferenced);
}

void CTaskSimpleGrabbedHit::PositionVictim(CPed* ped) const
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
    
    // Face opposite direction
    constexpr float pi = std::numbers::pi_v<float>;
    constexpr float twoPi = 2.0f * pi;
    
    float victimHeading = heading + pi;
    while (victimHeading > pi) victimHeading -= twoPi;
    while (victimHeading < -pi) victimHeading += twoPi;
    
    ped->m_fCurrentRotation = victimHeading;
    ped->m_fAimingRotation = victimHeading;
}

const char* CTaskSimpleGrabbedHit::GetAnimationName() const
{
    switch (m_hitType) {
    case CGrabContext::eGrabAction::JAB:
        return GrabAnimations::ANIM_GRABBED_JAB;
    case CGrabContext::eGrabAction::THROW:
        return GrabAnimations::ANIM_GRABBED_THROW;
    case CGrabContext::eGrabAction::UPPERCUT:
        return GrabAnimations::ANIM_GRABBED_UPPERCUT;
    default:
        return nullptr;
    }
}

void CTaskSimpleGrabbedHit::AnimFinishedCB(CAnimBlendAssociation*, void* data)
{
    auto* task = static_cast<CTaskSimpleGrabbedHit*>(data);
    if (!task) {
        return;
    }

    task->m_pAnim = nullptr;
    task->m_bFinished = true;

    // Signal completion - context will transition both tasks back to holding
    if (task->m_pContext) {
        task->m_pContext->OnActionComplete();
    }
}
