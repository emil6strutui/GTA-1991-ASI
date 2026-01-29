#pragma once
#include <plugin.h>
#include <CTaskComplexCustomBase.h>
#include <CPed.h>
#include <CPad.h>
#include "GrabContext.h"

class CTaskComplexGrabbed;

/**
 * Complex task managing the grabber's side of the grab interaction.
 * Controls subtask transitions: Reach -> Hold <-> Action
 *
 * This task owns the GrabContext and shares it with CTaskComplexGrabbed.
 */
class CTaskComplexGrab : public CTaskComplexCustomBase
{
public:
    static constexpr eTaskType Type = CGrabContext::TASK_COMPLEX_GRAB;

    // Victim finding constants
    static constexpr float GRAB_RANGE = 1.5f;
    static constexpr float GRAB_ANGLE = 45.0f;      // Degrees
    static constexpr float SNAP_DISTANCE = 0.5f;    // Instant grab if closer

private:
    GrabContextPtr m_pContext;
    bool m_bFinished = false;
    bool m_bVictimTaskAssigned = false;

public:
    CTaskComplexGrab();
    CTaskComplexGrab(const CTaskComplexGrab& other);
    ~CTaskComplexGrab() override;

    // Override ALL virtuals from CTask
    eTaskType GetId() override { return Type; }
    CTask* Clone() override { return reinterpret_cast<CTask*>(new CTaskComplexGrab(*this)); }
    CTask* GetSubTask() override { return m_pSubTask; }
    bool IsSimple() override { return false; }
    void StopTimer(CEvent*) override {}
    bool MakeAbortable(CPed* ped, eAbortPriority priority, CEvent* event) override;

    // CTaskComplex virtuals
    CTask* CreateFirstSubTask(CPed* ped) override;
    CTask* CreateNextSubTask(CPed* ped) override;
    CTask* ControlSubTask(CPed* ped) override;

    // Public interface
    [[nodiscard]] GrabContextPtr GetContext() const { return m_pContext; }
    [[nodiscard]] bool IsFinished() const { return m_bFinished; }
    [[nodiscard]] CGrabContext::eGrabPhase GetPhase() const;

    void RequestAction(CGrabContext::eGrabAction action);
    void ReleaseVictim();

private:
    CPed* FindValidVictim(CPed* grabber, float* outDistance = nullptr);
    bool InitializeGrab(CPed* grabber);
    void AssignVictimTask();

    CTask* CreateReachTask();
    CTask* CreateHoldTask();

    void Cleanup();

    [[nodiscard]] bool IsAttackPressed() const;
};