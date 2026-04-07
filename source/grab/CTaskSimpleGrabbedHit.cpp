#include "CTaskSimpleGrabbedHit.h"
#include "CPostGrabReaction.h"
#include "GrabAnimations.h"

#include <plugin.h>
#include <CAnimManager.h>
#include <CWeaponInfo.h>
#include <numbers>
#include <cmath>

static const char* GetGrabberAnimationName(CGrabContext::eGrabAction action) {
    switch (action) {
    case CGrabContext::eGrabAction::JAB:
        return GrabAnimations::ANIM_GRAB_JAB;
    case CGrabContext::eGrabAction::THROW:
        return GrabAnimations::ANIM_GRAB_THROW;
    case CGrabContext::eGrabAction::UPPERCUT:
        return GrabAnimations::ANIM_GRAB_UPPERCUT;
    default:
        return nullptr;
    }
}

CTaskSimpleGrabbedHit::CTaskSimpleGrabbedHit(GrabContextPtr context, CGrabContext::eGrabAction hitType)
    : m_pContext(std::move(context))
    , m_hitType(hitType)
{
    if (m_pContext) {
        m_pContext->AcquireVictimCollisionDisable();
        m_bCollisionDisabled = true;
    }
}

CTaskSimpleGrabbedHit::CTaskSimpleGrabbedHit(const CTaskSimpleGrabbedHit& other)
    : m_pContext(other.m_pContext)
    , m_hitType(other.m_hitType)
    , m_bAnimsReferenced(false)
    , m_bFinished(other.m_bFinished)
    , m_bAnimFinished(other.m_bAnimFinished)
    , m_bStarted(other.m_bStarted)
    , m_bCollisionDisabled(false)
{
    if (m_pContext) {
        m_pContext->AcquireVictimCollisionDisable();
        m_bCollisionDisabled = true;
    }
}

CTaskSimpleGrabbedHit::~CTaskSimpleGrabbedHit()
{
    Cleanup();
}

bool CTaskSimpleGrabbedHit::MakeAbortable(CPed* ped, eAbortPriority priority, CEvent* event)
{
    // Hit reactions are only abortable with immediate priority
    if (priority != ABORT_PRIORITY_IMMEDIATE && !m_bFinished) {
        return false;
    }

    Cleanup();
    m_bFinished = true;
    
    if (m_pContext) {
        m_pContext->OnVictimActionComplete();
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
        Cleanup();
        m_bFinished = true;
        return true;
    }

    if (m_pContext->GetPhase() != CGrabContext::eGrabPhase::ACTION) {
        m_bFinished = true;
        return true;
    }

    if (m_bAnimFinished) {
        m_bFinished = true;
        return true;
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

    // Check if the grabber signalled that the hit connected
    CheckDamageTrigger(ped);

    if (m_bDamageApplied && m_pContext->IsGrabberActionComplete()) {
        FinishEarly();
        return true;
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
        4.0f
    );

    if (m_pAnim) {
        m_pAnim->ReferenceAnimBlock();
        m_pAnim->m_fSpeed = GrabAnimations::GetSynchronizedSpeed(animName, GetGrabberAnimationName(m_hitType));

        if (const auto grabberAnim = GrabAnimations::FindAssociation(m_pContext ? m_pContext->GetGrabber() : nullptr, GetGrabberAnimationName(m_hitType))) {
            m_pAnim->SyncAnimation(grabberAnim);
        }

        m_pAnim->SetFinishCallback(AnimFinishedCB, this);
    } else {
        m_bFinished = true;
    }
}

void CTaskSimpleGrabbedHit::CheckDamageTrigger(CPed* ped)
{
    if (m_bDamageApplied || !m_pContext || !ped) {
        return;
    }

    // Poll the context for the hit signal from the grabber's action task
    if (!m_pContext->ConsumeHitConnected()) {
        return;
    }

    m_bDamageApplied = true;

    CPed* grabber = m_pContext->GetGrabber();
    if (!grabber) {
        return;
    }

    CWeaponInfo* weapInfo = CWeaponInfo::GetWeaponInfo(WEAPONTYPE_UNARMED);
    float damage = weapInfo ? static_cast<float>(weapInfo->m_nDamage) : 5.0f;

    CPostGrabReaction::ApplyDamageWithoutReaction(
        ped,
        grabber,
        damage,
        PED_PIECE_ASS,
        true
    );
}

void CTaskSimpleGrabbedHit::FinishEarly()
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

void CTaskSimpleGrabbedHit::Cleanup()
{
    if (m_bCollisionDisabled && m_pContext) {
        m_pContext->ReleaseVictimCollisionDisable();
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
    task->m_bAnimFinished = true;

    // Signal victim side completion to context
    if (task->m_pContext) {
        task->m_pContext->OnVictimActionComplete();
    }
}
