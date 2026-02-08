#include "CTaskSimpleGrabAction.h"
#include "GrabAnimations.h"

#include <plugin.h>
#include <CAnimManager.h>
#include <CAnimBlendHierarchy.h>
#include <eAudioEvents.h>

using namespace plugin;

// CAEPedAudioEntity::AddAudioEvent is not declared in plugin-sdk, call via raw address
using FnAddAudioEvent = void(__thiscall*)(
    void* thisPtr,          // CAEPedAudioEntity*
    int32_t event,          // eAudioEvents
    float volume,
    float speed,
    void* physical,         // CPhysical* (victim for hit events)
    int32_t surfaceId,
    int32_t a7,
    uint32_t maxVol
);
static auto PedAudio_AddAudioEvent = reinterpret_cast<FnAddAudioEvent>(0x4E2BB0);

CTaskSimpleGrabAction::CTaskSimpleGrabAction(GrabContextPtr context, CGrabContext::eGrabAction action)
    : m_pContext(std::move(context))
    , m_action(action)
{}

CTaskSimpleGrabAction::CTaskSimpleGrabAction(const CTaskSimpleGrabAction& other)
    : m_pContext(other.m_pContext)
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
        m_pContext->OnGrabberActionComplete();
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

    // Check if the punch connects at 70% of the animation
    CheckHitTrigger(ped);

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
        0x0,  
        8.0f
    );

    if (m_pAnim) {
        m_pAnim->ReferenceAnimBlock();
        m_pAnim->SetFinishCallback(AnimFinishedCB, this);
    } else {
        m_bFinished = true;
    }
}

void CTaskSimpleGrabAction::CheckHitTrigger(CPed* ped)
{
    if (m_bHitTriggered || !m_pAnim || !m_pContext) {
        return;
    }

    // Calculate the 60% mark of the animation
    float totalTime = m_pAnim->m_pHierarchy->m_fTotalTime;
    float hitTime = totalTime * 0.6f;

    // Frame-crossing detection: did we cross the hit point THIS frame?
    // Same pattern as TaskSimpleThrowProjectile in the game
    if (hitTime < m_pAnim->m_fCurrentTime
        && (m_pAnim->m_fCurrentTime - m_pAnim->fTimeStep) <= hitTime)
    {
        m_bHitTriggered = true;

        // Play punch impact SFX on the attacker's audio entity
        CPed* victim = m_pContext->GetVictim();
        PedAudio_AddAudioEvent(
            &ped->m_pedAudio,
            AE_PED_HIT_HIGH_UNARMED,   // 0x41 - unarmed punch, high
            0.0f,                       // volume (default)
            1.0f,                       // speed (default)
            victim,                     // victim physical for impact
            0,                          // surface (default)
            0, 0                        // extra params
        );

        // Signal to victim's task that the hit connected
        m_pContext->SignalHitConnected();
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

    // Signal grabber side completion to context
    if (task->m_pContext) {
        task->m_pContext->OnGrabberActionComplete();
    }
}
