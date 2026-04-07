#include "CPostGrabReaction.h"

#include <plugin.h>
#include <CTimer.h>
#include <CPedIntelligence.h>
#include <CEventGroup.h>
#include <CAnimManager.h>
#include <CPedDamageResponse.h>
#include <CPedDamageResponseCalculator.h>
#include <cstdint>

namespace CGrabSystem {
    void StartDelayedDebug(CPed* ped, int frames);
}

namespace CPostGrabReaction
{
    namespace {
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

        using FnEventDamageAffectsPed = bool(__thiscall*)(void* thisPtr, CPed* ped);
        using FnEventGroupAdd = void*(__thiscall*)(CEventGroup* thisPtr, void* event, bool bValid);

        static auto EventDamage_Ctor = reinterpret_cast<FnEventDamageConstructor>(0x4AD830);
        static auto EventDamage_AffectsPed = reinterpret_cast<FnEventDamageAffectsPed>(0x4B35A0);
        static auto EventGroup_Add = reinterpret_cast<FnEventGroupAdd>(0x4AB420);

        static constexpr size_t EVENT_DAMAGE_SIZE = 0x44;
        static constexpr size_t SOURCE_ENTITY_OFFSET = 0x14;
        static constexpr size_t DAMAGE_RESPONSE_OFFSET = 0x38;
        static auto ReportCrime = reinterpret_cast<void(__cdecl*)(uint32_t, CEntity*, CPed*)>(0x532010);

        void CleanUpEventDamageSourceRef(uint8_t* eventBuffer) {
            CEntity** ppSourceEntity = reinterpret_cast<CEntity**>(eventBuffer + SOURCE_ENTITY_OFFSET);
            if (*ppSourceEntity) {
                (*ppSourceEntity)->CleanUpOldReference(ppSourceEntity);
            }
        }
    }

    bool ApplyDamageWithoutReaction(CPed* victim, CPed* attacker, float damage, uint8_t bodyPart, bool bSpeak) {
        if (!victim || !attacker || victim->m_fHealth <= 0.0f) {
            return false;
        }

        alignas(8) uint8_t eventBuffer[EVENT_DAMAGE_SIZE];

        EventDamage_Ctor(
            eventBuffer,
            reinterpret_cast<CEntity*>(attacker),
            CTimer::m_snTimeInMilliseconds,
            WEAPONTYPE_UNARMED,
            bodyPart,
            0,
            false,
            victim->bInVehicle
        );

        if (!EventDamage_AffectsPed(eventBuffer, victim)) {
            CleanUpEventDamageSourceRef(eventBuffer);
            return false;
        }

        auto* response = reinterpret_cast<CPedDamageResponse*>(eventBuffer + DAMAGE_RESPONSE_OFFSET);
        CPedDamageResponseCalculator damageCalc(
            reinterpret_cast<CEntity*>(attacker),
            damage,
            WEAPONTYPE_UNARMED,
            bodyPart,
            bSpeak
        );
        damageCalc.ComputeDamageResponse(victim, *response, bSpeak);

        ReportCrime(/*CRIME_DAMAGED_PED*/ 2, reinterpret_cast<CEntity*>(victim), attacker);
        CleanUpEventDamageSourceRef(eventBuffer);
        return true;
    }

    bool QueueDamageEvent(CPed* victim, CPed* attacker, float damage, uint8_t bodyPart, bool bSpeak) {
        if (!victim || !attacker || !victim->m_pIntelligence || victim->m_fHealth <= 0.0f) {
            return false;
        }

        alignas(8) uint8_t eventBuffer[EVENT_DAMAGE_SIZE];

        EventDamage_Ctor(
            eventBuffer,
            reinterpret_cast<CEntity*>(attacker),
            CTimer::m_snTimeInMilliseconds,
            WEAPONTYPE_UNARMED,
            bodyPart,
            0,
            false,
            victim->bInVehicle
        );

        if (!EventDamage_AffectsPed(eventBuffer, victim)) {
            CleanUpEventDamageSourceRef(eventBuffer);
            return false;
        }

        if (damage > 0.0f) {
            auto* response = reinterpret_cast<CPedDamageResponse*>(eventBuffer + DAMAGE_RESPONSE_OFFSET);
            CPedDamageResponseCalculator damageCalc(
                reinterpret_cast<CEntity*>(attacker),
                damage,
                WEAPONTYPE_UNARMED,
                bodyPart,
                bSpeak
            );
            damageCalc.ComputeDamageResponse(victim, *response, bSpeak);
        }

        EventGroup_Add(&victim->m_pIntelligence->m_eventGroup, eventBuffer, false);
        CleanUpEventDamageSourceRef(eventBuffer);
        return true;
    }

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

        CGrabSystem::StartDelayedDebug(victim, 60);

        CTaskManager* taskMgr = &victim->m_pIntelligence->m_TaskMgr;
        CTask* fleeTask = taskMgr->m_aPrimaryTasks[TASK_PRIMARY_EVENT_RESPONSE_NONTEMP];
        if (fleeTask) {
            fleeTask->MakeAbortable(victim, ABORT_PRIORITY_IMMEDIATE, nullptr);
        }
        CTask* tempTask = taskMgr->m_aPrimaryTasks[TASK_PRIMARY_EVENT_RESPONSE_TEMP];
        if (tempTask) {
            tempTask->MakeAbortable(victim, ABORT_PRIORITY_IMMEDIATE, nullptr);
        }

        if (victim->m_pRwClump) {
            CAnimManager::BlendAnimation(
                victim->m_pRwClump,
                ANIM_GROUP_DEFAULT,
                ANIM_DEFAULT_IDLE_STANCE,
                1000.0f  // instant
            );
        }

        QueueDamageEvent(victim, attacker, 0.0f, PED_PIECE_TORSO, false);
    }
}
