# GTA San Andreas Fighting System Analysis

## Overview

This document analyzes the melee combat system in GTA San Andreas based on the gta-reversed project codebase. The goal is to understand the existing system to implement a VCS-style grab mechanic.

---

## 1. Task-Based Architecture

The combat system uses a task hierarchy where complex behaviors are built from simple tasks.

### Task Hierarchy

```
CTaskComplex (Complex, multi-subtask combat)
├── CTaskComplexKillPedOnFootMelee
│   ├── CTaskComplexSeekEntity (move to target)
│   ├── CTaskSimpleFightingControl (position/angle during fight)
│   ├── CTaskSimpleFight (perform strikes)
│   └── CTaskComplexTurnToFaceEntityOrCoord
│
└── CTaskComplexDestroyCarMelee
    ├── CTaskComplexSeekEntity (move to vehicle)
    ├── CTaskSimpleFightingControl (position around vehicle)
    └── CTaskSimpleFight (perform melee attacks)

CTaskSimple (Direct combat tasks)
├── CTaskSimpleFight (perform attacks/combos)
├── CTaskSimpleFightingControl (movement during fight)
├── CTaskSimpleBeHit (take damage/react to hit)
└── CTaskSimplePlayerOnFoot (includes PlayerControlFighter)
```

---

## 2. Core Fighting Task - CTaskSimpleFight

**Location:** `source/game_sa/Tasks/TaskTypes/TaskSimpleFight.h/cpp`

### Class Structure

```cpp
class CTaskSimpleFight : public CTaskSimple {
    bool m_bIsFinished;
    bool m_bIsInControl;
    bool m_bAnimsReferenced;
    AssocGroupId m_nRequiredAnimGroup;
    uint16 m_nIdlePeriod;
    uint16 m_nIdleCounter;
    int8 m_nContinueStrike;
    int8 m_nChainCounter;
    CEntity* m_pTargetEntity;
    CAnimBlendAssociation* m_pAnim;
    CAnimBlendAssociation* m_pIdleAnim;
    int8 m_nComboSet;
    eFightAttackType m_nCurrentMove;
    uint8 m_nNextCommand;
    uint8 m_nLastCommand;

    // Static array of 12 melee combo configurations
    static CMeleeInfo (&m_aComboData)[12];
};
```

### Key Methods

| Method | Purpose |
|--------|---------|
| `ChooseAttackPlayer(CPed* ped)` | Player selects attacks based on input |
| `ChooseAttackAI(CPed* ped)` | AI selects attacks based on state |
| `ControlFight(CEntity* entity, uint8 command)` | Controls fight flow with commands |
| `FightHitPed(CPed* creator, CPed* victim, ...)` | Hit detection against peds |
| `FightHitCar(CPed* ped, CVehicle* vehicle, ...)` | Hit detection against vehicles |
| `FightHitObj(CPed* ped, CObject* object, ...)` | Hit detection against objects |
| `FightStrike(CPed* ped, CVector& posn)` | Execute strike at position |
| `FightSetUpCol(float)` | Sets up collision for hit detection |
| `GetStrikeDamage(CPed* ped)` | Calculates damage of current strike |
| `IsComboSet()` | Checks if combo is available |
| `GetAvailableComboSet(CPed* ped, int8)` | Gets combo for current weapon/style |

### Attack Types Enumeration

```cpp
enum eFightAttackType : int8 {
    FIGHT_ATTACK_HIT_1 = 0,
    FIGHT_ATTACK_HIT_2 = 1,
    FIGHT_ATTACK_HIT_3 = 2,
    FIGHT_ATTACK_FIGHT_BLOCK = 3,
    FIGHT_ATTACK_FIGHTIDLE = 4,
};
```

---

## 3. Melee Combo System

**Location:** `source/game_sa/eMeleeCombo.h`

### Combo Types

