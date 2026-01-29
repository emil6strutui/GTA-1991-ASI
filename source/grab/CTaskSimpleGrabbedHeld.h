#pragma once
#include <plugin.h>
#include <CTaskSimpleCustomBase.h>
#include <CPed.h>
#include <CAnimBlendAssociation.h>
#include "GrabContext.h"

class CTaskSimpleGrabbedHeld : public CTaskSimpleCustomBase
{
public:
    static constexpr eTaskType Type = CGrabContext::TASK_SIMPLE_GRABBED_HELD;

private:
    GrabContextPtr m_pContext;
    CAnimBlendAssociation* m_pAnim = nullptr;
    bool m_bAnimsReferenced = false;
    bool m_bFinished = false;
    bool m_bCollisionDisabled = false;

public:
    explicit CTaskSimpleGrabbedHeld(GrabContextPtr context);
    CTaskSimpleGrabbedHeld(const CTaskSimpleGrabbedHeld& other);
    ~CTaskSimpleGrabbedHeld() override;

    // CTask virtuals
    eTaskType GetId() override { return Type; }
    CTask* Clone() override { return reinterpret_cast<CTask*>(new CTaskSimpleGrabbedHeld(*this)); }
    bool MakeAbortable(CPed* ped, eAbortPriority priority, CEvent* event) override;

    // CTaskSimple virtuals
    bool ProcessPed(CPed* ped) override;
    bool SetPedPosition(CPed* ped) override;

    bool IsFinished() const { return m_bFinished; }
    bool ShouldTransitionToHit() const;

private:
    void StartIdleAnimation(CPed* ped);
    void Cleanup(CPed* ped);
    void PositionVictim(CPed* ped) const;
};