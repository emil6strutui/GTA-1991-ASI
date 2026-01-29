#include "CTaskSimpleGrabHold.h"
#include "GrabAnimations.h"

#include <plugin.h>
#include <CAnimManager.h>

using namespace plugin;

CTaskSimpleGrabHold::CTaskSimpleGrabHold(GrabContextPtr context)
    : m_pContext(std::move(context))
{
}

CTaskSimpleGrabHold::CTaskSimpleGrabHold(const CTaskSimpleGrabHold& other)
    : m_pContext(other.m_pContext)
    , m_bFinished(other.m_bFinished)
{
}

CTaskSimpleGrabHold::~CTaskSimpleGrabHold()
{
    Cleanup();
}

bool CTaskSimpleGrabHold::MakeAbortable(CPed* ped, eAbortPriority priority, CEvent* event)
{
    // Hold state is abortable at any priority
    Cleanup();
    m_bFinished = true;
    return true;
}

bool CTaskSimpleGrabHold::ProcessPed(CPed* ped)
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

    // Check if we need to transition to action
    if (ShouldTransitionToAction()) {
        // Don't cleanup animation here - complex task will handle transition
        m_bFinished = true;
        return true;
    }

    // Check for release
    if (m_pContext->GetPhase() == CGrabContext::eGrabPhase::RELEASING ||
        m_pContext->GetPhase() == CGrabContext::eGrabPhase::FINISHED) {
        Cleanup();
        m_bFinished = true;
        return true;
    }

    // Load animations if needed
    if (!m_bAnimsReferenced && !GrabAnimations::LoadAnimations(m_bAnimsReferenced)) {
        return false;
    }

    // Start idle animation if not already playing
    if (!m_pAnim) {
        StartIdleAnimation(ped);
    }

    // Lock rotation
    ped->m_fAimingRotation = ped->m_fCurrentRotation;

    return false;
}

bool CTaskSimpleGrabHold::ShouldTransitionToAction() const
{
    if (!m_pContext) {
        return false;
    }
    return m_pContext->GetPhase() == CGrabContext::eGrabPhase::ACTION;
}

void CTaskSimpleGrabHold::StartIdleAnimation(CPed* ped)
{
    if (!ped || !ped->m_pRwClump) {
        return;
    }

    CAnimBlendHierarchy* hier = GrabAnimations::GetAnimation(GrabAnimations::ANIM_GRAB_IDLE);
    if (!hier) {
        return;
    }

    m_pAnim = CAnimManager::BlendAnimation(
        ped->m_pRwClump, 
        hier, 
        ANIMATION_IS_LOOPED, 
        8.0f
    );

    if (m_pAnim) {
        m_pAnim->ReferenceAnimBlock();
    }
}

void CTaskSimpleGrabHold::Cleanup()
{
    GrabAnimations::BlendOutAnimation(m_pAnim);
    GrabAnimations::UnloadAnimations(m_bAnimsReferenced);
}
