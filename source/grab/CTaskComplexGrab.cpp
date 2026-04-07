#include "CTaskComplexGrab.h"
#include "CTaskComplexGrabbed.h"
#include "CTaskSimpleGrabReach.h"
#include "CTaskSimpleGrabHold.h"
#include "CTaskSimpleGrabAction.h"

#include <plugin.h>
#include <CWorld.h>
#include <CColPoint.h>
#include <CPad.h>
#include <CTaskManager.h>
#include <numbers>
#include <cmath>
#include <algorithm>
#include <CAnimManager.h>
#include <GrabAnimations.h>

using namespace plugin;

namespace {
    constexpr float MAX_GRAB_VERTICAL_GAP = 1.25f;

    bool HasCustomGrabTask(CPed* ped) {
        if (!ped || !ped->m_pIntelligence) {
            return false;
        }

        auto& intel = *ped->m_pIntelligence;
        return intel.FindTaskByType(CGrabContext::TASK_COMPLEX_GRABBED)
            || intel.FindTaskByType(CGrabContext::TASK_COMPLEX_GRAB);
    }

    bool HasIncompatibleVictimState(const CPed* ped) {
        switch (ped->m_ePedState) {
        case PEDSTATE_JUMP:
        case PEDSTATE_FALL:
        case PEDSTATE_GETUP:
        case PEDSTATE_STAGGER:
        case PEDSTATE_EVADE_DIVE:
        case PEDSTATE_ARREST_PLAYER:
        case PEDSTATE_OPEN_DOOR:
        case PEDSTATE_ENTER_CAR:
        case PEDSTATE_EXIT_CAR:
        case PEDSTATE_CARJACK:
        case PEDSTATE_DRAGGED_FROM_CAR:
        case PEDSTATE_HANDS_UP:
        case PEDSTATE_ARRESTED:
        case PEDSTATE_ENTER_TRAIN:
        case PEDSTATE_EXIT_TRAIN:
        case PEDSTATE_DIE:
        case PEDSTATE_DEAD:
        case PEDSTATE_DIE_BY_STEALTH:
            return true;
        default:
            return false;
        }
    }

    bool HasIncompatibleVictimLocomotion(CPed* ped) {
        if (!ped || !ped->m_pIntelligence) {
            return true;
        }

        auto& intel = *ped->m_pIntelligence;

        if (intel.IsInACarOrEnteringOne()) {
            return true;
        }

        return intel.GetTaskClimb() || intel.GetTaskInAir() || intel.GetTaskSwim();
    }

    bool CanTakeVictimResponseSlot(CPed* ped) {
        if (!ped || !ped->m_pIntelligence) {
            return false;
        }

        auto& taskMgr = ped->m_pIntelligence->m_TaskMgr;
        auto* currentTask = taskMgr.m_aPrimaryTasks[TASK_PRIMARY_PHYSICAL_RESPONSE];
        if (!currentTask) {
            return true;
        }

        const auto currentTaskId = currentTask->GetId();
        if (currentTaskId == CGrabContext::TASK_COMPLEX_GRAB || currentTaskId == CGrabContext::TASK_COMPLEX_GRABBED) {
            return false;
        }

        return currentTask->MakeAbortable(ped, ABORT_PRIORITY_IMMEDIATE, nullptr);
    }

    void ForceAbortSecondaryGrabConflicts(CPed* ped) {
        if (!ped || !ped->m_pIntelligence) {
            return;
        }

        auto& intel = *ped->m_pIntelligence;
        auto& taskMgr = intel.m_TaskMgr;

        if (auto* attackTask = taskMgr.GetTaskSecondary(TASK_SECONDARY_ATTACK)) {
            attackTask->MakeAbortable(ped, ABORT_PRIORITY_IMMEDIATE, nullptr);
            taskMgr.SetTaskSecondary(nullptr, TASK_SECONDARY_ATTACK);
        }

        if (auto* partialAnimTask = taskMgr.GetTaskSecondary(TASK_SECONDARY_PARTIAL_ANIM)) {
            partialAnimTask->MakeAbortable(ped, ABORT_PRIORITY_IMMEDIATE, nullptr);
            taskMgr.SetTaskSecondary(nullptr, TASK_SECONDARY_PARTIAL_ANIM);
        }

        if (taskMgr.GetTaskSecondary(TASK_SECONDARY_DUCK)) {
            intel.ClearTaskDuckSecondary();
        }
    }

