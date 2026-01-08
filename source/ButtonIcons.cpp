#include "ButtonIcons.h"

#include <plugin.h>
#include <Events.h>
#include <CFont.h>
#include <CSprite2d.h>
#include <CTxdStore.h>
#include <RenderWare.h>

#include <cstring>

using namespace plugin;

namespace ButtonIcons {

// ============================================================================
// EXTENDED SPRITE ARRAY
// ============================================================================
// Original game has 15 slots at 0xC71AD8 (indices 1-7 used for PS2 buttons)
// We expand to 64 slots to support mouse + keyboard sprites
//
// Layout:
// 0:       Unused (PS2Symbol=0 means no sprite)
// 1-7:     PS2 controller buttons (original game)
// 8-31:    Keyboard keys (future)
// 32-38:   Mouse buttons
// 39-63:   Reserved
// ============================================================================

static const int MAX_EXTENDED_SPRITES = 64;
static CSprite2d g_ExtendedSprites[MAX_EXTENDED_SPRITES];

static const int MOUSE_SPRITE_BASE = 32;

// ============================================================================
// INTERNAL STATE
// ============================================================================

static bool g_Enabled = true;
static bool g_TexturesLoaded = false;
static int g_TxdSlot = -1;

// Sprite names in TXD - named by Windows Virtual Key codes where applicable
static const char* const g_SpriteNames[MOUSE_COUNT] = {
    "1",              // MOUSE_LMB (VK_LBUTTON = 0x01)
    "2",              // MOUSE_RMB (VK_RBUTTON = 0x02)
    "4",              // MOUSE_MMB (VK_MBUTTON = 0x04)
    "MWHU",           // MOUSE_WHEEL_UP
    "MWHD",           // MOUSE_WHEEL_DOWN
    "5",              // MOUSE_BUTTON_4 (VK_XBUTTON1 = 0x05)
    "6"               // MOUSE_BUTTON_5 (VK_XBUTTON2 = 0x06)
};

// ============================================================================
// ARRAY EXPANSION - Patch game to use our extended array
// ============================================================================

static void ExpandButtonSpriteArray() {
    // Copy original 15 sprites from game's array (preserves PS2 button textures)
    CSprite2d* originalArray = reinterpret_cast<CSprite2d*>(0xC71AD8);
    memcpy(g_ExtendedSprites, originalArray, 15 * sizeof(CSprite2d));
    
    // Patch CFont::PrintChar to use our extended array
    // At 0x718ADE: lea ecx, [0xC71AD8 + ecx*4]
    // The address 0xC71AD8 is encoded at 0x718AE1
    patch::SetPointer(0x718AE1, g_ExtendedSprites);
}

// ============================================================================
// TEXTURE LOADING
// ============================================================================

static void LoadTextures() {
    if (g_TexturesLoaded) return;
    
    g_TxdSlot = CTxdStore::AddTxdSlot("buttonicons");
    if (g_TxdSlot == -1) return;
    
    if (!CTxdStore::LoadTxd(g_TxdSlot, "models\\pcbtns.txd")) {
        CTxdStore::RemoveTxdSlot(g_TxdSlot);
        g_TxdSlot = -1;
        return;
    }
    
    CTxdStore::AddRef(g_TxdSlot);
    CTxdStore::SetCurrentTxd(g_TxdSlot);
    
    for (int i = 0; i < MOUSE_COUNT; i++) {
        g_ExtendedSprites[MOUSE_SPRITE_BASE + i].SetTexture(const_cast<char*>(g_SpriteNames[i]));
    }
    
    CTxdStore::PopCurrentTxd();
    g_TexturesLoaded = true;
}

static void UnloadTextures() {
    if (!g_TexturesLoaded) return;
    
    for (int i = 0; i < MOUSE_COUNT; i++) {
        g_ExtendedSprites[MOUSE_SPRITE_BASE + i].Delete();
    }
    
    if (g_TxdSlot != -1) {
        CTxdStore::RemoveTxdSlot(g_TxdSlot);
        g_TxdSlot = -1;
    }
    
    g_TexturesLoaded = false;
}

// ============================================================================
// HOOK: CFont::ParseToken
// Detect ~0~ through ~6~ tokens and set PS2Symbol to our sprite index
// ============================================================================

using ParseToken_t = char*(__cdecl*)(char*, CRGBA&, bool, char*);
static ParseToken_t ParseToken_Original = reinterpret_cast<ParseToken_t>(0x718F00);

static uint8_t* g_PS2Symbol = reinterpret_cast<uint8_t*>(0xC71A54);

char* __cdecl ParseToken_Hooked(char* text, CRGBA& color, bool isBlip, char* tag) {
    if (!text || !g_Enabled || !g_TexturesLoaded) {
        return ParseToken_Original(text, color, isBlip, tag);
    }
    
    // Check for ~0~ through ~6~ tokens
    if (text[0] == '~' && text[2] == '~') {
        char tokenChar = text[1];
        
        if (tokenChar >= '0' && tokenChar <= '6') {
            int mouseIndex = tokenChar - '0';
            *g_PS2Symbol = static_cast<uint8_t>(MOUSE_SPRITE_BASE + mouseIndex);
            return text + 3;
        }
    }
    
    return ParseToken_Original(text, color, isBlip, tag);
}

// ============================================================================
// HOOK: CFont::PrintString
// Replace mouse button text (LMB, RMB, etc.) with sprite tokens
// ============================================================================

using ProcessStringToDisplay_t = short(__cdecl*)(float x, float y, const char* text);
static ProcessStringToDisplay_t ProcessStringToDisplay_Orig = 
    reinterpret_cast<ProcessStringToDisplay_t>(0x71A600);

struct TextMapping {
    const char* text;
    const char* token;
};

static const TextMapping g_MouseMappings[] = {
    { "LMB", "~0~" },
    { "RMB", "~1~" },
    { "MMB", "~2~" },
    { "MSWHEELUP", "~3~" },
    { "MS WHEEL UP", "~3~" },
    { "MSWHEELDOWN", "~4~" },
    { "MS WHEEL DN", "~4~" },
    { "MXB1", "~5~" },
    { "MXB2", "~6~" },
};

static char g_TextBuffer[2048];

static void ReplaceMouseText(char* dest, const char* src, size_t destSize) {
    strncpy(dest, src, destSize - 1);
    dest[destSize - 1] = '\0';
    
    for (const auto& mapping : g_MouseMappings) {
        char* found = strstr(dest, mapping.text);
        while (found) {
            size_t textLen = strlen(mapping.text);
            size_t tokenLen = strlen(mapping.token);
            
            if (tokenLen <= textLen) {
                memcpy(found, mapping.token, tokenLen);
                if (tokenLen < textLen) {
                    memmove(found + tokenLen, found + textLen, strlen(found + textLen) + 1);
                }
            }
            
            found = strstr(found + tokenLen, mapping.text);
        }
    }
}

void __cdecl PrintString_Hooked(float x, float y, const char* text) {
    if (!text || *text == '\0' || *text == '*') {
        return;
    }
    
    const char* textToRender = text;
    
    if (g_Enabled && g_TexturesLoaded) {
        bool needsReplacement = strstr(text, "LMB") || strstr(text, "RMB") || 
                                strstr(text, "MMB") || strstr(text, "MXB") ||
                                strstr(text, "MSWHEEL") || strstr(text, "MS WHEEL");
        if (needsReplacement) {
            ReplaceMouseText(g_TextBuffer, text, sizeof(g_TextBuffer));
            textToRender = g_TextBuffer;
        }
    }
    
    ProcessStringToDisplay_Orig(x, y, textToRender);
}

// ============================================================================
// PUBLIC API: Direct Drawing
// ============================================================================

void DrawIcon(MouseButton button, float x, float y, float size) {
    if (!g_TexturesLoaded || button < 0 || button >= MOUSE_COUNT) return;
    
    CSprite2d* sprite = &g_ExtendedSprites[MOUSE_SPRITE_BASE + button];
    if (!sprite->m_pTexture) return;
    
    CRect rect(x - size * 0.5f, y - size * 0.5f, x + size * 0.5f, y + size * 0.5f);
    sprite->Draw(rect, CRGBA(255, 255, 255, 255));
}

void DrawIconColored(MouseButton button, float x, float y, float size,
                     unsigned char r, unsigned char g, unsigned char b, unsigned char a) {
    if (!g_TexturesLoaded || button < 0 || button >= MOUSE_COUNT) return;
    
    CSprite2d* sprite = &g_ExtendedSprites[MOUSE_SPRITE_BASE + button];
    if (!sprite->m_pTexture) return;
    
    CRect rect(x - size * 0.5f, y - size * 0.5f, x + size * 0.5f, y + size * 0.5f);
    sprite->Draw(rect, CRGBA(r, g, b, a));
}

// ============================================================================
// INSTALLATION
// ============================================================================

void InstallHooks() {
    ExpandButtonSpriteArray();
    
    Events::initGameEvent += []() {
        LoadTextures();
    };
    
    Events::shutdownRwEvent += []() {
        UnloadTextures();
    };
    
    patch::RedirectCall(0x719965, ParseToken_Hooked);
    patch::RedirectJump(0x71A700, PrintString_Hooked);
}

// ============================================================================
// PUBLIC API
// ============================================================================

bool IsEnabled() {
    return g_Enabled;
}

void SetEnabled(bool enabled) {
    g_Enabled = enabled;
}

void ReloadTextures() {
    UnloadTextures();
    LoadTextures();
}

} // namespace ButtonIcons
