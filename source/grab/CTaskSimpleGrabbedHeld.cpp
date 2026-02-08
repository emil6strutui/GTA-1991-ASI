#include "CTaskSimpleGrabbedHeld.h"
#include "GrabAnimations.h"

#include <plugin.h>
#include <CAnimManager.h>
#include <numbers>
#include <cmath>

CTaskSimpleGrabbedHeld::CTaskSimpleGrabbedHeld(GrabContextPtr context)
    : m_pContext(std::move(context))
{
}

CTaskSimpleGrabbedHeld::CTaskSimpleGrabbedHeld(const CTaskSimpleGrabbedHeld& other)
    : m_pContext(other.m_pContext)
    , m_bFinished(other.m_bFinished)
{
}

CTaskSimpleGrabbedHeld::~CTaskSimpleGrabbedHeld()
{
    GrabAnimations::CleanupAnimation(m_pAnim);
    GrabAnimations::UnloadAnimations(m_bAnimsReferenced);
}

bool CTaskSimpleGrabbedHeld::MakeAbortable(CPed* ped, eAbortPriority priority, CEvent* event)
{
    // Held state is abortable
    Cleanup(ped);
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
        Cleanup(ped);
        m_bFinished = true;
        return true;
    }

    // Phase transitions (release, action) are handled by the complex task's
    // ControlSubTask before ProcessPed runs. This task just plays the idle anim.

    // Maintain collision disable
    if (!m_bCollisionDisabled && ped && ped->bCollidable) {
        ped->bCollidable = false;
        m_bCollisionDisabled = true;
    }

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
    //return false;
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

    m_pAnim = CAnimManager::BlendAnimation(
        ped->m_pRwClump, 
        hier, 
        ANIMATION_IS_LOOPED | ANIMATION_IGNORE_ROOT_TRANSLATION, 
        8.0f
    );

    if (m_pAnim) {
        m_pAnim->ReferenceAnimBlock();
    }
}

void CTaskSimpleGrabbedHeld::Cleanup(CPed* ped)
{
    if (m_bCollisionDisabled && ped) {
        ped->bCollidable = true;
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
    float heading = grabber->m_fCurrentRotation;
    
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
    
    ped->m_fCurrentRotation = victimHeading;
    ped->m_fAimingRotation = victimHeading;
}
