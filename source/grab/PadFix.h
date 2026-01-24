#pragma once
#include <CPad.h>
#include <PluginBase.h>

// Correct CPad function wrappers (Plugin-SDK has wrong signatures)
namespace PadFix
{
    // 0x540390 - uint8 CPad::MeleeAttackJustDown(bool bCheckButtonCircleStateOnly) const
    inline unsigned char MeleeAttackJustDown(CPad* pad, bool checkButtonCircleOnly = false) {
        return plugin::CallMethodAndReturn<unsigned char, 0x540390, CPad*, bool>(pad, checkButtonCircleOnly);
    }

    // 0x540340 - uint8 CPad::GetMeleeAttack(bool bCheckButtonCircleStateOnly) const  
    inline unsigned char GetMeleeAttack(CPad* pad, bool checkButtonCircleOnly = false) {
        return plugin::CallMethodAndReturn<unsigned char, 0x540340, CPad*, bool>(pad, checkButtonCircleOnly);
    }
}