    bool HasLineOfSight(CPed* grabber, CPed* victim) {
        if (!grabber || !victim || !grabber->CanSeeEntity(victim, 100.0f)) {
            return false;
        }

        CColPoint hitPoint{};
        CEntity* hitEntity = nullptr;
        const CVector start = grabber->GetPosition() + CVector(0.0f, 0.0f, 0.6f);
        const CVector end = victim->GetPosition() + CVector(0.0f, 0.0f, 0.6f);

        if (!CWorld::ProcessLineOfSight(start, end, hitPoint, hitEntity, true, true, true, true, false, true, true, false)) {
            return true;
        }

        return hitEntity == victim;
    }
}

CTaskComplexGrab::CTaskComplexGrab()
{
    // Base class already initializes m_pParentTask and m_pSubTask to nullptr
    m_pContext = nullptr;
    m_bFinished = false;
    m_bVictimTaskAssigned = false;
}

CTaskComplexGrab::CTaskComplexGrab(const CTaskComplexGrab& other)
{
    m_pContext = other.m_pContext;
    m_bFinished = other.m_bFinished;
    m_bVictimTaskAssigned = other.m_bVictimTaskAssigned;
}

CTaskComplexGrab::~CTaskComplexGrab()
{
    Cleanup();
}

bool CTaskComplexGrab::MakeAbortable(CPed* ped, eAbortPriority priority, CEvent* event)
{
    if (m_pSubTask && !m_pSubTask->MakeAbortable(ped, priority, event)) {
        return false;
    }

    Cleanup();
    m_bFinished = true;
    return true;
}

CTask* CTaskComplexGrab::CreateFirstSubTask(CPed* ped)
{
    if (!InitializeGrab(ped)) {
        m_bFinished = true;
        return nullptr;
    }

    // Register grabber as active
    if (m_pContext) {
        m_pContext->SetGrabberActive(true);
    }

    // Assign victim task
    if (!AssignVictimTask()) {
        Cleanup();
        m_bFinished = true;
        return nullptr;
    }

    return CreateReachTask();
}

CTask* CTaskComplexGrab::CreateNextSubTask(CPed* ped)
{
    if (!m_pContext) {
        m_bFinished = true;
        return nullptr;
    }

    if (!m_pContext->IsValid()) {
        if (!m_pContext->HasEnded()) {
            m_pContext->Abort(CGrabContext::eGrabEndReason::INVALID_CONTEXT);
        }
        m_bFinished = true;
        return nullptr;
    }

    const auto phase = m_pContext->GetPhase();

    if (phase == CGrabContext::eGrabPhase::RELEASING || phase == CGrabContext::eGrabPhase::FINISHED) {
        m_bFinished = true;
        return nullptr;
    }

    // Progress based on the subtask that just finished rather than only the
    // shared phase. One side can finish a paired stage a frame earlier.
    switch (m_pSubTask ? m_pSubTask->GetId() : static_cast<eTaskType>(-1)) {
    case CGrabContext::TASK_SIMPLE_GRAB_REACH:
    case CGrabContext::TASK_SIMPLE_GRAB_ACTION:
        return CreateHoldTask();

    case CGrabContext::TASK_SIMPLE_GRAB_HOLD:
        if (const auto action = m_pContext->GetCurrentAction(); action != CGrabContext::eGrabAction::NONE) {
            return CreateActionTask(action);
        }
        return CreateHoldTask();

    default:
        break;
    }

    switch (phase) {
    case CGrabContext::eGrabPhase::REACHING:
        return CreateReachTask();

    case CGrabContext::eGrabPhase::HOLDING:
        return CreateHoldTask();

    case CGrabContext::eGrabPhase::ACTION:
        if (const auto action = m_pContext->GetCurrentAction(); action != CGrabContext::eGrabAction::NONE) {
            return CreateActionTask(action);
        }
        return CreateHoldTask();

    default:
        m_bFinished = true;
        return nullptr;
    }
}

