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
    , m_lastActionHandled(CGrabContext::eGrabAction::NONE)
{
}

CTaskComplexGrabbed::CTaskComplexGrabbed(const CTaskComplexGrabbed& other)
    : m_pContext(other.m_pContext)
    , m_bFinished(other.m_bFinished)
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
    if (!m_pContext || !m_pContext->IsValid()) {
        // Normally unreachable - ControlSubTask catches invalid context first.
        // Defensive fallback only.
        Cleanup(ped);
        m_bFinished = true;
        return nullptr;
    }

    auto phase = m_pContext->GetPhase();

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
    if (!m_pContext || !m_pContext->IsValid()) {
        Cleanup(ped);
        m_bFinished = true;
        return nullptr;
    }

    const auto subTaskType = m_pSubTask->GetId();
    const auto phase = m_pContext->GetPhase();

    // Check for release/finish - external condition (grabber released us)
    if (phase == CGrabContext::eGrabPhase::RELEASING || phase == CGrabContext::eGrabPhase::FINISHED) {
        Cleanup(ped);
        m_bFinished = true;

        // Trigger post-grab reaction so victim responds to attacker
        if (ped && m_pContext) {
            CPed* grabber = m_pContext->GetGrabber();
            if (grabber && ped->m_pIntelligence) {
                CPostGrabReaction::TriggerReaction(ped, grabber);
            }
        }

        return nullptr;
    }

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
    EnableCollision(ped);
    
    if (m_pContext) {
        m_pContext->SetVictimActive(false);
    }
}

void CTaskComplexGrabbed::EnableCollision(CPed* ped)
{
    if (ped && !ped->bCollidable) {
        ped->bCollidable = true;
    }
}
