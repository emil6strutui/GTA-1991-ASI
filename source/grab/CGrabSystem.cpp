#include "CGrabSystem.h"
#include "CTaskComplexGrab.h"
#include "GrabContext.h"

#include <plugin.h>
#include <CPad.h>
#include <CPlayerPed.h>
#include <CWorld.h>

using namespace plugin;

namespace CGrabSystem
{
    static CPlayerPed* GetPlayer() {
        return FindPlayerPed(0);
    }

    static bool IsGrabKeyJustPressed() {
        bool currentlyPressed = CPad::NewKeyState.standardKeys[GRAB_KEY] != 0;
        bool previouslyPressed = CPad::OldKeyState.standardKeys[GRAB_KEY] != 0;
        return currentlyPressed && !previouslyPressed;
    }

    static bool CanPlayerGrab(CPlayerPed* player)
    {
        if (!player) {
            return false;
        }

        if (player->m_fHealth <= 0.0f) {
            return false;
        }

        if (player->m_pVehicle) {
            return false;
        }

        auto pedState = static_cast<unsigned int>(player->m_ePedState);
        if (pedState >= PEDSTATE_DEAD && pedState <= PEDSTATE_ARRESTED) {
            return false;
        }

        if (player->m_nMoveState == PEDMOVE_SPRINT) {
            return false;
        }

        return true;
    }

    static void StartGrab(CPlayerPed* player)
    {
        if (!player || !player->m_pIntelligence) {
            return;
        }

        auto* grabTask = new CTaskComplexGrab();
        CTaskManager* taskMgr = &player->m_pIntelligence->m_TaskMgr;
        taskMgr->SetTask(reinterpret_cast<CTask*>(grabTask), TASK_PRIMARY_PHYSICAL_RESPONSE, false);
    }

    static CTaskComplexGrab* GetPlayerGrabTask(CPlayerPed* player)
    {
        if (!player || !player->m_pIntelligence) {
            return nullptr;
        }

        CTaskManager* taskMgr = &player->m_pIntelligence->m_TaskMgr;
        CTask* task = taskMgr->m_aPrimaryTasks[TASK_PRIMARY_PHYSICAL_RESPONSE];
        
        if (task && task->GetId() == CTaskComplexGrab::Type) {
            return reinterpret_cast<CTaskComplexGrab*>(task);
        }
        return nullptr;
    }

    static void OnGameProcess()
    {
        if (!IsGrabKeyJustPressed()) {
            return;
        }

        CPlayerPed* player = GetPlayer();
        if (!player) {
            return;
        }

        // If we already have a grab in progress, release it
        if (auto* existingGrab = GetPlayerGrabTask(player)) {
            auto phase = existingGrab->GetPhase();
            if (phase == CGrabContext::eGrabPhase::HOLDING || 
                phase == CGrabContext::eGrabPhase::ACTION ||
                phase == CGrabContext::eGrabPhase::REACHING) {
                existingGrab->ReleaseVictim();
                return;
            }
        }

        if (!CanPlayerGrab(player)) {
            return;
        }

        StartGrab(player);
    }

    void InstallHooks() {
        Events::gameProcessEvent += OnGameProcess;
    }
}