CTask* CTaskComplexGrab::ControlSubTask(CPed* ped)
{
    if (!m_pContext) {
        m_bFinished = true;
        return nullptr;
    }

    const auto phase = m_pContext->GetPhase();
    if (phase == CGrabContext::eGrabPhase::RELEASING || phase == CGrabContext::eGrabPhase::FINISHED) {
        if (m_pContext->IsSoftRelease() && ped && ped->m_pRwClump) {
            if (CAnimBlendHierarchy* hier = GrabAnimations::GetAnimation(GrabAnimations::ANIM_GRAB_RELEASE)) {
                if (auto* releaseAnimation = CAnimManager::BlendAnimation(
                    ped->m_pRwClump,
                    hier,
                    ANIMATION_IS_PARTIAL | ANIMATION_IS_BLEND_AUTO_REMOVE,
                    8.0f
                )) {
                    releaseAnimation->m_fSpeed = 1.2f;
                    releaseAnimation->SetFinishCallback([](CAnimBlendAssociation* anim, void*) {
                        if (anim) {
                            anim->m_fBlendDelta = -8.0f;
                        }
                    }, nullptr);
                }
            }
        }

        m_bFinished = true;
        return nullptr;
    }

    CPed* victim = m_pContext->GetVictim();
    if (victim && victim->m_fHealth <= 0.0f) {
        m_pContext->Abort(CGrabContext::eGrabEndReason::VICTIM_DIED);
        m_bFinished = true;
        return nullptr;
    }

    if (!m_pContext->IsValid()) {
        if (!m_pContext->HasEnded()) {
            m_pContext->Abort(CGrabContext::eGrabEndReason::INVALID_CONTEXT);
        }
        m_bFinished = true;
        return nullptr;
    }

    if (!m_pSubTask) {
        m_bFinished = true;
        return nullptr;
    }

    const auto subTaskType = m_pSubTask->GetId();

    if (subTaskType == CGrabContext::TASK_SIMPLE_GRAB_HOLD && phase == CGrabContext::eGrabPhase::HOLDING) {
        if (IsAttackPressed()) {
            m_pContext->RequestAction(CGrabContext::eGrabAction::JAB);
            return CreateActionTask(CGrabContext::eGrabAction::JAB);
        }
    }

    return m_pSubTask;
}

CGrabContext::eGrabPhase CTaskComplexGrab::GetPhase() const
{
    if (!m_pContext) {
        return CGrabContext::eGrabPhase::FINISHED;
    }
    return m_pContext->GetPhase();
}

void CTaskComplexGrab::RequestAction(CGrabContext::eGrabAction action)
{
    if (m_pContext) {
        m_pContext->RequestAction(action);
    }
}

void CTaskComplexGrab::ReleaseVictim()
{
    if (m_pContext) {
        m_pContext->Release(CGrabContext::eGrabEndReason::MANUAL_RELEASE);
    }
}