```cpp
enum eMeleeCombo : uint8 {
    MELEE_COMBO_IDLE = 0x0,
    MELEE_COMBO_MOVE = 0x1,
    MELEE_COMBO_BLOCK = 0x2,
    MELEE_COMBO_END = 0x3,
    MELEE_COMBO_UNARMED_1 = 0x4,
    MELEE_COMBO_UNARMED_2 = 0x5,
    MELEE_COMBO_UNARMED_3 = 0x6,
    MELEE_COMBO_UNARMED_4 = 0x7,
    MELEE_COMBO_BBALLBAT = 0x8,
    MELEE_COMBO_KNIFE = 0x9,
    MELEE_COMBO_GOLFCLUB = 0xA,
    MELEE_COMBO_SWORD = 0xB,
    MELEE_COMBO_CHAINSAW = 0xC,
    MELEE_COMBO_DILDO = 0xD,
    MELEE_COMBO_FLOWERS = 0xE,
    MELEE_COMBO_KICK_STD = 0xF,
    MELEE_COMBO_PISTOL_WHIP = 0x10,
};
```

### CMeleeInfo Data Structure

```cpp
class CMeleeInfo {
    AssocGroupId m_nAnimGroup;
    float m_fRanges;              // Attack range
    float m_fHit[5];              // Hit timing thresholds for combo moves
    float m_fChain[5];            // Chain timing thresholds
    float m_fRadius[5];           // Hit radius for each attack
    float m_fGroundLoop;
    int32 ABlockHit;
    int32 ABlockChain;
    uint8 m_nHitLevel;
    int32 m_nDamage;
    int32 field_58;
    int32 m_Hit[5];               // Hit flags for each attack
    int32 m_AltHit[5];
    uint16 m_wFlags;
};
```

---

## 4. Fighting Styles

**Location:** `source/game_sa/Ped/CPed.h` (eFightingStyle enum)

```cpp
enum eFightingStyle : int8 {
    STYLE_STANDARD = 4,
    STYLE_BOXING = 5,
    STYLE_KUNG_FU = 6,
    STYLE_KNEE_HEAD = 7,
    // ...
    STYLE_GRAB_KICK = 15,    // Note: Just a kick style, not actual grab
    STYLE_ELBOWS = 16,
};
```

Each fighting style corresponds to different animation groups and combo sets.

---

## 5. Movement Control During Combat

**Location:** `source/game_sa/Tasks/TaskTypes/TaskSimpleFightingControl.h/cpp`

### Class Structure

```cpp
class CTaskSimpleFightingControl : public CTaskSimple {
    CEntity* m_target;
    float m_angleRad;              // Attack angle
    float m_maxAttackRange;        // Max distance to attack
    float m_unk3;
    uint32 m_nextAttackTime;
    uint32 m_someTime;

    int16 CalcMoveCommand(CPed* ped);
};
```

### Movement Commands

| Command | Action |
|---------|--------|
| 3 | Move closer to target |
| 7 | Block/defend |
| 8 | Special move 1 (kick/jump kick based on distance) |
| 9 | Special move 2 |
| 10 | Special move 3 |

### Range Constraints

- Attack angle limited to 15 degrees (0.261 radians)
- Range obtained from `CMeleeInfo::m_fRanges`

---

## 6. Receiving Damage - CTaskSimpleBeHit

**Location:** `source/game_sa/Tasks/TaskTypes/TaskSimpleBeHit.h/cpp`

### Class Structure

```cpp
class CTaskSimpleBeHit : public CTaskSimple {
    CPed* m_Attacker;
    bool m_bIsFinished;
    bool m_bAnimAdded;
    AnimationId m_nAnimId;
    AssocGroupId m_nAnimGroup;
    ePedPieceTypes m_eHitZone;     // Where was ped hit
    int32 m_nHitPower;             // Damage amount
    int32 m_nDirn;                 // Direction of hit (0-3)
    CAnimBlendAssociation* m_Anim;
};
```

