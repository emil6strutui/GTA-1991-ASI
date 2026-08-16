#include "CTaskSimpleGrabEscape.h"
#include "GrabAnimations.h"

#include <CAnimManager.h>
#include <CAnimBlendHierarchy.h>
#include <utility>

CTaskSimpleGrabEscape::CTaskSimpleGrabEscape(GrabContextPtr context)
    : m_pContext(std::move(context))
{
}

CTaskSimpleGrabEscape::CTaskSimpleGrabEscape(const CTaskSimpleGrabEscape& other)
    : m_pContext(nullptr)
    , m_pAnim(nullptr)
    , m_bAnimsReferenced(false)
    , m_bFinished(true)
    , m_bAnimFinished(true)
    , m_bStarted(false)
{
}

CTaskSimpleGrabEscape::~CTaskSimpleGrabEscape()
{
    Cleanup(ShouldReleaseBreakAnimation());
}

bool CTaskSimpleGrabEscape::MakeAbortable(CPed* ped, eAbortPriority priority, CEvent* event)
{
    (void)event;

    const auto phase = m_pContext ? m_pContext->GetPhase() : CGrabContext::eGrabPhase::FINISHED;

    if (priority != ABORT_PRIORITY_IMMEDIATE
        && !m_bFinished
        && phase != CGrabContext::eGrabPhase::RELEASING
        && phase != CGrabContext::eGrabPhase::FINISHED) {
        return false;
    }

    if (ShouldReleaseBreakAnimation()) {
        Cleanup(true);
    } else {
        GrabAnimations::AbortAnimation(ped, m_pAnim, priority);
        GrabAnimations::UnloadAnimations(m_bAnimsReferenced);
    }

    Finish();
    return true;
}

bool CTaskSimpleGrabEscape::ProcessPed(CPed* ped)
{
    if (m_bFinished) {
        return true;
    }

    if (!m_pContext || !m_pContext->ArePedsValid()) {
        Cleanup(false);
        Finish();
        return true;
    }

    if (m_pContext->GetPhase() != CGrabContext::eGrabPhase::ACTION) {
        Cleanup(ShouldReleaseBreakAnimation());
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

    if (m_pContext->HasEscapeImpactTriggered()) {
        Cleanup(true);
        Finish();
        return true;
    }

    if (ped) {
        ped->m_fHeadingGoal = ped->m_fHeadingCurrent;
    }

    return false;
}

void CTaskSimpleGrabEscape::StartAnimation(CPed* ped)
{
    if (!ped || !ped->m_pRwClump) {
        m_bAnimFinished = true;
        if (m_pContext) {
            m_pContext->OnGrabberActionComplete();
        }
        return;
    }

    CAnimBlendHierarchy* hier = GrabAnimations::GetAnimation(GrabAnimations::ANIM_GRAB_BREAK);
    if (!hier) {
        m_bAnimFinished = true;
        if (m_pContext) {
            m_pContext->OnGrabberActionComplete();
        }
        return;
    }

    m_pAnim = CAnimManager::BlendAnimation(
        ped->m_pRwClump,
        hier,
        ANIMATION_FREEZE_LAST_FRAME | ANIMATION_UNLOCK_LAST_FRAME,
        4.0f
    );

    if (m_pAnim) {
        m_pAnim->ReferenceAnimBlock();
        m_pAnim->m_fSpeed = GrabAnimations::ESCAPE_BREAK_ANIM_SPEED;
        m_pAnim->SetFinishCallback(AnimFinishedCB, this);
    } else {
        m_bAnimFinished = true;
        if (m_pContext) {
            m_pContext->OnGrabberActionComplete();
        }
    }
}

void CTaskSimpleGrabEscape::Finish()
{
    if (m_bFinished) {
        return;
    }

    m_bFinished = true;
    if (m_pContext) {
        m_pContext->OnGrabberActionComplete();
    }
}

void CTaskSimpleGrabEscape::Cleanup(bool releaseAnimation)
{
    if (releaseAnimation) {
        GrabAnimations::ReleaseAnimation(m_pAnim);
    } else {
        GrabAnimations::CleanupAnimation(m_pAnim);
    }

    GrabAnimations::UnloadAnimations(m_bAnimsReferenced);
}

bool CTaskSimpleGrabEscape::ShouldReleaseBreakAnimation() const
{
    return m_pContext
        && m_pContext->GetEndReason() == CGrabContext::eGrabEndReason::VICTIM_ESCAPED;
}

void CTaskSimpleGrabEscape::AnimFinishedCB(CAnimBlendAssociation*, void* data)
{
    auto* task = static_cast<CTaskSimpleGrabEscape*>(data);
    if (!task) {
        return;
    }

    task->m_pAnim = nullptr;
    task->m_bAnimFinished = true;

    if (task->m_pContext) {
        task->m_pContext->OnGrabberActionComplete();
    }
}