CPed* CTaskComplexGrab::FindValidVictim(CPed* grabber, float* outDistance)
{
    if (!grabber) {
        return nullptr;
    }

    CVector grabberPos = grabber->GetPosition();
    float grabberHeading = grabber->m_fCurrentRotation;

    short numFound = 0;
    CEntity* entities[16];

    CWorld::FindObjectsInRange(grabberPos, GRAB_RANGE, true, &numFound, 16, entities,
                               false, false, true, false, false);

    CPed* bestVictim = nullptr;
    float bestScore = -1.0f;
    float bestDistance = 0.0f;

    constexpr float pi = std::numbers::pi_v<float>;
    constexpr float twoPi = 2.0f * pi;

    for (int i = 0; i < numFound; i++) {
        CEntity* entity = entities[i];
        if (!entity || entity == grabber || entity->m_nType != ENTITY_TYPE_PED) {
            continue;
        }

        auto* ped = static_cast<CPed*>(entity);

        if (ped->m_fHealth <= 0.0f || ped->m_pVehicle || !ped->m_pIntelligence) {
            continue;
        }

        if (std::abs(ped->GetPosition().z - grabberPos.z) > MAX_GRAB_VERTICAL_GAP) {
            continue;
        }

        if (ped->bIsInTheAir || ped->bIsLanding || ped->bIsBeingArrested || ped->bHasAScriptBrain) {
            continue;
        }

        if (HasCustomGrabTask(ped) || HasIncompatibleVictimState(ped) || HasIncompatibleVictimLocomotion(ped)) {
            continue;
        }

        if (!HasLineOfSight(grabber, ped)) {
            continue;
        }

        CVector toVictim = ped->GetPosition() - grabberPos;
        float angleToVictim = std::atan2(-toVictim.x, toVictim.y);
        float angleDiff = grabberHeading - angleToVictim;

        while (angleDiff > pi) angleDiff -= twoPi;
        while (angleDiff < -pi) angleDiff += twoPi;

        float angleDiffDeg = std::abs(angleDiff) * (180.0f / pi);

        if (angleDiffDeg > GRAB_ANGLE) {
            continue;
        }

        float dist = toVictim.Magnitude2D();
        float score = (GRAB_RANGE - dist) * (GRAB_ANGLE - angleDiffDeg);

        if (score > bestScore) {
            bestScore = score;
            bestVictim = ped;
            bestDistance = dist;
        }
    }

    if (outDistance && bestVictim) {
        *outDistance = bestDistance;
    }

    return bestVictim;
}

bool CTaskComplexGrab::InitializeGrab(CPed* grabber)
{
    float distance = 0.0f;
    CPed* victim = FindValidVictim(grabber, &distance);

    if (!victim) {
        return false;  // No valid victim found
    }

    // Create shared context
    m_pContext = CGrabContext::Create(grabber, victim, distance);
    
    return m_pContext != nullptr;
}

bool CTaskComplexGrab::AssignVictimTask()
{
    if (!m_pContext || m_bVictimTaskAssigned) {
        return m_bVictimTaskAssigned;
    }

    CPed* victim = m_pContext->GetVictim();
    if (!victim || !victim->m_pIntelligence) {
        return false;
    }

    CTaskManager* taskMgr = &victim->m_pIntelligence->m_TaskMgr;
    if (!CanTakeVictimResponseSlot(victim)) {
        return false;
    }

    ForceAbortSecondaryGrabConflicts(victim);

    // Create and assign victim's task with shared context
    auto* victimTask = new CTaskComplexGrabbed(m_pContext);
    
    taskMgr->SetTask(reinterpret_cast<CTask*>(victimTask), TASK_PRIMARY_PHYSICAL_RESPONSE, false);
    
    m_bVictimTaskAssigned = true;
    return true;
}

CTask* CTaskComplexGrab::CreateReachTask()
{
    return reinterpret_cast<CTask*>(new CTaskSimpleGrabReach(m_pContext));
}

CTask* CTaskComplexGrab::CreateHoldTask()
{
    return reinterpret_cast<CTask*>(new CTaskSimpleGrabHold(m_pContext));
}

CTask* CTaskComplexGrab::CreateActionTask(CGrabContext::eGrabAction action)
{
    return reinterpret_cast<CTask*>(new CTaskSimpleGrabAction(m_pContext, action));
}

void CTaskComplexGrab::Cleanup()
{
    if (m_pContext) {
        m_pContext->SetGrabberActive(false);
        if (!m_pContext->HasEnded()) {
            m_pContext->Abort(CGrabContext::eGrabEndReason::ATTACKER_ABORTED);
        }
    }
}

bool CTaskComplexGrab::IsAttackPressed() const
{
    return CPad::GetPad(0)->MeleeAttackJustDown(false) != 0;
}
