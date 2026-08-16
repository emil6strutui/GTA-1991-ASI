#pragma once
#include <plugin.h>
#include <CTaskCustomBase.h>


class CTaskComplexCustomBase : public CTaskCustomBase {
public:
    CTask* m_pSubTask = nullptr;

    ~CTaskComplexCustomBase() override {
        delete m_pSubTask;
        m_pSubTask = nullptr;
    }

    CTask* GetSubTask() override { return m_pSubTask; }
    bool IsSimple() override { return false; }

    virtual void SetSubTask(CTask* subTask) {
        if (m_pSubTask == subTask) return;
        delete m_pSubTask;
        m_pSubTask = subTask;
        if (subTask) {
            subTask->m_pParentTask = reinterpret_cast<CTask*>(this);
        }
    }

    virtual CTask* CreateNextSubTask(CPed* ped) = 0;
    virtual CTask* CreateFirstSubTask(CPed* ped) = 0;
    virtual CTask* ControlSubTask(CPed* ped) = 0;
};