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

    static int s_debugCountdown = -1;
    static CPed* s_debugPed = nullptr;

    void StartDelayedDebug(CPed* ped, int frames = 60) {
        s_debugPed = ped;
        s_debugCountdown = frames;
    }

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

        if (s_debugCountdown > 0) {
            s_debugCountdown--;
        }
        else if (s_debugCountdown == 0) {
            s_debugCountdown = -1;
            if (s_debugPed && s_debugPed->m_pIntelligence) {
                CTaskManager* taskMgr = &s_debugPed->m_pIntelligence->m_TaskMgr;

                // Debug: print task types
                char buf[512];
                for (int i = 0; i < 5; i++) {
                    CTask* task = taskMgr->m_aPrimaryTasks[i];
                    if (task) {
                        sprintf(buf, "Slot[%d]: ptr=%p type=%d\n", i, task, task->GetId());
                        OutputDebugStringA(buf);
                    }
                }

                if (!s_debugPed || !s_debugPed->m_pRwClump) return;

                OutputDebugStringA("=== Current Animations ===\n");
                CAnimBlendAssociation* assoc = RpAnimBlendClumpGetFirstAssociation(s_debugPed->m_pRwClump);
                int count = 0;

                while (assoc) {
                    char buf[256];
                    sprintf(buf, "[%d] Blend: %.2f Delta: %.2f Flags: 0x%X\n",
                        count, assoc->m_fBlendAmount, assoc->m_fBlendDelta, assoc->m_nFlags);
                    OutputDebugStringA(buf);
                    assoc = RpAnimBlendGetNextAssociation(assoc);
                    count++;
                }

                if (count == 0) {
                    OutputDebugStringA("NO ANIMATIONS!\n");
                }
            }
        }

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