### Hit Animation Selection Based On

- Hit zone (torso, left/right arm, left/right leg, head)
- Direction of hit (4 directions)
- Attacker's current combo set
- Attacker's current move type

---

## 7. Damage Event System

**Location:** `source/game_sa/Events/EventDamage.h`

### CEventDamage Class

Contains:
- Source entity (attacker)
- Weapon type
- Hit location (`ePedPieceTypes`)
- Hit direction
- Damage response

### Key Methods

- `ProcessDamage()` - Process the damage event
- `ComputeDeathAnim()` - Determine death animation
- `ComputeDamageAnim()` - Determine damage reaction animation

### Hit Zones (ePedPieceTypes)

```cpp
enum ePedPieceTypes {
    PED_PIECE_TORSO,
    PED_PIECE_LEFT_ARM,
    PED_PIECE_RIGHT_ARM,
    PED_PIECE_LEFT_LEG,
    PED_PIECE_RIGHT_LEG,
    PED_PIECE_HEAD,
};
```

---

## 8. Damage Response

**Location:** `source/game_sa/Ped/PedDamageResponse.h`

```cpp
class CPedDamageResponse {
    float m_fDamageHealth;
    float m_fDamageArmor;
    bool m_bHealthZero;
    bool m_bForceDeath;
    bool m_bDamageCalculated;
    bool m_bCheckIfAffectsPed;
};
```

---

## 9. Geometry and Hit Side Detection

**Location:** `source/game_sa/PedGeometryAnalyser.h`

### Key Static Methods

```cpp
static int32 ComputeEntityHitSide(const CPed& ped, CEntity& entity);
static int32 ComputePedHitSide(const CPed& ped, const CPhysical& physical);
static void ComputeEntityBoundingBoxPlanesUncachedAll(
    float zPos,
    CEntity& entity,
    CVector (*outPlanes)[4],
    float* outPlanesDot
);
```

Used to determine:
- Which side of target was hit (for appropriate animation)
- Whether target is within attack range
- Valid hit geometry based on entity bounding box

---

## 10. Player Control Integration

**Location:** `source/game_sa/Tasks/TaskTypes/TaskSimplePlayerOnFoot.h/cpp`

### PlayerControlFighter Method

```cpp
void PlayerControlFighter(CPlayerPed* player);
```

This method:
- Processes player input for fighting
- Interfaces with `CTaskSimpleFight`
- Handles attack button presses and combo inputs

---

## 11. AI Combat Integration

**Location:** `source/game_sa/Intelligence/PedIntelligence.h`

### Key Methods

```cpp
CTaskSimpleFight* GetTaskFighting();
CTask* FindTaskByType(eTaskType taskId);
```

The `CPedIntelligence` class manages:
- Event processing for combat
- Task assignment and management
- AI decision making during fights

---

## 12. Key Files Summary

| File | Purpose |
|------|---------|
| `TaskSimpleFight.h/cpp` | Core fighting task, combo selection, damage application |
| `TaskSimpleFightingControl.h/cpp` | Movement and positioning during fights |
| `TaskComplexKillPedOnFootMelee.h/cpp` | Complex multi-step task for AI melee combat |
| `TaskComplexDestroyCarMelee.h/cpp` | Complex task for destroying vehicles with melee |
| `TaskSimpleBeHit.h/cpp` | Handles being hit, plays reaction animations |
| `TaskSimplePlayerOnFoot.h/cpp` | Player control including `PlayerControlFighter()` |
| `eMeleeCombo.h` | Enum of 17 different combo/weapon types |
| `EventDamage.h` | Damage event with full hit information |
| `PedGeometryAnalyser.h` | Geometry calculations for hit detection |
| `PedDamageResponse.h` | Damage response tracking |
| `PedIntelligence.h` | AI task and event management |

---

## 13. Current Grab/Grapple Mechanics

**Finding: NO grab or grapple mechanics exist in SA.**

