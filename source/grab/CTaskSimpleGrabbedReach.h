#pragma once
#include <plugin.h>
#include <CTaskSimpleCustomBase.h>
#include <CPed.h>
#include <CAnimBlendAssociation.h>
#include "GrabContext.h"

class CTaskSimpleGrabbedReach : public CTaskSimpleCustomBase
{
public:
    static constexpr eTaskType Type = CGrabContext::TASK_SIMPLE_GRABBED_REACH;

private:
    GrabContextPtr m_pContext;
    CAnimBlendAssociation* m_pAnim = nullptr;
    bool m_bAnimsReferenced = false;
    bool m_bFinished = false;
    bool m_bAnimFinished = false;
    bool m_bStarted = false;
    bool m_bCollisionDisabled = false;

public:
    explicit CTaskSimpleGrabbedReach(GrabContextPtr context);
    CTaskSimpleGrabbedReach(const CTaskSimpleGrabbedReach& other);
    ~CTaskSimpleGrabbedReach() override;

    // CTask virtuals
    eTaskType GetId() override { return Type; }
    CTask* Clone() override { return reinterpret_cast<CTask*>(new CTaskSimpleGrabbedReach(*this)); }
    bool MakeAbortable(CPed* ped, eAbortPriority priority, CEvent* event) override;

    // CTaskSimple virtuals
    bool ProcessPed(CPed* ped) override;
    bool SetPedPosition(CPed* ped) override;

    bool IsFinished() const { return m_bFinished; }

private:
    void StartAnimation(CPed* ped);
    void Cleanup();
    void PositionVictim(CPed* ped) const;
    static void AnimFinishedCB(CAnimBlendAssociation* anim, void* data);
};
