#pragma once
#include <plugin.h>
#include <CTaskSimpleCustomBase.h>
#include <CPed.h>
#include <CAnimBlendAssociation.h>
#include "GrabContext.h"

class CTaskSimpleGrabHold : public CTaskSimpleCustomBase
{
public:
    static constexpr eTaskType Type = CGrabContext::TASK_SIMPLE_GRAB_HOLD;

private:
    GrabContextPtr m_pContext;
    CAnimBlendAssociation* m_pAnim = nullptr;
    bool m_bAnimsReferenced = false;
    bool m_bFinished = false;

public:
    explicit CTaskSimpleGrabHold(GrabContextPtr context);
    CTaskSimpleGrabHold(const CTaskSimpleGrabHold& other);
    ~CTaskSimpleGrabHold() override;

    // CTask virtuals
    eTaskType GetId() override { return Type; }
    CTask* Clone() override { return reinterpret_cast<CTask*>(new CTaskSimpleGrabHold(*this)); }
    bool MakeAbortable(CPed* ped, eAbortPriority priority, CEvent* event) override;

    // CTaskSimple virtuals
    bool ProcessPed(CPed* ped) override;

    bool IsFinished() const { return m_bFinished; }

private:
    void StartIdleAnimation(CPed* ped);
    void Cleanup();
};