Evidence:
- No files or classes named `*Grab*`, `*Grapple*`, `*Clinch*`, or `*Takedown*`
- Combat is purely strike-based with no holding/clinching mechanics
- `STYLE_GRAB_KICK` is just a fighting style name, not an actual grab mechanic

---

## 14. VCS Grab Mechanic Reference

In GTA Vice City Stories, the grab system works as follows:

### Grab Initiation
- Player must be close to target ped (within grab range)
- Press attack button when in range to initiate grab
- Player grabs and holds the ped

### Grab State
- Player holds the ped in place
- Both player and ped are locked in grab animation
- Ped cannot escape easily (may struggle)

### Follow-up Actions (while holding)
- **Knee strike** - Knee to body/face
- **Headbutt** - Head strike
- **Throw** - Throw ped in a direction
- **Shove/Push** - Push ped away
- **Release** - Let go of ped

### Grab Escape (for AI)
- AI peds may attempt to break free
- Struggle animations
- Success based on ped stats/health

---

## 15. Proposed Implementation Plan

### New Tasks Required

1. **CTaskSimpleGrab** - Initiates and manages grab state for attacker
2. **CTaskSimpleBeGrabbed** - Manages grabbed state for victim
3. **CTaskSimpleGrabFollow** - Follow-up actions (knee, throw, etc.)

### New Enums

```cpp
enum eGrabAction : uint8 {
    GRAB_ACTION_HOLD = 0,
    GRAB_ACTION_KNEE = 1,
    GRAB_ACTION_HEADBUTT = 2,
    GRAB_ACTION_THROW = 3,
    GRAB_ACTION_SHOVE = 4,
    GRAB_ACTION_RELEASE = 5,
};

enum eGrabState : uint8 {
    GRAB_STATE_NONE = 0,
    GRAB_STATE_INITIATING = 1,
    GRAB_STATE_HOLDING = 2,
    GRAB_STATE_PERFORMING_ACTION = 3,
    GRAB_STATE_RELEASING = 4,
};
```

### Integration Points

1. **PlayerControlFighter()** - Add grab input detection
2. **CTaskSimpleFight** - Add grab initiation as attack option
3. **CPed** - Add grab state tracking
4. **Animation system** - New grab animations required

### Animation Requirements

**IFP File:** `fight_a.ifp`

#### Grabber (Attacker) Animations

| Animation Name | Purpose | Type |
|----------------|---------|------|
| `Fight_grab` | Initial grab initiation | One-shot |
| `Fight_grab_idle` | Holding victim | Loop |
| `Fight_grab_jab` | Jab/punch while holding | One-shot |
| `Fight_grab_stom` | Knee to stomach | One-shot |
| `Fight_grab_fwd` | Throw victim forward | One-shot |
| `Fight_grab_break` | Reaction when victim escapes | One-shot |
| `Fight_grab_ko_re` | Release after knockout | One-shot |

#### Grabbed (Victim) Animations

| Animation Name | Purpose | Type |
|----------------|---------|------|
| `Fight_grabbed` | Being grabbed (initial reaction) | One-shot |
| `Fight_grabbed_idle` | Being held | Loop |
| `Fight_grabbed_jab` | Receiving jab | One-shot |
| `Fight_grabbed_stom` | Receiving stomach knee | One-shot |
| `Fight_grabbed_butt` | Receiving headbutt | One-shot |
| `Fight_grabbed_fwd` | Being thrown forward | One-shot |
| `Fight_grabbed_strug` | Struggling to escape | Loop/One-shot |
| `Fight_grabbed_break` | Successfully breaking free | One-shot |
| `Fight_grabbed_ko` | Getting knocked out | One-shot |
| `Fight_grabbed_ko_re` | Knockout recovery | One-shot |

#### Animation Pairs (Synced)

