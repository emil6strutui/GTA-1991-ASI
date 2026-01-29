#pragma once

#include <plugin.h>
#include <CPed.h>

/**
 * Handles post-grab AI reactions.
 * Triggers appropriate events to make the victim respond after being released.
 */
namespace CPostGrabReaction
{
    /**
     * Trigger a reaction in the victim after being released from a grab.
     * Creates a damage event to make the victim aware of the attacker.
     * 
     * @param victim The ped that was grabbed
     * @param attacker The ped that did the grabbing
     */
    void TriggerReaction(CPed* victim, CPed* attacker);
}
