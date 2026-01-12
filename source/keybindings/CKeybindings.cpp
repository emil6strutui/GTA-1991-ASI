#include "CKeybindings.h"
#include "CControllerConfigManager.h"
#include "CMenuManager.h"
#include <plugin.h>

using namespace plugin;

extern CMenuManager& FrontEndMenuManager;

namespace CKeybindings
{

    // ============================================================================
    // FUNCTION POINTERS
    // ============================================================================

    // SetControllerKeyAssociatedWithAction - sets keyboard keys
    using SetKeyFn = void(__thiscall *)(void *, int, int, int);
    static SetKeyFn SetKey = (SetKeyFn)0x530490;

    // SetMouseButtonAssociatedWithAction - sets mouse buttons
    using SetMouseFn = void(__thiscall *)(void *, int, int);
    static SetMouseFn SetMouseButton = (SetMouseFn)0x52F590;

    constexpr uintptr_t OFFSET_STICK_BOOL_ARRAYS = 0x12D0;
    constexpr uintptr_t OFFSET_MOUSE_FOUND_INIT_SET = 0x12E0; // Right after the 16 stick bools

    // ============================================================================
    // InitDefaultControlConfiguration REPLACEMENT
    // ============================================================================

    bool *__fastcall InitControlConfiguration_Hook(void *pThis, void *edx)
    {

        // ====================================================================
        // MOUSE BUTTONS - SET FIRST (gets slot 1 priority)
        // ====================================================================

        // Set m_MouseFoundInitSet = true (like original does when mouse is detected)
        *(bool *)((uintptr_t)pThis + OFFSET_MOUSE_FOUND_INIT_SET) = true;

        // ON FOOT - Mouse controls
        SetMouseButton(pThis, PED_FIREWEAPON, rsMOUSELEFTBUTTON);
        SetMouseButton(pThis, PED_LOCK_TARGET, rsMOUSERIGHTBUTTON);
        SetMouseButton(pThis, PED_LOOKBEHIND, rsMOUSEMIDDLEBUTTON);
        SetMouseButton(pThis, PED_CYCLE_WEAPON_RIGHT, rsMOUSEWHEELUPBUTTON);
        SetMouseButton(pThis, PED_CYCLE_WEAPON_LEFT, rsMOUSEWHEELDOWNBUTTON);
        SetMouseButton(pThis, PED_SNIPER_ZOOM_IN, rsMOUSEWHEELUPBUTTON);
        SetMouseButton(pThis, PED_SNIPER_ZOOM_OUT, rsMOUSEWHEELDOWNBUTTON);

        // VEHICLE - Mouse controls
        SetMouseButton(pThis, VEHICLE_FIREWEAPON, rsMOUSELEFTBUTTON);
        SetMouseButton(pThis, VEHICLE_MOUSELOOK, rsMOUSERIGHTBUTTON);
        SetMouseButton(pThis, VEHICLE_LOOKBEHIND, rsMOUSEMIDDLEBUTTON);
        SetMouseButton(pThis, VEHICLE_RADIO_STATION_UP, rsMOUSEWHEELUPBUTTON);
        SetMouseButton(pThis, VEHICLE_RADIO_STATION_DOWN, rsMOUSEWHEELDOWNBUTTON);

        // ====================================================================
        // KEYBOARD KEYS - SET AFTER MOUSE (gets slot 2+ priority)
        // ====================================================================

        // ON FOOT - MOVEMENT (WASD primary, Arrows secondary)
        SetKey(pThis, GO_FORWARD, 'W', 0);
        SetKey(pThis, GO_FORWARD, rsUP, 1);
        SetKey(pThis, GO_BACK, 'S', 0);
        SetKey(pThis, GO_BACK, rsDOWN, 1);
        SetKey(pThis, GO_LEFT, 'A', 0);
        SetKey(pThis, GO_LEFT, rsLEFT, 1);
        SetKey(pThis, GO_RIGHT, 'D', 0);
        SetKey(pThis, GO_RIGHT, rsRIGHT, 1);

        // ON FOOT - ACTIONS
        SetKey(pThis, PED_FIREWEAPON, rsPADINS + '1', 0);
        SetKey(pThis, PED_FIREWEAPON, rsLCTRL, 1);
        SetKey(pThis, PED_LOOKBEHIND, rsPADEND, 0);
        SetKey(pThis, PED_CYCLE_WEAPON_RIGHT, 'E', 0);
        SetKey(pThis, PED_CYCLE_WEAPON_LEFT, 'Q', 0);
        SetKey(pThis, PED_SNIPER_ZOOM_IN, rsPGUP, 0);
        SetKey(pThis, PED_SNIPER_ZOOM_OUT, rsPGDN, 0);
        SetKey(pThis, VEHICLE_ENTER_EXIT, 'F', 0);
        SetKey(pThis, VEHICLE_ENTER_EXIT, rsENTER, 1);
        SetKey(pThis, CAMERA_CHANGE_VIEW_ALL_SITUATIONS, 'V', 0);
        SetKey(pThis, PED_JUMPING, rsLSHIFT, 0);
        SetKey(pThis, PED_SPRINT, ' ', 0);
        SetKey(pThis, PED_DUCK, 'C', 0);
        SetKey(pThis, PED_ANSWER_PHONE, rsTAB, 0);
        SetKey(pThis, SNEAK_ABOUT, rsLALT, 0);

        // ON FOOT - TARGETING & LOOK
        SetKey(pThis, PED_1RST_PERSON_LOOK_LEFT, rsPADLEFT, 0);
        SetKey(pThis, PED_1RST_PERSON_LOOK_RIGHT, rsPADRIGHT, 0);
        SetKey(pThis, PED_1RST_PERSON_LOOK_UP, rsPADUP, 0);
        SetKey(pThis, PED_1RST_PERSON_LOOK_DOWN, rsPADDOWN, 0);
        SetKey(pThis, PED_CYCLE_TARGET_LEFT, '[', 0);
        SetKey(pThis, PED_CYCLE_TARGET_RIGHT, ']', 0);
        SetKey(pThis, PED_CENTER_CAMERA_BEHIND_PLAYER, '#', 0);

        // VEHICLE - STEERING
        SetKey(pThis, VEHICLE_STEERLEFT, 'A', 0);
        SetKey(pThis, VEHICLE_STEERLEFT, rsLEFT, 1);
        SetKey(pThis, VEHICLE_STEERRIGHT, 'D', 0);
        SetKey(pThis, VEHICLE_STEERRIGHT, rsRIGHT, 1);
        SetKey(pThis, VEHICLE_ACCELERATE, 'W', 0);
        SetKey(pThis, VEHICLE_BRAKE, 'S', 0);
        SetKey(pThis, VEHICLE_HANDBRAKE, ' ', 0);

        // VEHICLE - AIRCRAFT
        SetKey(pThis, VEHICLE_STEERUP, rsUP, 0);
        SetKey(pThis, VEHICLE_STEERDOWN, rsDOWN, 0);

        // VEHICLE - WEAPONS & CAMERA
        SetKey(pThis, VEHICLE_FIREWEAPON, rsLCTRL, 0);
        SetKey(pThis, VEHICLE_FIREWEAPON_ALT, rsRCTRL, 0);
        SetKey(pThis, VEHICLE_LOOKLEFT, 'Q', 0);
        SetKey(pThis, VEHICLE_LOOKRIGHT, 'E', 0);

        // VEHICLE - TURRET
        SetKey(pThis, VEHICLE_TURRETLEFT, rsPADLEFT, 0);
        SetKey(pThis, VEHICLE_TURRETRIGHT, rsPADRIGHT, 0);
        SetKey(pThis, VEHICLE_TURRETUP, rsPADUP, 0);
        SetKey(pThis, VEHICLE_TURRETDOWN, rsPADDOWN, 0);

        // VEHICLE - MISC
        SetKey(pThis, VEHICLE_RADIO_STATION_UP, '4', 0);
        SetKey(pThis, VEHICLE_RADIO_STATION_DOWN, '5', 0);
        // USER TRACK SKIP
        SetKey(pThis, 28, '6', 0);
        SetKey(pThis, VEHICLE_HORN, 'H', 0);
        SetKey(pThis, TOGGLE_SUBMISSIONS, '2', 0);

        // MISC
        SetKey(pThis, CONVERSATION_YES, 'Y', 0);
        SetKey(pThis, CONVERSATION_NO, 'N', 0);
        SetKey(pThis, GROUP_CONTROL_FWD, 'G', 0);
        SetKey(pThis, GROUP_CONTROL_BWD, 'H', 0);

        // CLEAR STICK BOOL ARRAYS (required)
        memset((void *)((uintptr_t)pThis + OFFSET_STICK_BOOL_ARRAYS), 0, 16);

        return (bool *)((uintptr_t)pThis + OFFSET_STICK_BOOL_ARRAYS + 4);
    }

    // ============================================================================
    // INSTALL
    // ============================================================================

    void InstallPatches()
    {
        //Don't write to the mouse invert Y default value
        patch::Nop(0x573C00, 5);
        //Don't write to the subtitles default value
        patch::Nop(0x573BF1, 3);
        //Don't write to the widescreen default value
        patch::Nop(0x573BD6, 3);

        CMenuManager::bInvertMouseY = true; // true = OFF
        FrontEndMenuManager.m_bPrefsShowSubtitles = true;
        FrontEndMenuManager.m_bPrefsUseWideScreen = true;

        // Hook InitDefaultControlConfiguration to set mouse first, then keyboard
        injector::MakeJMP(0x530640, InitControlConfiguration_Hook, true);

        // NOP out InitDefaultControlConfigMouse (0x52F6F0) - we handle mouse in our hook
        // Write "ret 8" (0xC2 0x08 0x00) to make it return immediately
        // __thiscall with 2 stack params: const CMouseControllerState& (4) + bool (4) = 8 bytes
        injector::WriteMemory<uint8_t>(0x52F6F0, 0xC2, true);    // ret
        injector::WriteMemory<uint16_t>(0x52F6F1, 0x0008, true); // 8
    }

}