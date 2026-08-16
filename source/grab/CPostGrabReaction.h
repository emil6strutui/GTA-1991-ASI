#pragma once

#include <plugin.h>
#include <CPed.h>
#include <cstdint>

/**
 * Handles post-grab AI reactions.
 * Triggers appropriate events to make the victim respond after being released.
 */
namespace CPostGrabReaction
{
    // Native SA 1.0 US body-part IDs. plugin-sdk's ePedPieceTypes is empty.
    inline constexpr uint8_t kPedPieceTorso = 3;
    inline constexpr uint8_t kPedPieceMidsection = 4;

    bool ApplyDamageWithoutReaction(CPed* victim, CPed* attacker, float damage, uint8_t bodyPart, bool bSpeak);
    bool QueueDamageEvent(CPed* victim, CPed* attacker, float damage, uint8_t bodyPart, bool bSpeak);
    bool QueueDamageEvent(CPed* victim, CPed* attacker, float damage, uint8_t bodyPart, uint8_t direction, bool forceFallDown, bool bSpeak);
    bool QueueZeroDamageMeleeChestEvent(CPed* victim, CPed* attacker, uint8_t direction, bool bSpeak);

    /**
     * Trigger a reaction in the victim after being released from a grab.
     * Creates a damage event to make the victim aware of the attacker.
     * 
     * @param victim The ped that was grabbed
     * @param attacker The ped that did the grabbing
     */
    void TriggerReaction(CPed* victim, CPed* attacker);
    void TriggerReleasePush(CPed* victim, CPed* attacker);
    void TriggerThreatResponse(CPed* victim, CPed* threat);
}
