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
// Layout:
// 0:       Unused (PS2Symbol=0 means no sprite)
// 1-7:     PS2 controller buttons (original game)
// 8-55:    Keyboard keys
// 56-62:   Mouse buttons
// 63:      Reserved
// ============================================================================

static const int MAX_EXTENDED_SPRITES = 64;
static CSprite2d g_ExtendedSprites[MAX_EXTENDED_SPRITES];

static const int KEYBOARD_SPRITE_BASE = 8;
static const int MOUSE_SPRITE_BASE = 56;

// ============================================================================
// KEYBOARD SPRITE NAMES (Windows Virtual Key codes)
// ============================================================================

static const char* const g_KeyboardSpriteNames[KEYBOARD_COUNT] = {
    // Movement keys
    "W",   // KEY_W      VK_W = 0x57 (87)
    "A",   // KEY_A      VK_A = 0x41 (65)
    "S",   // KEY_S      VK_S = 0x53 (83)
    "D",   // KEY_D      VK_D = 0x44 (68)
    "38",   // KEY_UP     VK_UP = 0x26 (38)
    "40",   // KEY_DOWN   VK_DOWN = 0x28 (40)
    "37",   // KEY_LEFT   VK_LEFT = 0x25 (37)
    "39",   // KEY_RIGHT  VK_RIGHT = 0x27 (39)
    
    // Letter keys
    "E",   // KEY_E      VK_E = 0x45 (69)
    "Q",   // KEY_Q      VK_Q = 0x51 (81)
    "F",   // KEY_F      VK_F = 0x46 (70)
    "G",   // KEY_G      VK_G = 0x47 (71)
    "H",   // KEY_H      VK_H = 0x48 (72)
    "N",   // KEY_N      VK_N = 0x4E (78)
    "Y",   // KEY_Y      VK_Y = 0x59 (89)
    "X",   // KEY_X      VK_X = 0x58 (88)
    "Z",   // KEY_Z      VK_Z = 0x5A (90)
    "V",   // KEY_V      VK_V = 0x56 (86)
    "C",   // KEY_C      VK_C = 0x43 (67)
    
    // Numpad
    "96",   // KEY_NUM0   VK_NUMPAD0 = 0x60 (96)
    "97",   // KEY_NUM1   VK_NUMPAD1 = 0x61 (97)
    "98",   // KEY_NUM2   VK_NUMPAD2 = 0x62 (98)
    "99",   // KEY_NUM3   VK_NUMPAD3 = 0x63 (99)
    "100",  // KEY_NUM4   VK_NUMPAD4 = 0x64 (100)
    "101",  // KEY_NUM5   VK_NUMPAD5 = 0x65 (101)
    "102",  // KEY_NUM6   VK_NUMPAD6 = 0x66 (102)
    "103",  // KEY_NUM7   VK_NUMPAD7 = 0x67 (103)
    "104",  // KEY_NUM8   VK_NUMPAD8 = 0x68 (104)
    "105",  // KEY_NUM9   VK_NUMPAD9 = 0x69 (105)
    "110",  // KEY_NUMDOT VK_DECIMAL = 0x6E (110)
    
    // Modifiers
    "162",  // KEY_LCTRL    VK_LCONTROL = 0xA2 (162)
    "163",  // KEY_RCTRL    VK_RCONTROL = 0xA3 (163)
    "160",  // KEY_LSHIFT   VK_LSHIFT = 0xA0 (160)
    "164",  // KEY_LALT     VK_LMENU = 0xA4 (164)
    
    // Special keys
    "32",   // KEY_SPACE    VK_SPACE = 0x20 (32)
    "9",    // KEY_TAB      VK_TAB = 0x09 (9)
    "20",   // KEY_CAPSLOCK VK_CAPITAL = 0x14 (20)
    "46",   // KEY_DEL      VK_DELETE = 0x2E (46)
    "36",   // KEY_HOME     VK_HOME = 0x24 (36)
    "35",   // KEY_END      VK_END = 0x23 (35)
    "33",   // KEY_PGUP     VK_PRIOR = 0x21 (33)
    "34",   // KEY_PGDN     VK_NEXT = 0x22 (34)
    "13",   // KEY_RETURN   VK_RETURN = 0x0D (13)
    "padenter",   // KEY_ENT      (numpad enter)
};

