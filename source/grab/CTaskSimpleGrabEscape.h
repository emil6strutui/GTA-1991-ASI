#pragma once

#include <plugin.h>
#include <CTaskSimpleCustomBase.h>
#include <CPed.h>
#include <CAnimBlendAssociation.h>

#include "GrabContext.h"

class CTaskSimpleGrabEscape : public CTaskSimpleCustomBase
{
public:
    static constexpr eTaskType Type = CGrabContext::TASK_SIMPLE_GRAB_ESCAPE;

private:
    GrabContextPtr m_pContext;
    CAnimBlendAssociation* m_pAnim = nullptr;
    bool m_bAnimsReferenced = false;
    bool m_bFinished = false;
    bool m_bAnimFinished = false;
    bool m_bStarted = false;

public:
    explicit CTaskSimpleGrabEscape(GrabContextPtr context);
    CTaskSimpleGrabEscape(const CTaskSimpleGrabEscape& other);
    ~CTaskSimpleGrabEscape() override;

    eTaskType GetId() override { return Type; }
    CTask* Clone() override { return reinterpret_cast<CTask*>(new CTaskSimpleGrabEscape(*this)); }
    bool MakeAbortable(CPed* ped, eAbortPriority priority, CEvent* event) override;
    bool ProcessPed(CPed* ped) override;

    [[nodiscard]] bool IsFinished() const { return m_bFinished; }

private:
    void StartAnimation(CPed* ped);
    void Finish();
    void Cleanup(bool releaseAnimation);
    [[nodiscard]] bool ShouldReleaseBreakAnimation() const;

    static void AnimFinishedCB(CAnimBlendAssociation* anim, void* data);
};
