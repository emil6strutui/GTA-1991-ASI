#pragma once
#include <CPed.h>

namespace CGrabSystem
{
    void StartDelayedDebug(CPed* ped, int frames);
    void InstallHooks();
    constexpr char GRAB_KEY = 'R';
}
