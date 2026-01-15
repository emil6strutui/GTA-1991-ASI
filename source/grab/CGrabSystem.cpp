#include "CGrabSystem.h"
#include "CTaskSimpleGrab.h"

#include <plugin.h>
#include <CPad.h>
#include <CPlayerPed.h>
#include <CWorld.h>

using namespace plugin;

namespace CGrabSystem
{
    // ========================================================================
    // Helper: Find the player ped
    // ========================================================================
    static CPlayerPed* GetPlayer()
    {
        return FindPlayerPed(0);
    }

    // ========================================================================
    // Helper: Check if grab key was just pressed (this frame only)
    // ========================================================================
    static bool IsGrabKeyJustPressed()
    {
        // Get keyboard state
        bool currentlyPressed = CPad::NewKeyState.standardKeys[GRAB_KEY] != 0;
        bool previouslyPressed = CPad::OldKeyState.standardKeys[GRAB_KEY] != 0;

        // Just pressed = pressed now but not before
        return currentlyPressed && !previouslyPressed;
    }

    // ========================================================================
    // Helper: Check if player can initiate grab
    // ========================================================================
    static bool CanPlayerGrab(CPlayerPed* player)
    {
        if (!player)
        {
            return false;
        }

        // Player must be alive
        if (player->m_fHealth <= 0.0f)
        {
            return false;
        }

        // Player must not be in a vehicle
        if (player->m_pVehicle)
        {
            return false;
        }

        // Player must be on foot (check ped state)
        // PED_STATES: 0=IDLE, 1=LOOK_ENTITY, 2=LOOK_HEADING, etc.
        // We'll allow grab from most normal states
        unsigned int pedState = (unsigned int)player->m_ePedState;
        
        // Disallow grab during certain states
        // PEDSTATE_DEAD = 55, PEDSTATE_DIE = 54, PEDSTATE_ARRESTED = 53
        // PEDSTATE_DRIVING = 50, PEDSTATE_IN_CAR = 51
        if (pedState >= 50 && pedState <= 55)
        {
            return false;
        }

        // Check if player already has a grab task running
        // We could check the task manager here, but for simplicity
        // we'll rely on the task system to handle conflicts

        return true;
    }

    // ========================================================================
    // Helper: Start grab for player
    // ========================================================================
    static void StartGrab(CPlayerPed* player)
    {
        if (!player || !player->m_pIntelligence)
        {
            return;
        }

        // Create the grab task
        CTaskSimpleGrab* grabTask = new CTaskSimpleGrab();

        // Assign to SECONDARY_ATTACK slot (same as fight task)
        CTaskManager* taskMgr = &player->m_pIntelligence->m_TaskMgr;
        taskMgr->SetTaskSecondary((CTask*)grabTask, TASK_SECONDARY_ATTACK);
    }

    // ========================================================================
    // Game process event handler - called every frame
    // ========================================================================
    static void OnGameProcess()
    {
        // Check for grab key
        if (!IsGrabKeyJustPressed())
        {
            return;
        }

        // Get player
        CPlayerPed* player = GetPlayer();
        if (!player)
        {
            return;
        }

        // Check if player can grab
        if (!CanPlayerGrab(player))
        {
            return;
        }

        // Start grab
        StartGrab(player);
    }

    // ========================================================================
    // Install hooks
    // ========================================================================
    void InstallHooks()
    {
        // Register our process handler
        Events::gameProcessEvent += OnGameProcess;
    }
}
