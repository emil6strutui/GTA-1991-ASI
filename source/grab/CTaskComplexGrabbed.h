#pragma once
#include <plugin.h>
#include <CTaskComplexCustomBase.h>
#include <CPed.h>
#include "GrabContext.h"

class CTaskComplexGrabbed : public CTaskComplexCustomBase
{
public:
    static constexpr eTaskType Type = CGrabContext::TASK_COMPLEX_GRABBED;

private:
    GrabContextPtr m_pContext;
    bool m_bFinished = false;
    CGrabContext::eGrabAction m_lastActionHandled = CGrabContext::eGrabAction::NONE;

public:
    explicit CTaskComplexGrabbed(GrabContextPtr context);
    CTaskComplexGrabbed(const CTaskComplexGrabbed& other);
    ~CTaskComplexGrabbed() override;

    // CTask virtuals
    eTaskType GetId() override { return Type; }
    CTask* Clone() override { return reinterpret_cast<CTask*>(new CTaskComplexGrabbed(*this)); }
    void StopTimer(CEvent*) override {}
    bool MakeAbortable(CPed* ped, eAbortPriority priority, CEvent* event) override;

    // CTaskComplex virtuals
    CTask* CreateFirstSubTask(CPed* ped) override;
    CTask* CreateNextSubTask(CPed* ped) override;
    CTask* ControlSubTask(CPed* ped) override;

    bool IsFinished() const { return m_bFinished; }
    GrabContextPtr GetContext() const { return m_pContext; }

private:
    CTask* CreateReachTask();
    CTask* CreateHeldTask();
    void Cleanup(CPed* ped);
    void EnableCollision(CPed* ped);
};