// ============================================================================
// MOUSE SPRITE NAMES
// ============================================================================

static const char* const g_MouseSpriteNames[MOUSE_COUNT] = {
    "1",              // MOUSE_LMB (VK_LBUTTON = 0x01)
    "2",              // MOUSE_RMB (VK_RBUTTON = 0x02)
    "4",              // MOUSE_MMB (VK_MBUTTON = 0x04)
    "MWHU",           // MOUSE_WHEEL_UP
    "MWHD",           // MOUSE_WHEEL_DOWN
    "5",              // MOUSE_BUTTON_4 (VK_XBUTTON1 = 0x05)
    "6"               // MOUSE_BUTTON_5 (VK_XBUTTON2 = 0x06)
};

// ============================================================================
// INTERNAL STATE
// ============================================================================

static bool g_Enabled = true;
static bool g_TexturesLoaded = false;
static int g_TxdSlot = -1;

// ============================================================================
// ARRAY EXPANSION
// ============================================================================

static void ExpandButtonSpriteArray() {
    CSprite2d* originalArray = reinterpret_cast<CSprite2d*>(0xC71AD8);
    memcpy(g_ExtendedSprites, originalArray, 15 * sizeof(CSprite2d));
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
    
    // Load keyboard sprites
    for (int i = 0; i < KEYBOARD_COUNT; i++) {
        g_ExtendedSprites[KEYBOARD_SPRITE_BASE + i].SetTexture(const_cast<char*>(g_KeyboardSpriteNames[i]));
    }
    
    // Load mouse sprites
    for (int i = 0; i < MOUSE_COUNT; i++) {
        g_ExtendedSprites[MOUSE_SPRITE_BASE + i].SetTexture(const_cast<char*>(g_MouseSpriteNames[i]));
    }
    
    CTxdStore::PopCurrentTxd();
    g_TexturesLoaded = true;
}

