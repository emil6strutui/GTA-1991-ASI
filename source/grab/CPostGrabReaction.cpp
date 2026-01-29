#include "CPostGrabReaction.h"

#include <plugin.h>
#include <CTimer.h>
#include <CPedIntelligence.h>
#include <CEventGroup.h>
#include <cstdint>

namespace CPostGrabReaction
{
    // Using plugin-sdk's CEventDamage if available, otherwise manual approach
    // This implementation uses direct function calls to avoid version-specific struct layouts

    // Function signatures from the game
    // CEventDamage::CEventDamage(CEntity*, uint32, eWeaponType, ePedPieceTypes, uint8, bool, bool)
    using FnEventDamageConstructor = void*(__thiscall*)(
        void* thisPtr,
        CEntity* source,
        uint32_t startTime,
        int32_t weaponType,
        int32_t pieceHit,
        uint8_t direction,
        bool bJumpedOutOfCar,
        bool bPedInVehicle
    );
    
    // CEventDamage::AffectsPed(CPed*)
    using FnEventDamageAffectsPed = bool(__thiscall*)(void* thisPtr, CPed* ped);
    
    // CEventGroup::Add(CEvent*, bool)
    using FnEventGroupAdd = void*(__thiscall*)(CEventGroup* thisPtr, void* event, bool bValid);
    
    // CEntity::CleanUpOldReference(CEntity**)
    using FnCleanUpOldReference = void(__cdecl*)(CEntity** ppEntity);

    // Game function addresses (1.0 US)
    static auto EventDamage_Ctor = reinterpret_cast<FnEventDamageConstructor>(0x4AD830);
    static auto EventDamage_AffectsPed = reinterpret_cast<FnEventDamageAffectsPed>(0x4B35A0);
    static auto EventGroup_Add = reinterpret_cast<FnEventGroupAdd>(0x4AB420);

    // CEventDamage size is 0x44 bytes
    static constexpr size_t EVENT_DAMAGE_SIZE = 0x44;
    // m_pSourceEntity offset within CEventDamage
    static constexpr size_t SOURCE_ENTITY_OFFSET = 0x14;

    void TriggerReaction(CPed* victim, CPed* attacker)
    {
        if (!victim || !attacker) {
            return;
        }
        
        if (!victim->m_pIntelligence) {
            return;
        }
        
        if (victim->m_fHealth <= 0.0f) {
            return;
        }

        // Allocate CEventDamage on stack with proper alignment
        alignas(8) uint8_t eventBuffer[EVENT_DAMAGE_SIZE];
       
        EventDamage_Ctor(
            eventBuffer,
            reinterpret_cast<CEntity*>(attacker),
            CTimer::m_snTimeInMilliseconds,
            WEAPONTYPE_UNARMED,
            PED_PIECE_TORSO,     
            0,      // direction (front)
            false,  // jumped out of moving car
            victim->bInVehicle
        );
        
        // Check if the event affects the victim
        bool affects = EventDamage_AffectsPed(eventBuffer, victim);
        
        if (affects) {
            // CEventGroup::Add clones the event internally, so stack allocation is fine
            EventGroup_Add(&victim->m_pIntelligence->m_eventGroup, eventBuffer, false);
        }
        
        // Manually clean up the source entity reference (what the destructor does)
        // m_pSourceEntity is at offset 0x14 in CEventDamage
        CEntity** ppSourceEntity = reinterpret_cast<CEntity**>(eventBuffer + SOURCE_ENTITY_OFFSET);
        if (*ppSourceEntity) {
            (*ppSourceEntity)->CleanUpOldReference(ppSourceEntity);
        }
    }
}