| Action | Grabber Anim | Victim Anim |
|--------|--------------|-------------|
| Initiate | `Fight_grab` | `Fight_grabbed` |
| Hold | `Fight_grab_idle` | `Fight_grabbed_idle` |
| Jab | `Fight_grab_jab` | `Fight_grabbed_jab` |
| Stomach knee | `Fight_grab_stom` | `Fight_grabbed_stom` |
| Headbutt | ??? | `Fight_grabbed_butt` |
| Throw | `Fight_grab_fwd` | `Fight_grabbed_fwd` |
| Victim escapes | `Fight_grab_break` | `Fight_grabbed_break` |
| Knockout | `Fight_grab_ko_re` | `Fight_grabbed_ko` |
| KO Recovery | - | `Fight_grabbed_ko_re` |
| Struggle | - | `Fight_grabbed_strug` |

---

## 16. Technical Considerations

### Positioning
- Need to sync positions of grabber and grabbed ped
- Use attachment system or manual position updates
- Consider using `CTaskSimpleIKChain` for arm positioning

### State Synchronization
- Both peds must be in compatible states
- Handle interruption (damage, vehicle collision, etc.)
- Handle death of either ped during grab

### AI Response
- AI peds need logic to attempt grab escape
- Escape chance based on ped attributes
- Other AI peds should react to grab situation

### Camera
- May need special camera behavior during grab
- Focus on action for player feedback

---

## 17. 1991 ASI Mod Integration

### Project Location
`D:\GTA 1991\GTA 1991\cleo\1991\`

### Structure
```
1991/
├── source/
│   ├── Main.cpp                     # Entry point
│   ├── keybindings/                 # Input handling
│   │   ├── CKeybindings.h
│   │   └── CKeybindings.cpp
│   ├── stories-sprinting/           # Animation system reference
│   │   ├── CStoriesSprinting.h
│   │   └── CStoriesSprinting.cpp
│   ├── hud/                         # Custom HUD
│   └── grab/                        # NEW - Grab system (to create)
│       ├── CGrabSystem.h
│       └── CGrabSystem.cpp
└── third-party/
    └── plugin-sdk/                  # Hooking utilities
```

### Hooking Mechanisms Available

1. **Direct Memory Patching**
   ```cpp
   patch::SetInt(address, value);
   patch::SetFloat(address, value);
   patch::SetPointer(address, ptr);
   ```

2. **Function Call Redirection**
   ```cpp
   patch::RedirectCall(address, function);
   ```

3. **Function Jump Redirection**
   ```cpp
   patch::RedirectJump(address, function);
   ```

4. **Assembly Hooks**
   ```cpp
   injector::MakeJMP(address, function);
   ```

### Input System

Keybindings use `CControllerConfigManager`:
- Hook at `0x530640` (InitDefaultControlConfiguration)
- `SetKey()` at `0x530490`
- **Grab key: 'R'** (unused, easy reach from WASD)

### Animation IFP Loading (CRITICAL)

Animation blocks must be loaded through the **streaming system**, not just by adding references.

**Common Mistake:**
```cpp
// WRONG - This only increments ref count, doesn't load the IFP!
CAnimManager::AddAnimBlockRef(blockIndex);
```

**Correct Pattern (gta-reversed reference, CarEnterExit.cpp:680-692):**
```cpp
// gta-reversed style (for reference only)
CStreaming::RequestModel(IFPToModelId(blockIndex), STREAMING_KEEP_IN_MEMORY);
CStreaming::LoadAllRequestedModels(false);
CAnimManager::AddAnimBlockRef(blockIndex);
```

**Plugin-SDK Implementation (for actual compilation):**
```cpp
#include <CStreaming.h>
#include <CStreamingInfo.h>

// IFP resources start at model ID 25575
constexpr int IFPToModelId(int blockIndex) { return 25575 + blockIndex; }

// Helper - plugin-sdk doesn't have CStreaming::IsModelLoaded()
static bool IsModelLoaded(int modelId) {
    return CStreaming::ms_aInfoForModel[modelId].m_nLoadState == LOADSTATE_LOADED;
}

