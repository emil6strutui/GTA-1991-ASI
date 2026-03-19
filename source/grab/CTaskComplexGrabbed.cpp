#include "CTaskComplexGrabbed.h"
#include "CTaskSimpleGrabbedReach.h"
#include "CTaskSimpleGrabbedHeld.h"
#include "CTaskSimpleGrabbedHit.h"
#include "CPostGrabReaction.h"

#include <plugin.h>
#include <CAnimManager.h>

using namespace plugin;

CTaskComplexGrabbed::CTaskComplexGrabbed(GrabContextPtr context)
    : m_pContext(std::move(context))
    , m_bFinished(false)
    , m_bReactionTriggered(false)
    , m_lastActionHandled(CGrabContext::eGrabAction::NONE)
{
}

CTaskComplexGrabbed::CTaskComplexGrabbed(const CTaskComplexGrabbed& other)
    : m_pContext(other.m_pContext)
    , m_bFinished(other.m_bFinished)
    , m_bReactionTriggered(other.m_bReactionTriggered)
    , m_lastActionHandled(other.m_lastActionHandled)
{
}

CTaskComplexGrabbed::~CTaskComplexGrabbed()
{
    // Note: Can't cleanup with ped reference here
    // The subtask destructor should handle collision re-enable
    if (m_pContext) {
        m_pContext->SetVictimActive(false);
    }
}

bool CTaskComplexGrabbed::MakeAbortable(CPed* ped, eAbortPriority priority, CEvent* event)
{
    if (m_pSubTask && !m_pSubTask->MakeAbortable(ped, priority, event)) {
        return false;
    }

    Cleanup(ped);
    TriggerFallbackReaction(ped);
    m_bFinished = true;
    return true;
}

CTask* CTaskComplexGrabbed::CreateFirstSubTask(CPed* ped)
{
    if (!m_pContext || !m_pContext->IsValid()) {
        m_bFinished = true;
        return nullptr;
    }

    // Register victim as active
    m_pContext->SetVictimActive(true);

    return CreateReachTask();
}

CTask* CTaskComplexGrabbed::CreateNextSubTask(CPed* ped)
{
    if (!m_pContext) {
        m_bFinished = true;
        return nullptr;
    }

    const auto phase = m_pContext->GetPhase();
    if (phase == CGrabContext::eGrabPhase::RELEASING || phase == CGrabContext::eGrabPhase::FINISHED) {
        Cleanup(ped);
        TriggerFallbackReaction(ped);
        m_bFinished = true;
        return nullptr;
    }

    if (!m_pContext->IsValid()) {
        // Normally unreachable - ControlSubTask catches invalid context first.
        // Defensive fallback only.
        Cleanup(ped);
        TriggerFallbackReaction(ped);
        m_bFinished = true;
        return nullptr;
    }

    switch (phase) {
    case CGrabContext::eGrabPhase::REACHING:
        return CreateReachTask();

    case CGrabContext::eGrabPhase::HOLDING:
        return CreateHeldTask();

    case CGrabContext::eGrabPhase::ACTION:
        if (const auto action = m_pContext->GetCurrentAction(); action != CGrabContext::eGrabAction::NONE) {
            return reinterpret_cast<CTask*>(new CTaskSimpleGrabbedHit(m_pContext, action));
        }
        return CreateHeldTask();

    case CGrabContext::eGrabPhase::RELEASING:
    case CGrabContext::eGrabPhase::FINISHED:
        // Normally unreachable - ControlSubTask catches RELEASING/FINISHED first.
        // Defensive fallback only.
        Cleanup(ped);
        m_bFinished = true;
        return nullptr;

    default:
        Cleanup(ped);
        m_bFinished = true;
        return nullptr;
    }
}

CTask* CTaskComplexGrabbed::ControlSubTask(CPed* ped)
{
    if (!m_pContext) {
        m_bFinished = true;
        return nullptr;
    }

    const auto phase = m_pContext->GetPhase();

    // Check for release/finish first so abort-driven cleanup still gets a fallback reaction.
    if (phase == CGrabContext::eGrabPhase::RELEASING || phase == CGrabContext::eGrabPhase::FINISHED) {
        Cleanup(ped);
        TriggerFallbackReaction(ped);
        m_bFinished = true;
        return nullptr;
    }

    if (!m_pContext->IsValid()) {
        Cleanup(ped);
        TriggerFallbackReaction(ped);
        m_bFinished = true;
        return nullptr;
    }

    const auto subTaskType = m_pSubTask->GetId();

    // Transition from reach to held when grabber signals - external condition (grabber reached)
    if (subTaskType == CGrabContext::TASK_SIMPLE_GRABBED_REACH && phase == CGrabContext::eGrabPhase::HOLDING) {
        return CreateHeldTask();
    }

    // Transition from held to hit when grabber initiates an action - external condition (grabber jabbed)
    if (subTaskType == CGrabContext::TASK_SIMPLE_GRABBED_HELD && phase == CGrabContext::eGrabPhase::ACTION) {
        auto action = m_pContext->ConsumePendingAction();
        if (action != CGrabContext::eGrabAction::NONE) {
            return reinterpret_cast<CTask*>(new CTaskSimpleGrabbedHit(m_pContext, action));
        }
    }

    return m_pSubTask;
}

CTask* CTaskComplexGrabbed::CreateReachTask()
{
    return reinterpret_cast<CTask*>(new CTaskSimpleGrabbedReach(m_pContext));
}

CTask* CTaskComplexGrabbed::CreateHeldTask()
{
    return reinterpret_cast<CTask*>(new CTaskSimpleGrabbedHeld(m_pContext));
}

void CTaskComplexGrabbed::Cleanup(CPed* ped)
{
    if (m_pContext) {
        m_pContext->ForceRestoreVictimCollisionDisable();
        m_pContext->SetVictimActive(false);
    }
}

void CTaskComplexGrabbed::TriggerFallbackReaction(CPed* ped)
{
    if (m_bReactionTriggered || !ped || !m_pContext || !ped->m_pIntelligence || ped->m_fHealth <= 0.0f) {
        return;
    }

    CPed* grabber = m_pContext->GetGrabber();
    if (!grabber || grabber == ped || grabber->m_fHealth <= 0.0f) {
        return;
    }

    m_bReactionTriggered = true;
    CPostGrabReaction::TriggerReaction(ped, grabber);
}

void CTaskComplexGrabbed::EnableCollision(CPed* ped)
{
    if (ped && !ped->bCollidable) {
        ped->bCollidable = true;
    }
}
