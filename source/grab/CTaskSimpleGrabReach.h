#pragma once
#include <plugin.h>
#include <CTaskSimpleCustomBase.h>
#include <CPed.h>
#include <CAnimBlendAssociation.h>
#include "GrabContext.h"

class CTaskSimpleGrabReach : public CTaskSimpleCustomBase
{
public:
    static constexpr eTaskType Type = CGrabContext::TASK_SIMPLE_GRAB_REACH;

private:
    GrabContextPtr m_pContext;
    CAnimBlendAssociation* m_pAnim = nullptr;
    bool m_bAnimsReferenced = false;
    bool m_bFinished = false;
    bool m_bStarted = false;

public:
    explicit CTaskSimpleGrabReach(GrabContextPtr context);
    CTaskSimpleGrabReach(const CTaskSimpleGrabReach& other);
    ~CTaskSimpleGrabReach() override;

    // CTask virtuals
    eTaskType GetId() override { return Type; }
    CTask* Clone() override { return reinterpret_cast<CTask*>(new CTaskSimpleGrabReach(*this)); }
    bool MakeAbortable(CPed* ped, eAbortPriority priority, CEvent* event) override;

    // CTaskSimple virtuals
    bool ProcessPed(CPed* ped) override;

    bool IsFinished() const { return m_bFinished; }

private:
    void StartAnimation(CPed* ped);
    void Cleanup();
    static void AnimFinishedCB(CAnimBlendAssociation* anim, void* data);
};