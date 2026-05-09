#pragma once
#include <CPed.h>

namespace CGrabSystem
{
    void StartDelayedDebug(CPed* ped, int frames);
    void RegisterEscapedVictim(CPed* ped);
    bool HasEscapedVictim(CPed* ped);
    void InstallHooks();
    constexpr char GRAB_KEY = 'R';
}
