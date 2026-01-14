#pragma once

#include <plugin.h>
#include <CTask.h>
#include <CTaskSimple.h>

// ============================================================================
// CUSTOM GRAB TASK
// ============================================================================
// Purpose: Hold player in grab state without:
// - Blending idle animation (like CTaskSimpleStandStill does)
// - Blocking camera rotation
// - Allowing punch/fight actions
//
// This task simply occupies TASK_PRIMARY_PRIMARY to prevent
// CTaskSimplePlayerOnFoot from processing fighting/movement.
// ============================================================================

class CTaskSimpleGrab : public CTaskSimple {
public:
    // Constructor - must use plugin::dummy_func_t pattern
    CTaskSimpleGrab() : CTaskSimple(plugin::dummy) {}
    
    // Clone for task system
    CTask* Clone() override { 
        return new CTaskSimpleGrab(); 
    }
    
    // Return nullptr - simple tasks have no subtasks
    CTask* GetSubTask() override {
        return nullptr;
    }
    
    // We are a simple task
    bool IsSimple() override {
        return true;
    }
    
    // Return our task type ID
    eTaskType GetId() override { 
        return TASK_SIMPLE_STAND_STILL; 
    }
    
    // MUST override StopTimer to prevent VMT recursion crash!
    // This is called when a task timer needs to stop
    void StopTimer(CEvent* event) override {
        // Do nothing - we don't use timers
    }
    
    // Called every frame - just return false to keep task alive
    // We do NOT blend any animation here (unlike CTaskSimpleStandStill)
    bool ProcessPed(CPed* ped) override {
        // Do nothing - just keep the task alive
        // This prevents PlayerOnFoot from processing attacks
        // But doesn't override our grab animation
        return false;  // false = task continues
    }
    
    // Must override to prevent infinite recursion in plugin-sdk VMT
    bool SetPedPosition(CPed* ped) override {
        return false;  // Don't modify ped position
    }
    
    // Allow abort when needed
    bool MakeAbortable(CPed* ped, eAbortPriority priority, CEvent* event) override {
        return true;  // Always allow abort
    }
};
