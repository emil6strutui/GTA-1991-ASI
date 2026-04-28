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
    bool ApplyDamageWithoutReaction(CPed* victim, CPed* attacker, float damage, uint8_t bodyPart, bool bSpeak);
    bool QueueDamageEvent(CPed* victim, CPed* attacker, float damage, uint8_t bodyPart, bool bSpeak);
    bool QueueDamageEvent(CPed* victim, CPed* attacker, float damage, uint8_t bodyPart, uint8_t direction, bool forceFallDown, bool bSpeak);

    /**
     * Trigger a reaction in the victim after being released from a grab.
     * Creates a damage event to make the victim aware of the attacker.
     * 
     * @param victim The ped that was grabbed
     * @param attacker The ped that did the grabbing
     */
    void TriggerReaction(CPed* victim, CPed* attacker);
    void TriggerReleasePush(CPed* victim, CPed* attacker);
}
