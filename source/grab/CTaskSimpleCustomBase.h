#pragma once
#include <CTaskCustomBase.h>


class CTaskSimpleCustomBase : public CTaskCustomBase {
public:
    // VMT index 2
    CTask* GetSubTask() override { return nullptr; }
    // VMT index 3
    bool IsSimple() override { return true; }

    // VMT index 7
    virtual bool ProcessPed(CPed* ped) = 0;
    // VMT index 8
    virtual bool SetPedPosition(CPed* ped) { return false; }
};