bool LoadAnimationBlock(const char* blockName) {
    // 1. Get block index
    int blockIndex = CAnimManager::GetAnimationBlockIndex(blockName);
    if (blockIndex < 0) return false;

    // 2. Convert to streaming model ID
    int modelId = IFPToModelId(blockIndex);

    // 3. Check if already loaded
    if (!IsModelLoaded(modelId)) {
        // 4. Request through streaming system (plugin-sdk uses KEEP_IN_MEMORY, not STREAMING_KEEP_IN_MEMORY)
        CStreaming::RequestModel(modelId, KEEP_IN_MEMORY);

        // 5. Force immediate load
        CStreaming::LoadAllRequestedModels(false);
    }

    // 6. Add reference to keep in memory
    CAnimManager::AddAnimBlockRef(blockIndex);

    // 7. Get block pointer and verify (plugin-sdk uses bLoaded, not IsLoaded)
    CAnimBlock* block = CAnimManager::GetAnimationBlock(blockName);
    return block != nullptr && block->bLoaded;
}
```

**Plugin-SDK vs gta-reversed naming differences:**
| plugin-sdk | gta-reversed |
|------------|--------------|
| `CAnimBlock::bLoaded` | `CAnimBlock::IsLoaded` |
| `KEEP_IN_MEMORY` | `STREAMING_KEEP_IN_MEMORY` |
| `ms_aInfoForModel[id].m_nLoadState` | `CStreaming::IsModelLoaded(id)` |
| `LOADSTATE_LOADED` | `eStreamingLoadState::LOADSTATE_LOADED` |

**Cleanup on game restart:**
```cpp
if (blockIndex >= 0) {
    CAnimManager::RemoveAnimBlockRef(blockIndex);
}
```

**Key Files:**
- `gta-reversed/source/game_sa/Streaming.h` - Streaming flags and IFPToModelId
- `gta-reversed/source/game_sa/CarEnterExit.cpp:680-692` - Best loading example
- `gta-reversed/source/game_sa/Interior/InteriorGroup_c.cpp:107-117` - Check-before-load pattern

### Implementation Pattern

Follow existing subsystem pattern:
```cpp
// CGrabSystem.h
class CGrabSystem {
public:
    static void InstallHooks();
    static void ProcessGrab();
private:
    static CPed* m_pGrabbedPed;
    static bool m_bIsGrabbing;
    static int m_nGrabState;
};

// Main.cpp
CGrabSystem::InstallHooks();
```

---

## 18. Grab System Implementation Plan

### Phase 1: Foundation
- [x] Create `CGrabSystem.h/cpp` files
- [x] Add 'R' key binding for grab
- [x] Hook into player update loop (Events::gameProcessEvent)
- [x] Load `fight_a.ifp` animations via streaming system (see "Animation IFP Loading" section)

### Phase 2: Core Grab Logic
- [ ] Detect nearby peds in grab range
- [ ] Initiate grab on key press
- [ ] Play `Fight_grab` / `Fight_grabbed` sync
- [ ] Transition to hold state (`Fight_grab_idle` / `Fight_grabbed_idle`)

### Phase 3: Grab Actions
- [ ] Jab attack (Attack button) → `Fight_grab_jab` / `Fight_grabbed_jab`
- [ ] Uppercut (Forward + Attack) → `Fight_grab_stom` / `Fight_grabbed_butt`
- [ ] Throw (Movement direction + G) → `Fight_grab_fwd` / `Fight_grabbed_fwd`
- [ ] Release (G again or timeout)

### Phase 4: Victim AI
- [ ] Struggle system (`Fight_grabbed_strug`)
- [ ] Break free chance (`Fight_grabbed_break` / `Fight_grab_break`)
- [ ] Knockout state (`Fight_grabbed_ko`)

### Key Memory Addresses Needed
- `CPlayerPed` pointer
- `CPed::m_pPed` offset (0x2C8)
- Nearby ped scanning function
- Animation playback functions
- Ped state management

---

## 19. GTA-Reversed Key References

Use gta-reversed as the primary reference (not plugin-sdk).

### Helper Functions (from World.h)
```cpp
// Get player ped
CPlayerPed* FindPlayerPed(int32 playerId = -1);

