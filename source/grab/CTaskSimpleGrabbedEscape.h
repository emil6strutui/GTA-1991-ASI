#pragma once

#include <plugin.h>
#include <CTaskSimpleCustomBase.h>
#include <CPed.h>
#include <CAnimBlendAssociation.h>

#include "GrabContext.h"

class CTaskSimpleGrabbedEscape : public CTaskSimpleCustomBase
{
public:
    static constexpr eTaskType Type = CGrabContext::TASK_SIMPLE_GRABBED_ESCAPE;

private:
    GrabContextPtr m_pContext;
    CAnimBlendAssociation* m_pAnim = nullptr;
    bool m_bAnimsReferenced = false;
    bool m_bFinished = false;
    bool m_bAnimFinished = false;
    bool m_bStarted = false;
    bool m_bCollisionDisabled = false;
    bool m_bImpactTriggered = false;

public:
    explicit CTaskSimpleGrabbedEscape(GrabContextPtr context);
    CTaskSimpleGrabbedEscape(const CTaskSimpleGrabbedEscape& other);
    ~CTaskSimpleGrabbedEscape() override;

    eTaskType GetId() override { return Type; }
    CTask* Clone() override { return reinterpret_cast<CTask*>(new CTaskSimpleGrabbedEscape(*this)); }
    bool MakeAbortable(CPed* ped, eAbortPriority priority, CEvent* event) override;
    bool ProcessPed(CPed* ped) override;
    bool SetPedPosition(CPed* ped) override;

    [[nodiscard]] bool IsFinished() const { return m_bFinished; }

private:
    void StartAnimation(CPed* ped);
    void CheckImpactTrigger(CPed* ped);
    void TriggerEscapeImpact(CPed* ped);
    void Finish();
    void Cleanup();
    void PositionVictim(CPed* ped) const;

    static void AnimFinishedCB(CAnimBlendAssociation* anim, void* data);
};
