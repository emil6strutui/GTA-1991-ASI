#pragma once
#include <plugin.h>

class CEvent;

// Minimal task base that uses real C++ virtuals, not Plugin-SDK wrappers
class CTaskCustomBase {
public:
    void* operator new(unsigned int size) {
        typedef void* (__cdecl* AllocFunc)(unsigned int);
        AllocFunc gameAlloc = (AllocFunc)0x61A5A0;
        return gameAlloc(size);
    }

    void operator delete(void* object) {
        typedef void(__cdecl* FreeFunc)(void*);
        FreeFunc gameFree = (FreeFunc)0x61A5B0;
        gameFree(object);
    }

    CTask* m_pParentTask = nullptr;

    virtual ~CTaskCustomBase() = default;
    virtual CTask* Clone() = 0;
    virtual CTask* GetSubTask() = 0;
    virtual bool IsSimple() = 0;
    virtual eTaskType GetId() = 0;
    virtual void StopTimer(CEvent*) {}
    virtual bool MakeAbortable(CPed* ped, eAbortPriority priority, CEvent* event) = 0;
};