// Get player position
CVector FindPlayerCoors(int32 playerId = -1);

// Find objects in range (peds, vehicles, etc.)
void CWorld::FindObjectsInRange(
    const CVector& point,
    float radius,
    bool b2D,
    int16* outCount,
    int16 maxCount,
    CEntity** outEntities,
    bool buildings,
    bool vehicles,
    bool peds,      // Set true to find peds
    bool objects,
    bool dummies
);
```

### CPed Key Members (from Entity/Ped/Ped.h)
```cpp
class CPed : public CPhysical {
    CPedIntelligence* m_pIntelligence;  // Task manager access
    CPlayerPedData*   m_pPlayerData;
    ePedState         m_nPedState;       // Current state
    eMoveState        m_nMoveState;
    float             m_fHealth;
    float             m_fMaxHealth;
    float             m_fArmour;
    CVehicle*         m_pVehicle;
    eFightingStyle    m_nFightingStyle;

    // Key methods
    CPedIntelligence* GetIntelligence();
    CTaskManager& GetTaskManager();
    void SetPedState(ePedState pedState);
    bool IsAlive() const;
    bool IsPedInControl() const;
    CVector GetBonePosition(eBoneTag boneId, bool updateSkinBones = false);
};
```

### CTaskSimpleFight (from Tasks/TaskTypes/TaskSimpleFight.h)
```cpp
class CTaskSimpleFight : public CTaskSimple {
    bool                   m_bIsFinished;
    bool                   m_bIsInControl;
    CEntity*               m_pTargetEntity;
    CAnimBlendAssociation* m_pAnim;
    CAnimBlendAssociation* m_pIdleAnim;
    int8                   m_nComboSet;
    eFightAttackType       m_nCurrentMove;
    uint8                  m_nNextCommand;
    uint8                  m_nLastCommand;

    // Static combo data
    static inline CMeleeInfo (&m_aComboData)[12] = *(CMeleeInfo(*)[12])0xC170D0;

    // Key methods
    bool ControlFight(CEntity* entity, uint8 command);
    void FightHitPed(CPed* creator, CPed* victim, CVector& posn1, CVector& posn2, int16);
    void FightStrike(CPed* ped, CVector& posn);
};
```

### CMeleeInfo Structure
```cpp
class CMeleeInfo {
    AssocGroupId m_nAnimGroup;
    float  m_fRanges;           // Attack range
    float  m_fHit[5];           // Hit timing
    float  m_fChain[5];         // Chain timing
    float  m_fRadius[5];        // Hit radius per move
    float  m_fGroundLoop;
    int32  ABlockHit;
    int32  ABlockChain;
    uint8  m_nHitLevel;
    int32  m_nDamage;
    int32  m_Hit[5];
    int32  m_AltHit[5];
    uint16 m_wFlags;
};
```

### Animation System (from Animation/AnimManager.h)
```cpp
class CAnimManager {
    // Load/get animation block
    static CAnimBlock* GetAnimationBlock(const char* name);
    static int32 GetAnimationBlockIndex(const char* name);
    static int32 GetAnimationBlockIndex(CAnimBlock* block);

    // Get animation from block
    static CAnimBlendHierarchy* GetAnimation(const char* animName, const CAnimBlock* animBlock);

    // Play animations on ped clump
    static CAnimBlendAssociation* AddAnimation(RpClump* clump, AssocGroupId groupId, AnimationId animId);
    static CAnimBlendAssociation* BlendAnimation(RpClump* clump, AssocGroupId groupId, AnimationId animId, float blendData = 8.f);
    static CAnimBlendAssociation* BlendAnimation(RpClump* clump, CAnimBlendHierarchy* hier, int32 flags, float blendData = 8.f);

