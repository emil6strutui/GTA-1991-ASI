#pragma once

#include <plugin.h>
#include <CAnimManager.h>
#include <CAnimBlendAssociation.h>
#include <CAnimBlendHierarchy.h>
#include <CStreaming.h>
#include <CStreamingInfo.h>

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
    static constexpr const char* ANIM_GRAB_THROW = "Fight_grab_throw";       // If exists
    static constexpr const char* ANIM_GRAB_UPPERCUT = "Fight_grab_uppercut"; // If exists

    // Victim animations
    static constexpr const char* ANIM_GRABBED = "Fight_grabbed";
    static constexpr const char* ANIM_GRABBED_IDLE = "Fight_grabbed_idle";
    static constexpr const char* ANIM_GRABBED_JAB = "Fight_grabbed_jab";
    static constexpr const char* ANIM_GRABBED_THROW = "Fight_grabbed_throw";       // If exists
    static constexpr const char* ANIM_GRABBED_UPPERCUT = "Fight_grabbed_uppercut"; // If exists

    // Positioning constants
    static constexpr float START_OFFSET_FORWARD = 1.6f;
    static constexpr float FINAL_OFFSET_FORWARD = 0.5f;
    static constexpr float OFFSET_Z = 0.0f;

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

    /**
     * Safely clean up an animation association.
     */
    inline void CleanupAnimation(CAnimBlendAssociation*& anim) {
        if (!anim) {
            return;
        }
        
        anim->SetDeleteCallback(DefaultAnimCB, nullptr);
        anim->SetFinishCallback(DefaultAnimCB, nullptr);
        anim->m_nFlags |= ANIMATION_IS_BLEND_AUTO_REMOVE;
        
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
        
        anim->SetFinishCallback(DefaultAnimCB, nullptr);
        anim->m_fBlendDelta = blendDelta;
        anim = nullptr;
    }
}
