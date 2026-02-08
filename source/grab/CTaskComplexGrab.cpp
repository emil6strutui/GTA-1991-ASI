#include "CTaskComplexGrab.h"
#include "CTaskComplexGrabbed.h"
#include "CTaskSimpleGrabReach.h"
#include "CTaskSimpleGrabHold.h"
#include "CTaskSimpleGrabAction.h"

#include <plugin.h>
#include <CWorld.h>
#include <CPad.h>
#include <numbers>
#include <cmath>
#include <algorithm>
#include <CAnimManager.h>
#include <GrabAnimations.h>

using namespace plugin;

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
    AssignVictimTask();

    return CreateReachTask();
}

CTask* CTaskComplexGrab::CreateNextSubTask(CPed* ped)
{
    if (!m_pContext || !m_pContext->IsValid()) {
        m_bFinished = true;
        return nullptr;
    }

    auto phase = m_pContext->GetPhase();

    switch (phase) {
    case CGrabContext::eGrabPhase::REACHING:
        if (m_pContext->IsGrabberReachComplete()) {
            return CreateHoldTask();
        }
        return CreateReachTask();

    case CGrabContext::eGrabPhase::HOLDING:
        return CreateHoldTask();

    case CGrabContext::eGrabPhase::ACTION:
        // Action sub-task finished; OnActionComplete already transitioned phase
        // back to HOLDING in the anim callback, but handle this defensively.
        return CreateHoldTask();

    case CGrabContext::eGrabPhase::RELEASING:
    case CGrabContext::eGrabPhase::FINISHED: 
        m_bFinished = true;
        return nullptr;

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

    CPed* victim = m_pContext->GetVictim();
    if (victim && victim->m_fHealth <= 0.0f) {
        m_pContext->Release();
        Cleanup();
        m_bFinished = true;
        return nullptr;
    }

    if (!m_pContext->IsValid()) {
        m_bFinished = true;
        return nullptr;
    }

    auto phase = m_pContext->GetPhase();
    if (phase == CGrabContext::eGrabPhase::RELEASING || phase == CGrabContext::eGrabPhase::FINISHED) {
        CAnimBlendHierarchy* hier = GrabAnimations::GetAnimation(GrabAnimations::ANIM_GRAB_RELEASE);
        if (hier) {
            auto releaseAnimation = CAnimManager::BlendAnimation(
                ped->m_pRwClump,
                hier,
                ANIMATION_IS_PARTIAL | ANIMATION_IS_BLEND_AUTO_REMOVE,
                8.0f
            );
            
            releaseAnimation->m_fSpeed = 1.2;
            releaseAnimation->SetFinishCallback([](CAnimBlendAssociation* anim, void*) {
                if (anim) {
                    anim->m_fBlendDelta = -8.0f;
                }
            }, nullptr);
        }
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
        m_pContext->Release();
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

        if (ped->m_fHealth <= 0.0f || ped->m_pVehicle) {
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

void CTaskComplexGrab::AssignVictimTask()
{
    if (!m_pContext || m_bVictimTaskAssigned) {
        return;
    }

    CPed* victim = m_pContext->GetVictim();
    if (!victim || !victim->m_pIntelligence) {
        return;
    }

    CTaskManager* taskMgr = &victim->m_pIntelligence->m_TaskMgr;
    // Create and assign victim's task with shared context
    auto* victimTask = new CTaskComplexGrabbed(m_pContext);
    
    taskMgr->SetTask(reinterpret_cast<CTask*>(victimTask), TASK_PRIMARY_PHYSICAL_RESPONSE, false);
    
    m_bVictimTaskAssigned = true;
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
        m_pContext->Abort();
    }
}

bool CTaskComplexGrab::IsAttackPressed() const
{
    return CPad::GetPad(0)->MeleeAttackJustDown(false) != 0;
}
