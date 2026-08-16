#include "CPostGrabReaction.h"

#include <plugin.h>
#include <CTimer.h>
#include <CPedIntelligence.h>
#include <CEventGroup.h>
#include <CAnimManager.h>
#include <CPedDamageResponse.h>
#include <CPedDamageResponseCalculator.h>
#include <CVector.h>
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
        using FnEventAcquaintancePedHateConstructor = void*(__thiscall*)(void* thisPtr, CPed* ped);

        static auto EventDamage_Ctor = reinterpret_cast<FnEventDamageConstructor>(0x4AD830);
        static auto EventDamage_AffectsPed = reinterpret_cast<FnEventDamageAffectsPed>(0x4B35A0);
        static auto EventGroup_Add = reinterpret_cast<FnEventGroupAdd>(0x4AB420);
        static auto EventAcquaintancePedHate_Ctor = reinterpret_cast<FnEventAcquaintancePedHateConstructor>(0x420E70);

        static constexpr size_t EVENT_DAMAGE_SIZE = 0x44;
        static constexpr size_t EVENT_ACQUAINTANCE_PED_SIZE = 0x18;
        static constexpr size_t SOURCE_ENTITY_OFFSET = 0x14;
        static constexpr size_t ACQUAINTANCE_PED_OFFSET = 0x14;
        static constexpr size_t EDITABLE_RESPONSE_TASK_ID_OFFSET = 0x0E;
        static constexpr size_t DAMAGE_FLAGS_OFFSET = 0x25;
        static constexpr size_t DAMAGE_RESPONSE_OFFSET = 0x38;
        static constexpr uint8_t DAMAGE_FLAG_FALL_DOWN = 1u << 1;
        static constexpr uint8_t RELEASE_FALL_BACK_DIRECTION = 2;
        static constexpr float RELEASE_PUSH_DAMAGE = 3.0f;
        static constexpr float RELEASE_PUSH_FORCE = 4.0f;
        static constexpr float RELEASE_PUSH_UP_FORCE = 1.5f;
        static auto ReportCrime = reinterpret_cast<void(__cdecl*)(uint32_t, CEntity*, CPed*)>(0x532010);

        void EnsureBaseAnimation(CPed* ped) {
            if (!ped || !ped->m_pRwClump) {
                return;
            }

            CAnimManager::BlendAnimation(
                ped->m_pRwClump,
                ped->m_nAnimGroup,
                ANIM_DEFAULT_IDLE_STANCE,
                1000.0f
            );
        }

        void CleanUpEventDamageSourceRef(uint8_t* eventBuffer) {
            CEntity** ppSourceEntity = reinterpret_cast<CEntity**>(eventBuffer + SOURCE_ENTITY_OFFSET);
            if (*ppSourceEntity) {
                (*ppSourceEntity)->CleanUpOldReference(ppSourceEntity);
            }
        }

        void CleanUpAcquaintancePedRef(uint8_t* eventBuffer) {
            CPed** ppPed = reinterpret_cast<CPed**>(eventBuffer + ACQUAINTANCE_PED_OFFSET);
            if (*ppPed) {
                (*ppPed)->CleanUpOldReference(reinterpret_cast<CEntity**>(ppPed));
            }
        }

        void ClearCurrentEventResponses(CPed* ped) {
            if (!ped || !ped->m_pIntelligence) {
                return;
            }

            CTaskManager* taskMgr = &ped->m_pIntelligence->m_TaskMgr;
            CTask* nonTempTask = taskMgr->m_aPrimaryTasks[TASK_PRIMARY_EVENT_RESPONSE_NONTEMP];
            if (nonTempTask) {
                nonTempTask->MakeAbortable(ped, ABORT_PRIORITY_IMMEDIATE, nullptr);
            }

            CTask* tempTask = taskMgr->m_aPrimaryTasks[TASK_PRIMARY_EVENT_RESPONSE_TEMP];
            if (tempTask) {
                tempTask->MakeAbortable(ped, ABORT_PRIORITY_IMMEDIATE, nullptr);
            }
        }

        eTaskType ChooseEscapeThreatTask() {
            return TASK_COMPLEX_KILL_PED_ON_FOOT;
        }

        void ApplyReleasePushForce(CPed* victim, CPed* attacker) {
            CVector force = victim->GetPosition() - attacker->GetPosition();
            force.z = 0.0f;

            if (force.MagnitudeSqr2D() <= 0.0001f) {
                force = -victim->GetForward();
                force.z = 0.0f;
            }

            force.Normalize();
            force *= RELEASE_PUSH_FORCE;
            force.z = RELEASE_PUSH_UP_FORCE;

            victim->bWasStanding = false;
            victim->bIsStanding = false;
            victim->ApplyMoveForce(force);
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
        return QueueDamageEvent(victim, attacker, damage, bodyPart, 0, false, bSpeak);
    }

    bool QueueDamageEvent(CPed* victim, CPed* attacker, float damage, uint8_t bodyPart, uint8_t direction, bool forceFallDown, bool bSpeak) {
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
            direction,
            false,
            victim->bInVehicle
        );

        if (forceFallDown) {
            eventBuffer[DAMAGE_FLAGS_OFFSET] |= DAMAGE_FLAG_FALL_DOWN;
        }

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

    bool QueueZeroDamageMeleeChestEvent(CPed* victim, CPed* attacker, uint8_t direction, bool bSpeak) {
        (void)bSpeak;

        if (!victim || !attacker || !victim->m_pIntelligence || victim->m_fHealth <= 0.0f) {
            return false;
        }

        alignas(8) uint8_t eventBuffer[EVENT_DAMAGE_SIZE];

        EventDamage_Ctor(
            eventBuffer,
            reinterpret_cast<CEntity*>(attacker),
            CTimer::m_snTimeInMilliseconds,
            WEAPONTYPE_UNARMED,
            kPedPieceTorso,
            direction,
            false,
            victim->bInVehicle
        );

        if (!EventDamage_AffectsPed(eventBuffer, victim)) {
            CleanUpEventDamageSourceRef(eventBuffer);
            return false;
        }

        auto* response = reinterpret_cast<CPedDamageResponse*>(eventBuffer + DAMAGE_RESPONSE_OFFSET);
        response->m_fDamageHealth = 0.0f;
        response->m_fDamageArmor = 0.0f;
        response->m_bHealthZero = false;
        response->m_bForceDeath = false;
        response->m_bDamageCalculated = true;
        response->m_bCheckIfAffectsPed = false;

        EventGroup_Add(&victim->m_pIntelligence->m_eventGroup, eventBuffer, false);
        CleanUpEventDamageSourceRef(eventBuffer);
        return true;
    }

    static bool QueueThreatEvent(CPed* victim, CPed* threat) {
        if (!victim || !threat || victim == threat || !victim->m_pIntelligence || victim->m_fHealth <= 0.0f || threat->m_fHealth <= 0.0f) {
            return false;
        }

        alignas(8) uint8_t eventBuffer[EVENT_ACQUAINTANCE_PED_SIZE];

        EventAcquaintancePedHate_Ctor(eventBuffer, threat);
        *reinterpret_cast<int16_t*>(eventBuffer + EDITABLE_RESPONSE_TASK_ID_OFFSET) = static_cast<int16_t>(ChooseEscapeThreatTask());
        EventGroup_Add(&victim->m_pIntelligence->m_eventGroup, eventBuffer, false);
        CleanUpAcquaintancePedRef(eventBuffer);
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

        QueueDamageEvent(victim, attacker, 0.0f, kPedPieceTorso, false);
    }

    void TriggerReleasePush(CPed* victim, CPed* attacker)
    {
        if (!victim || !attacker || !victim->m_pIntelligence || victim->m_fHealth <= 0.0f) {
            return;
        }

        EnsureBaseAnimation(victim);
        ApplyReleasePushForce(victim, attacker);
        QueueDamageEvent(
            victim,
            attacker,
            RELEASE_PUSH_DAMAGE,
            kPedPieceTorso,
            RELEASE_FALL_BACK_DIRECTION,
            true,
            false
        );
    }

    void TriggerThreatResponse(CPed* victim, CPed* threat)
    {
        if (!victim || !threat || victim == threat || !victim->m_pIntelligence || victim->m_fHealth <= 0.0f || threat->m_fHealth <= 0.0f) {
            return;
        }

        ClearCurrentEventResponses(victim);
        QueueThreatEvent(victim, threat);
    }
}
