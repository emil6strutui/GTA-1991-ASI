#pragma once

#include <plugin.h>
#include <CAnimManager.h>
#include <CAnimBlendAssociation.h>
#include <CAnimBlendHierarchy.h>
#include <CStreaming.h>
#include <CStreamingInfo.h>
#include <CPed.h>
#include <algorithm>

/**
 * Animation constants and helpers for the grab system.
 * Centralizes all animation-related definitions.
 */
namespace GrabAnimations
{
    // Animation block name
    static constexpr const char* ANIM_BLOCK_NAME = "fight_a";

    // Grabber animations
    static constexpr const char* ANIM_GRAB = "Fight_grab";
    static constexpr const char* ANIM_GRAB_IDLE = "Fight_grab_idle";
    static constexpr const char* ANIM_GRAB_JAB = "Fight_grab_jab";
    static constexpr const char* ANIM_GRAB_BREAK = "Fight_grab_break";
    static constexpr const char* ANIM_GRAB_RELEASE = "Fight_grb_r";
    static constexpr const char* ANIM_GRAB_THROW = "Fight_grb_r";
    static constexpr const char* ANIM_GRAB_UPPERCUT = "Fight_grab_stom";

    // Victim animations
    static constexpr const char* ANIM_GRABBED = "Fight_grabbed";
    static constexpr const char* ANIM_GRABBED_IDLE = "Fight_grabbed_idle";
    static constexpr const char* ANIM_GRABBED_JAB = "Fight_grabbed_jab";
    static constexpr const char* ANIM_GRABBED_BREAK = "Fight_grabbed_break";
    static constexpr const char* ANIM_GRABBED_THROW = "Fight_grabbed_throw";
    static constexpr const char* ANIM_GRABBED_UPPERCUT = "Fight_grabbed_stom";

    // Positioning constants
    static constexpr float START_OFFSET_FORWARD = 1.6f;
    static constexpr float FINAL_OFFSET_FORWARD = 0.5f;
    static constexpr float OFFSET_Z = 0.0f;
    static constexpr float START_BLEND_DELTA = 1000.0f;
    static constexpr float ESCAPE_BREAK_ANIM_SPEED = 0.8f;

    // CDefaultAnimCallback::DefaultAnimCB - does nothing (just retn)
    static auto DefaultAnimCB = reinterpret_cast<void(*)(CAnimBlendAssociation*, void*)>(0x4D1390);

    // Helper to convert IFP block index to model ID for streaming
    inline constexpr int IFPToModelId(int blockIndex) {
        return 25575 + blockIndex;
    }

    // Check if animation model is loaded
    inline bool IsModelLoaded(int modelId) {
        return CStreaming::ms_aInfoForModel[modelId].m_nLoadState == LOADSTATE_LOADED;
    }

    /**
     * Load animations for the grab system.
     * @param[out] animsReferenced Set to true if animations were successfully loaded and referenced
     * @return true if animations are ready to use
     */
    inline bool LoadAnimations(bool& animsReferenced) {
        if (animsReferenced) {
            return true;
        }

        int blockIndex = CAnimManager::GetAnimationBlockIndex(ANIM_BLOCK_NAME);
        if (blockIndex < 0) {
            return false;
        }

        int modelId = IFPToModelId(blockIndex);

        if (!IsModelLoaded(modelId)) {
            CStreaming::RequestModel(modelId, KEEP_IN_MEMORY);
            CStreaming::LoadAllRequestedModels(false);
            if (!IsModelLoaded(modelId)) {
                return false;
            }
        }

        CAnimManager::AddAnimBlockRef(blockIndex);

        animsReferenced = true;
        return true;
    }

    /**
     * Unload animation references.
     */
    inline void UnloadAnimations(bool& animsReferenced) {
        if (!animsReferenced) {
            return;
        }

        int blockIndex = CAnimManager::GetAnimationBlockIndex(ANIM_BLOCK_NAME);
        if (blockIndex >= 0) {
            CAnimManager::RemoveAnimBlockRef(blockIndex);
        }
        animsReferenced = false;
    }

    /**
     * Get animation block pointer.
     */
    inline CAnimBlock* GetAnimBlock() {
        return CAnimManager::GetAnimationBlock(ANIM_BLOCK_NAME);
    }

    /**
     * Get animation hierarchy by name.
     */
    inline CAnimBlendHierarchy* GetAnimation(const char* animName) {
        CAnimBlock* block = GetAnimBlock();
        if (!block) {
            return nullptr;
        }
        return CAnimManager::GetAnimation(animName, block);
    }

