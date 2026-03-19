#pragma once

#include <plugin.h>
#include <CTaskSimpleCustomBase.h>
#include <CPed.h>
#include <CAnimBlendAssociation.h>

#include "GrabContext.h"

/**
 * Simple task for victim's reaction to being hit while grabbed.
 * Plays hit reaction animation then returns to held state.
 */
class CTaskSimpleGrabbedHit : public CTaskSimpleCustomBase
{
public:
    static constexpr eTaskType Type = CGrabContext::TASK_SIMPLE_GRABBED_HIT;

private:
    GrabContextPtr m_pContext;
    CGrabContext::eGrabAction m_hitType;
    CAnimBlendAssociation* m_pAnim = nullptr;
    bool m_bAnimsReferenced = false;
    bool m_bFinished = false;
    bool m_bAnimFinished = false;
    bool m_bStarted = false;
    bool m_bCollisionDisabled = false;
    bool m_bDamageApplied = false;

public:
    CTaskSimpleGrabbedHit(GrabContextPtr context, CGrabContext::eGrabAction hitType);
    CTaskSimpleGrabbedHit(const CTaskSimpleGrabbedHit& other);
    ~CTaskSimpleGrabbedHit() override;

    eTaskType GetId() override { return Type; }
    CTask* Clone() override { return reinterpret_cast<CTask*>(new CTaskSimpleGrabbedHit(*this)); }
    bool MakeAbortable(CPed* ped, eAbortPriority priority, CEvent* event) override;
    bool ProcessPed(CPed* ped) override;
    bool SetPedPosition(CPed* ped) override;

    [[nodiscard]] bool IsFinished() const { return m_bFinished; }

private:
    void StartAnimation(CPed* ped);
    void CheckDamageTrigger(CPed* ped);
    void FinishEarly();
    void Cleanup();
    void PositionVictim(CPed* ped) const;
    [[nodiscard]] const char* GetAnimationName() const;
    
    static void AnimFinishedCB(CAnimBlendAssociation* anim, void* data);
};
