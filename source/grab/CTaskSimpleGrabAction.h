#pragma once

#include <plugin.h>
#include <CTaskSimple.h>
#include <CPed.h>
#include <CAnimBlendAssociation.h>

#include "GrabContext.h"

/**
 * Simple task for the grabber's action phase (jab, throw, etc).
 * Plays the action animation and signals completion.
 */
class CTaskSimpleGrabAction : public CTaskSimple
{
public:
    static constexpr eTaskType Type = CGrabContext::TASK_SIMPLE_GRAB_ACTION;

private:
    GrabContextPtr m_pContext;
    CGrabContext::eGrabAction m_action;
    CAnimBlendAssociation* m_pAnim = nullptr;
    bool m_bAnimsReferenced = false;
    bool m_bFinished = false;
    bool m_bStarted = false;

public:
    CTaskSimpleGrabAction(GrabContextPtr context, CGrabContext::eGrabAction action);
    CTaskSimpleGrabAction(const CTaskSimpleGrabAction& other);
    ~CTaskSimpleGrabAction() override;

    eTaskType GetId() override { return Type; }
    CTask* GetSubTask() override { return nullptr; }
    bool IsSimple() override { return true; }
    void StopTimer(CEvent*) override { }
    CTask* Clone() override { return new CTaskSimpleGrabAction(*this); }
    bool MakeAbortable(CPed* ped, eAbortPriority priority, CEvent* event) override;
    bool ProcessPed(CPed* ped) override;
    bool SetPedPosition(CPed*) override { return false; }

    [[nodiscard]] bool IsFinished() const { return m_bFinished; }
    [[nodiscard]] CGrabContext::eGrabAction GetAction() const { return m_action; }

private:
    void StartAnimation(CPed* ped);
    void Cleanup();
    [[nodiscard]] const char* GetAnimationName() const;
    
    static void AnimFinishedCB(CAnimBlendAssociation* anim, void* data);
};