    inline float GetAnimationDuration(const char* animName) {
        if (const auto* hier = GetAnimation(animName)) {
            return hier->m_fTotalTime;
        }
        return 0.0f;
    }

    inline float GetSynchronizedSpeed(const char* animName, const char* partnerAnimName) {
        const float ownDuration = GetAnimationDuration(animName);
        const float partnerDuration = GetAnimationDuration(partnerAnimName);
        const float targetDuration = std::max(ownDuration, partnerDuration);

        if (ownDuration <= 0.0f || targetDuration <= 0.0f) {
            return 1.0f;
        }

        return std::clamp(ownDuration / targetDuration, 0.01f, 1.0f);
    }

    inline CAnimBlendAssociation* FindAssociation(CPed* ped, const char* animName) {
        if (!ped || !ped->m_pRwClump || !animName) {
            return nullptr;
        }
        return RpAnimBlendClumpGetAssociation(ped->m_pRwClump, const_cast<char*>(animName));
    }

    inline void ClearAnimationCallbacks(CAnimBlendAssociation* anim) {
        if (!anim) {
            return;
        }

        anim->SetDeleteCallback(DefaultAnimCB, nullptr);
        anim->SetFinishCallback(DefaultAnimCB, nullptr);
    }

    inline void EnsureBaseAnimation(CPed* ped, float blendDelta = 1000.0f) {
        if (!ped || !ped->m_pRwClump) {
            return;
        }

        CAnimManager::BlendAnimation(
            ped->m_pRwClump,
            ped->m_nAnimGroup,
            ANIM_DEFAULT_IDLE_STANCE,
            blendDelta
        );
    }

    /**
     * Safely clean up an animation association.
     */
    inline void CleanupAnimation(CAnimBlendAssociation*& anim) {
        if (!anim) {
            return;
        }
        
        ClearAnimationCallbacks(anim);
        anim->m_nFlags |= ANIMATION_FREEZE_LAST_FRAME;
        
        if (anim->m_fBlendAmount > 0.0f && anim->m_fBlendDelta >= 0.0f) {
            anim->m_fBlendDelta = -4.0f;
        }
        
        anim = nullptr;
    }

    /**
     * Blend out an animation gracefully.
     */
    inline void BlendOutAnimation(CAnimBlendAssociation*& anim, float blendDelta = -4.0f) {
        if (!anim) {
            return;
        }

        ClearAnimationCallbacks(anim);
        anim->m_nFlags |= ANIMATION_FREEZE_LAST_FRAME;
        anim->m_fBlendDelta = blendDelta;
        anim = nullptr;
    }

    /**
     * Abort an owned task animation using the same model as CTaskSimpleAnim:
     * partial animations fade themselves out, while full-body animations blend
     * the ped back to idle so the clump is never left with no base movement.
     */
    inline void AbortAnimation(CPed* ped, CAnimBlendAssociation*& anim, eAbortPriority priority, bool holdLastFrame = false) {
        if (!anim) {
            return;
        }

        const float blendDelta = priority == ABORT_PRIORITY_IMMEDIATE ? -1000.0f : -4.0f;

        ClearAnimationCallbacks(anim);
        anim->m_nFlags |= ANIMATION_FREEZE_LAST_FRAME;

        if (!holdLastFrame) {
            if (anim->m_nFlags & ANIMATION_PARTIAL) {
                anim->m_fBlendDelta = blendDelta;
            } else if (ped && ped->m_pRwClump) {
                CAnimManager::BlendAnimation(ped->m_pRwClump, ped->m_nAnimGroup, ANIM_DEFAULT_IDLE_STANCE, -blendDelta);
            } else if (anim->m_fBlendAmount > 0.0f && anim->m_fBlendDelta >= 0.0f) {
                anim->m_fBlendDelta = blendDelta;
            }
        }

        anim = nullptr;
    }

    /**
     * Release ownership of an animation without blending it out.
     * The animation keeps playing on the clump. BLEND_AUTO_REMOVE is set
     * as a safety net so the engine will clean it up if something later
     * fades it (e.g. a new non-partial anim blends in after the grab ends).
     */
    inline void ReleaseAnimation(CAnimBlendAssociation*& anim) {
        if (!anim) {
            return;
        }

        ClearAnimationCallbacks(anim);
        anim->m_nFlags |= ANIMATION_FREEZE_LAST_FRAME;
        anim = nullptr;
    }
}