    // Reference counting (does NOT load the IFP!)
    static void AddAnimBlockRef(int32 index);
    static void RemoveAnimBlockRef(int32 index);
    static int32 GetNumRefsToAnimBlock(int32 index);

    // Uncompress animation for playback
    static void UncompressAnimation(CAnimBlendHierarchy* hier);
};
```

### Animation Block Structure (from Animation/AnimBlock.h)
```cpp
class CAnimBlock {
    char         Name[16];        // Block name (e.g., "ped", "fight_a")
    bool         IsLoaded;        // Whether block is loaded in memory
    int16        RefCnt;          // Reference counter
    int32        FirstAnimIdx;    // Index of first animation in global array
    uint32       NumAnims;        // Number of animations in this block
    AssocGroupId GroupId;         // Associated animation group ID
};
// Size: 0x20 bytes (32 bytes)
```

### Streaming System Integration (from Streaming.h)
```cpp
// Resource ID ranges
enum eResourceFirstID : int32 {
    RESOURCE_ID_DFF = 0,          // Models: 0 - 19999
    RESOURCE_ID_TXD = 20000,      // Textures: 20000 - 24999
    RESOURCE_ID_COL = 25000,      // Collision: 25000 - 25254
    RESOURCE_ID_IPL = 25255,      // IPL: 25255 - 25510
    RESOURCE_ID_DAT = 25511,      // DAT: 25511 - 25574
    RESOURCE_ID_IFP = 25575,      // Animation IFPs: 25575 - 25754 (180 slots)
    RESOURCE_ID_RRR = 25755,      // Vehicle recordings
    RESOURCE_ID_SCM = 26230,      // Streamed scripts
};

// Convert block index to streaming model ID
constexpr int32 IFPToModelId(int32 blockIndex) { return 25575 + blockIndex; }

// Streaming flags
enum eStreamingFlags {
    STREAMING_DEFAULT          = 0x0,
    STREAMING_GAME_REQUIRED    = 0x2,   // Required by game
    STREAMING_MISSION_REQUIRED = 0x4,   // Required by mission
    STREAMING_KEEP_IN_MEMORY   = 0x8,   // Don't auto-unload
    STREAMING_PRIORITY_REQUEST = 0x10,  // Load with priority
};

class CStreaming {
    static void RequestModel(int32 modelId, eStreamingFlags flags);
    static void LoadAllRequestedModels(bool onlyPriority);
    static bool IsModelLoaded(int32 modelId);
    static void SetModelIsDeletable(int32 modelId);
    static void RemoveModel(int32 modelId);
};
```

### Animation Limits
```cpp
NUM_ANIM_BLOCKS = 180;           // Max animation blocks (IFP files)
NUM_ANIM_ASSOC_GROUPS = 118;     // Max animation groups
MAX_ANIM_BLOCK_NAME = 16;        // Max block name length
// Total animations: 2500 (global cache)
```

### eFightAttackType
```cpp
enum eFightAttackType : int8 {
    FIGHT_ATTACK_HIT_1 = 0,
    FIGHT_ATTACK_HIT_2 = 1,
    FIGHT_ATTACK_HIT_3 = 2,
    FIGHT_ATTACK_FIGHT_BLOCK = 3,
    FIGHT_ATTACK_FIGHTIDLE = 4,
};
```

### ePedState (relevant states)
```cpp
enum ePedState {
    PEDSTATE_NONE = 0,
    PEDSTATE_IDLE = 1,
    PEDSTATE_ATTACK = 4,
    PEDSTATE_DEAD = 54,
    PEDSTATE_DIE = 55,
    // ... etc
};
```

---

*Document created for gta-reversed project - VCS Grab Mechanic Implementation*
