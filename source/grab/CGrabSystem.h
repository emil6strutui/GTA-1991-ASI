#pragma once

// ============================================================================
// CGrabSystem - VCS-style grab system
//
// Detects R key press and initiates grab for the player.
// Coordinates CTaskSimpleGrab (player) and CTaskSimpleGrabbed (victim).
// ============================================================================

namespace CGrabSystem
{
    // Install hooks and event handlers
    void InstallHooks();

    // Configuration
    constexpr char GRAB_KEY = 'R';  // Key to initiate grab
}
