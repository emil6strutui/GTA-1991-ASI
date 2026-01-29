#include "CTaskSimpleGrabAction.h"
#include "GrabAnimations.h"

#include <plugin.h>
#include <CAnimManager.h>

using namespace plugin;

CTaskSimpleGrabAction::CTaskSimpleGrabAction(GrabContextPtr context, CGrabContext::eGrabAction action)
    : CTaskSimple(plugin::dummy)
    , m_pContext(std::move(context))
    , m_action(action)
{}

CTaskSimpleGrabAction::CTaskSimpleGrabAction(const CTaskSimpleGrabAction& other)
    : CTaskSimple(plugin::dummy)
    , m_pContext(other.m_pContext)
    , m_action(other.m_action)
    , m_bAnimsReferenced(false)
    , m_bFinished(other.m_bFinished)
    , m_bStarted(other.m_bStarted)
{
}

CTaskSimpleGrabAction::~CTaskSimpleGrabAction()
{
    Cleanup();
}

bool CTaskSimpleGrabAction::MakeAbortable(CPed* ped, eAbortPriority priority, CEvent* event)
{
    // Actions are not interruptible except for immediate priority
    if (priority != ABORT_PRIORITY_IMMEDIATE && !m_bFinished) {
        return false;
    }

    Cleanup();
    m_bFinished = true;
    
    if (m_pContext) {
        m_pContext->OnActionComplete();
    }
    
    return true;
}

bool CTaskSimpleGrabAction::ProcessPed(CPed* ped)
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

    // Load animations if needed
    if (!m_bAnimsReferenced && !GrabAnimations::LoadAnimations(m_bAnimsReferenced)) {
        return false;
    }

    // Start animation on first process
    if (!m_bStarted) {
        StartAnimation(ped);
        m_bStarted = true;
    }

    // Lock rotation
    ped->m_fAimingRotation = ped->m_fCurrentRotation;

    return false;
}

void CTaskSimpleGrabAction::StartAnimation(CPed* ped)
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
        0x0,  // No special flags
        8.0f
    );

    if (m_pAnim) {
        m_pAnim->ReferenceAnimBlock();
        m_pAnim->SetFinishCallback(AnimFinishedCB, this);
    } else {
        m_bFinished = true;
    }
}

void CTaskSimpleGrabAction::Cleanup()
{
    GrabAnimations::CleanupAnimation(m_pAnim);
    GrabAnimations::UnloadAnimations(m_bAnimsReferenced);
}

const char* CTaskSimpleGrabAction::GetAnimationName() const
{
    switch (m_action) {
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

void CTaskSimpleGrabAction::AnimFinishedCB(CAnimBlendAssociation*, void* data)
{
    auto* task = static_cast<CTaskSimpleGrabAction*>(data);
    if (!task) {
        return;
    }

    task->m_pAnim = nullptr;
    task->m_bFinished = true;

    // Signal action completion to context
    if (task->m_pContext) {
        task->m_pContext->OnActionComplete();
    }
}