static void UnloadTextures() {
    if (!g_TexturesLoaded) return;
    
    for (int i = 0; i < KEYBOARD_COUNT; i++) {
        g_ExtendedSprites[KEYBOARD_SPRITE_BASE + i].Delete();
    }
    
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
// TOKEN TO SPRITE INDEX MAPPING
// We use two-digit hex-like tokens: ~Kxx~ where xx is 00-99
// This allows up to 100 keyboard sprites
// ============================================================================

static int ParseKeyboardToken(const char* text) {
    // Format: ~Kxx~ where xx is two digits
    if (text[0] != '~' || text[1] != 'K' || text[4] != '~') {
        return -1;
    }
    
    char d1 = text[2];
    char d2 = text[3];
    
    if (d1 >= '0' && d1 <= '9' && d2 >= '0' && d2 <= '9') {
        return (d1 - '0') * 10 + (d2 - '0');
    }
    
    return -1;
}

// ============================================================================
// HOOK: CFont::ParseToken
// ============================================================================

using ParseToken_t = char*(__cdecl*)(char*, CRGBA&, bool, char*);
static ParseToken_t ParseToken_Original = reinterpret_cast<ParseToken_t>(0x718F00);
static uint8_t* g_PS2Symbol = reinterpret_cast<uint8_t*>(0xC71A54);

char* __cdecl ParseToken_Hooked(char* text, CRGBA& color, bool isBlip, char* tag) {
    if (!text || !g_Enabled || !g_TexturesLoaded) {
        return ParseToken_Original(text, color, isBlip, tag);
    }
    
    // Check for keyboard token: ~Kxx~
    if (text[0] == '~' && text[1] == 'K') {
        int keyIndex = ParseKeyboardToken(text);
        if (keyIndex >= 0 && keyIndex < KEYBOARD_COUNT) {
            *g_PS2Symbol = static_cast<uint8_t>(KEYBOARD_SPRITE_BASE + keyIndex);
            return text + 5;  // Skip ~Kxx~
        }
    }
    
    // Check for mouse token: ~Mxx~
    if (text[0] == '~' && text[1] == 'M' && text[4] == '~') {
        char d1 = text[2];
        char d2 = text[3];
        if (d1 == '0' && d2 >= '0' && d2 <= '6') {
            int mouseIndex = d2 - '0';
            *g_PS2Symbol = static_cast<uint8_t>(MOUSE_SPRITE_BASE + mouseIndex);
            return text + 5;  // Skip ~Mxx~
        }
    }
    
    return ParseToken_Original(text, color, isBlip, tag);
}

// ============================================================================
// TEXT REPLACEMENT MAPPINGS
// ============================================================================

using ProcessStringToDisplay_t = short(__cdecl*)(float x, float y, const char* text);
static ProcessStringToDisplay_t ProcessStringToDisplay_Orig = 
    reinterpret_cast<ProcessStringToDisplay_t>(0x71A600);

struct TextMapping {
    const char* text;
    const char* token;
};

// Mouse text → token mappings
static const TextMapping g_MouseMappings[] = {
    { "LMB", "~M00~" },
    { "RMB", "~M01~" },
    { "MMB", "~M02~" },
    { "MSWHEELUP", "~M03~" },
    { "MS WHEEL UP", "~M03~" },
    { "MSWHEELDOWN", "~M04~" },
    { "MS WHEEL DN", "~M04~" },
    { "MXB1", "~M05~" },
    { "MXB2", "~M06~" },
};

// Keyboard text → token mappings (index = KEY_xxx enum value)
// Format: ~Kxx~ where xx is the two-digit index
static const TextMapping g_KeyboardMappings[] = {
    // Movement (matching KEY_xxx order)
    { "W", "~K00~" },
    { "A", "~K01~" },
    { "S", "~K02~" },
    { "D", "~K03~" },
    { "UP", "~K04~" },
    { "DOWN", "~K05~" },
    { "LEFT", "~K06~" },
    { "RIGHT", "~K07~" },
    
    // Letters
    { "E", "~K08~" },
    { "Q", "~K09~" },
    { "F", "~K10~" },
    { "G", "~K11~" },
    { "H", "~K12~" },
    { "N", "~K13~" },
    { "Y", "~K14~" },
    { "X", "~K15~" },
    { "Z", "~K16~" },
    { "V", "~K17~" },
    { "C", "~K18~" },
    
    // Numpad
    { "NUM0", "~K19~" },
    { "NUM1", "~K20~" },
    { "NUM2", "~K21~" },
    { "NUM3", "~K22~" },
    { "NUM4", "~K23~" },
    { "NUM5", "~K24~" },
    { "NUM6", "~K25~" },
    { "NUM7", "~K26~" },
    { "NUM8", "~K27~" },
    { "NUM9", "~K28~" },
    { "NUM .", "~K29~" },
    { "NUM.", "~K29~" },
    
    // Modifiers
    { "LCTRL", "~K30~" },
    { "RCTRL", "~K31~" },
    { "LSHIFT", "~K32~" },
    { "LALT", "~K33~" },
    
    // Special keys
    { "SPACE", "~K34~" },
    { "TAB", "~K35~" },
    { "CAPSLOCK", "~K36~" },
    { "DEL", "~K37~" },
    { "HOME", "~K38~" },
    { "END", "~K39~" },
    { "PGUP", "~K40~" },
    { "PGDN", "~K41~" },
    { "RETURN", "~K42~" },
    { "ENT", "~K43~" },
};

static char g_TextBuffer[2048];

// Helper to replace text in buffer
static void DoReplace(char* dest, size_t destSize, const char* searchText, const char* replaceToken, bool wholeWordOnly) {
    size_t textLen = strlen(searchText);
    size_t tokenLen = strlen(replaceToken);
    
    char* found = strstr(dest, searchText);
    while (found) {
        bool isWholeWord = true;
        if (wholeWordOnly) {
            if (found > dest && isalnum(*(found - 1))) {
                isWholeWord = false;
            }
            char afterChar = *(found + textLen);
            if (isalnum(afterChar)) {
                isWholeWord = false;
            }
        }
        
        if (isWholeWord) {
            size_t remaining = strlen(found + textLen) + 1;
            size_t offset = found - dest;
            
            if (offset + tokenLen + remaining <= destSize) {
                memmove(found + tokenLen, found + textLen, remaining);
                memcpy(found, replaceToken, tokenLen);
                found = strstr(found + tokenLen, searchText);
            } else {
                found = strstr(found + 1, searchText);
            }
        } else {
            found = strstr(found + 1, searchText);
        }
    }
}

static void ReplaceText(char* dest, const char* src, size_t destSize) {
    strncpy(dest, src, destSize - 1);
    dest[destSize - 1] = '\0';
    
    // Apply mouse mappings (unique strings, no whole-word check)
    for (const auto& mapping : g_MouseMappings) {
        DoReplace(dest, destSize, mapping.text, mapping.token, false);
    }
    
    // Apply keyboard mappings (need whole-word check for single letters)
    for (const auto& mapping : g_KeyboardMappings) {
        bool wholeWord = strlen(mapping.text) <= 2; // Single/double char needs whole-word
        DoReplace(dest, destSize, mapping.text, mapping.token, wholeWord);
    }
}

void __cdecl PrintString_Hooked(float x, float y, const char* text) {
    if (!text || *text == '\0' || *text == '*') {
        return;
    }
    
    const char* textToRender = text;
    
    if (g_Enabled && g_TexturesLoaded) {
        // Check if any replacement might be needed
        bool needsReplacement = 
            strstr(text, "LMB") || strstr(text, "RMB") || strstr(text, "MMB") ||
            strstr(text, "MXB") || strstr(text, "MSWHEEL") || strstr(text, "MS WHEEL") ||
            strstr(text, "NUM") || strstr(text, "CTRL") || strstr(text, "SHIFT") ||
            strstr(text, "ALT") || strstr(text, "SPACE") || strstr(text, "TAB") ||
            strstr(text, "CAPS") || strstr(text, "DEL") || strstr(text, "HOME") ||
            strstr(text, "END") || strstr(text, "PG") || strstr(text, "RETURN") ||
            strstr(text, "ENT") || strstr(text, "UP") || strstr(text, "DOWN") ||
            strstr(text, "LEFT") || strstr(text, "RIGHT");
        
        // Check for single letter keys
        if (!needsReplacement) {
            for (const char* p = text; *p; p++) {
                char c = *p;
                if ((c == 'W' || c == 'A' || c == 'S' || c == 'D' || 
                     c == 'E' || c == 'Q' || c == 'F' || c == 'G' ||
                     c == 'H' || c == 'N' || c == 'Y' || c == 'X' ||
                     c == 'Z' || c == 'V' || c == 'C') &&
                    (p == text || !isalnum(*(p-1))) &&
                    !isalnum(*(p+1))) {
                    needsReplacement = true;
                    break;
                }
            }
        }
        
        if (needsReplacement) {
            ReplaceText(g_TextBuffer, text, sizeof(g_TextBuffer));
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
    
    Events::initRwEvent += []() {
        LoadTextures();
    };
    
    Events::shutdownRwEvent += []() {
        UnloadTextures();
    };
    
    patch::RedirectCall(0x719965, ParseToken_Hooked);
    patch::RedirectCall(0x71A018, ParseToken_Hooked);
    patch::RedirectCall(0x71A2C4, ParseToken_Hooked);
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
