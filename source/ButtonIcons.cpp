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
// KEYBOARD SPRITE NAMES
// ============================================================================

static const char* const g_KeyboardSpriteNames[KEYBOARD_COUNT] = {
    "W", "A", "S", "D",           // Movement WASD
    "38", "40", "37", "39",       // Arrow keys
    "E", "Q", "F", "G", "H",      // Letter keys
    "N", "Y", "X", "Z", "V", "C",
    "96", "97", "98", "99",       // Numpad 0-9
    "100", "101", "102", "103",
    "104", "105", "110",          // Numpad dot
    "162", "163", "160", "164",   // Modifiers (Ctrl, Shift, Alt)
    "32", "9", "20",              // Space, Tab, CapsLock
    "46", "36", "35",             // Del, Home, End
    "33", "34", "13", "padenter"  // PgUp, PgDn, Enter, NumEnter
};

// ============================================================================
// MOUSE SPRITE NAMES
// ============================================================================

static const char* const g_MouseSpriteNames[MOUSE_COUNT] = {
    "1", "2", "4",                // LMB, RMB, MMB
    "MWHU", "MWHD",               // Wheel up/down
    "5", "6"                      // Extra buttons
};

// ============================================================================
// INTERNAL STATE
// ============================================================================

static bool g_Enabled = true;
static bool g_TexturesLoaded = false;
static int g_TxdSlot = -1;

// ============================================================================
// RSKEYCODES (from skeleton.h)
// ============================================================================

enum RsKeyCodes : int {
    rsNULL = 1056,
    rsUP = 1019, rsDOWN = 1020, rsLEFT = 1021, rsRIGHT = 1022,
    rsPADINS = 1038, rsPADEND = 1028, rsPADDOWN = 1029, rsPADPGDN = 1030,
    rsPADLEFT = 1031, rsPAD5 = 1032, rsPADRIGHT = 1034,
    rsPADHOME = 1035, rsPADUP = 1036, rsPADPGUP = 1037, rsPADDEL = 1027,
    rsLCTRL = 1049, rsRCTRL = 1050, rsLSHIFT = 1046, rsLALT = 1051,
    rsTAB = 1043, rsCAPSLK = 1044, rsDEL = 1014,
    rsHOME = 1015, rsEND = 1016, rsPGUP = 1017, rsPGDN = 1018,
    rsENTER = 1045, rsPADENTER = 1039,
    rsMOUSE_LEFT_BUTTON = 1, rsMOUSE_MIDDLE_BUTTON = 2, rsMOUSE_RIGHT_BUTTON = 3,
    rsMOUSE_WHEEL_UP_BUTTON = 4, rsMOUSE_WHEEL_DOWN_BUTTON = 5,
    rsMOUSE_X1_BUTTON = 6, rsMOUSE_X2_BUTTON = 7,
};

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
    
    for (int i = 0; i < KEYBOARD_COUNT; i++) {
        g_ExtendedSprites[KEYBOARD_SPRITE_BASE + i].SetTexture(const_cast<char*>(g_KeyboardSpriteNames[i]));
    }
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
// KEY CODE TO SPRITE TOKEN MAPPING
// ============================================================================

static const char* GetSpriteTokenForKeyCode(unsigned int keyCode) {
    // ASCII letter keys
    switch (keyCode) {
        case 'W': case 'w': return "~K00~";
        case 'A': case 'a': return "~K01~";
        case 'S': case 's': return "~K02~";
        case 'D': case 'd': return "~K03~";
        case 'E': case 'e': return "~K08~";
        case 'Q': case 'q': return "~K09~";
        case 'F': case 'f': return "~K10~";
        case 'G': case 'g': return "~K11~";
        case 'H': case 'h': return "~K12~";
        case 'N': case 'n': return "~K13~";
        case 'Y': case 'y': return "~K14~";
        case 'X': case 'x': return "~K15~";
        case 'Z': case 'z': return "~K16~";
        case 'V': case 'v': return "~K17~";
        case 'C': case 'c': return "~K18~";
        case ' ': return "~K34~";
    }
    
    // Non-ASCII keys (RsKeyCodes)
    switch (keyCode) {
        case rsUP:    return "~K04~";
        case rsDOWN:  return "~K05~";
        case rsLEFT:  return "~K06~";
        case rsRIGHT: return "~K07~";
        case rsPADINS:   return "~K19~";
        case rsPADEND:   return "~K20~";
        case rsPADDOWN:  return "~K21~";
        case rsPADPGDN:  return "~K22~";
        case rsPADLEFT:  return "~K23~";
        case rsPAD5:     return "~K24~";
        case rsPADRIGHT: return "~K25~";
        case rsPADHOME:  return "~K26~";
        case rsPADUP:    return "~K27~";
        case rsPADPGUP:  return "~K28~";
        case rsPADDEL:   return "~K29~";
        case rsLCTRL:  return "~K30~";
        case rsRCTRL:  return "~K31~";
        case rsLSHIFT: return "~K32~";
        case rsLALT:   return "~K33~";
        case rsTAB:      return "~K35~";
        case rsCAPSLK:   return "~K36~";
        case rsDEL:      return "~K37~";
        case rsHOME:     return "~K38~";
        case rsEND:      return "~K39~";
        case rsPGUP:     return "~K40~";
        case rsPGDN:     return "~K41~";
        case rsENTER:    return "~K42~";
        case rsPADENTER: return "~K43~";
        default: return nullptr;
    }
}

static const char* GetSpriteTokenForMouseCode(unsigned int mouseCode) {
    switch (mouseCode) {
        case rsMOUSE_LEFT_BUTTON:       return "~M00~";
        case rsMOUSE_RIGHT_BUTTON:      return "~M01~";
        case rsMOUSE_MIDDLE_BUTTON:     return "~M02~";
        case rsMOUSE_WHEEL_UP_BUTTON:   return "~M03~";
        case rsMOUSE_WHEEL_DOWN_BUTTON: return "~M04~";
        case rsMOUSE_X1_BUTTON:         return "~M05~";
        case rsMOUSE_X2_BUTTON:         return "~M06~";
        default: return nullptr;
    }
}

// ============================================================================
// TOKEN PARSING
// ============================================================================

static int ParseKeyboardToken(const char* text) {
    if (text[0] != '~' || text[1] != 'K' || text[4] != '~') return -1;
    char d1 = text[2], d2 = text[3];
    if (d1 >= '0' && d1 <= '9' && d2 >= '0' && d2 <= '9') {
        return (d1 - '0') * 10 + (d2 - '0');
    }
    return -1;
}

// ============================================================================
// GetStringWidth HOOK
// ============================================================================

using GetStringLength_t = int(__cdecl*)(const char*);
static GetStringLength_t CMessages_GetStringLength = reinterpret_cast<GetStringLength_t>(0x69DB50);

using GetCharacterSize_t = float(__cdecl*)(uint8_t);
static GetCharacterSize_t CFont_GetCharacterSize = reinterpret_cast<GetCharacterSize_t>(0x719750);

using GetScriptLetterSize_t = float(__cdecl*)(uint8_t);
static GetScriptLetterSize_t CFont_GetScriptLetterSize = reinterpret_cast<GetScriptLetterSize_t>(0x719670);

static float* g_FontScaleY = reinterpret_cast<float*>(0xC71A68);

float __cdecl GetStringWidth_Reimplemented(const char* string, bool full, bool scriptText) {
    int len = CMessages_GetStringLength(string);
    if (len <= 0) return 0.0f;
    
    float width = 0.0f;
    bool lastWasTag = false, lastWasLetter = false;
    const char* pStr = string;
    
    while (*pStr != '\0') {
        if (*pStr == ' ' && !full) break;
        
        if (*pStr == '~') {
            if (!full && (lastWasTag || lastWasLetter)) return width;
            
            const char* next = pStr + 1;
            
            // Check for our sprite tokens: ~Kxx~ or ~Mxx~
            if ((*next == 'K' || *next == 'M') && 
                next[1] >= '0' && next[1] <= '9' &&
                next[2] >= '0' && next[2] <= '9' &&
                next[3] == '~') {
                width += 17.0f * (*g_FontScaleY);
                pStr = next + 4;
            } else {
                if (*next != '~') {
                    for (; *next && *next != '~'; next++);
                }
                pStr = next + 1;
            }
            
            if (lastWasLetter || *pStr == '~') lastWasTag = true;
        } else {
            if (!full && *pStr == ' ' && lastWasTag) return width;
            
            uint8_t upper = static_cast<uint8_t>(*pStr) - 0x20;
            pStr++;
            
            width += scriptText ? CFont_GetScriptLetterSize(upper) : CFont_GetCharacterSize(upper);
            lastWasLetter = true;
        }
    }
    return width;
}

// ============================================================================
// GetControllerSettingTextKeyBoard HOOK (0x52FE10)
// ============================================================================

static char* g_KeyNameBuffer = reinterpret_cast<char*>(0xB714BC);
static char* g_NumberBuffer = reinterpret_cast<char*>(0xB7149C);
static int* g_TextLanguage = reinterpret_cast<int*>(0xBA67C8);

// Wrapper for CText::Get
static char* CText_Get(const char* key) {
    using CText_Get_t = char*(__thiscall*)(void*, const char*);
    static CText_Get_t CText_Get_Raw = reinterpret_cast<CText_Get_t>(0x6A0050);
    static void* TheText = reinterpret_cast<void*>(0xC1B340);
    
    return CText_Get_Raw(TheText, key);
}

using InsertNumberInString_t = void(__cdecl*)(char*, int, int, int, int, int, int, char*);
static InsertNumberInString_t CMessages_InsertNumberInString = reinterpret_cast<InsertNumberInString_t>(0x69DE90);

static char g_SpriteTokenBuffer[8];
static void* g_ControllerThis;

static char* __cdecl GetControllerSettingTextKeyBoard_Impl(int action, int type) {
    void* thisPtr = g_ControllerThis;
    memset(g_KeyNameBuffer, 0, 0x30);
    
    // Sanity checks
    if (!thisPtr || thisPtr == reinterpret_cast<void*>(0xFFFFFFFF)) return nullptr;
    if (action < 0 || action > 58 || type < 0 || type > 3) return nullptr;
    
    unsigned int* thisAsInt = reinterpret_cast<unsigned int*>(thisPtr);
    unsigned int keyCode = thisAsInt[8 * action + 2 * type + 0x2DC];
    
    if (keyCode == 0 || keyCode == 1056) return nullptr;  // Unbound
    
    // Non-ASCII keys (>= 256)
    if (keyCode >= 0x100) {
        const char* token = GetSpriteTokenForKeyCode(keyCode);
        if (token && g_Enabled && g_TexturesLoaded) {
            strcpy(g_SpriteTokenBuffer, token);
            return g_SpriteTokenBuffer;
        }
        
        // F1-F12 keys
        if (keyCode >= 0x3E9 && keyCode <= 0x3F4) {
            char* fncText = CText_Get("FEC_FNC");
            if (fncText) {
                CMessages_InsertNumberInString(fncText, keyCode - 1000, -1, -1, -1, -1, -1, g_NumberBuffer);
                return g_NumberBuffer;
            }
            // Fallback if CText_Get returns invalid
            sprintf(g_KeyNameBuffer, "F%d", keyCode - 1000);
            return g_KeyNameBuffer;
        }
        
        // Other special keys - return GXT text
        switch (keyCode) {
            case 0x3F5: return CText_Get( "FEC_IRT");
            case 0x3F6: return CText_Get( "FEC_DLL");
            case 0x3F7: return CText_Get( "FEC_HME");
            case 0x3F8: return CText_Get( "FEC_END");
            case 0x3F9: return CText_Get( "FEC_PGU");
            case 0x3FA: return CText_Get( "FEC_PGD");
            case 0x3FB: return CText_Get( "FEC_UPA");
            case 0x3FC: return CText_Get( "FEC_DWA");
            case 0x3FD: return CText_Get( "FEC_LFA");
            case 0x3FE: return CText_Get( "FEC_RFA");
            case 0x3FF: return CText_Get( "FEC_FWS");
            case 0x400: return CText_Get( "FECSTAR");
            case 0x401: return CText_Get( "FEC_PLS");
            case 0x402: return CText_Get( "FEC_MIN");
            case 0x403: return CText_Get( "FEC_DOT");
            case 0x404: case 0x405: case 0x406: case 0x407: case 0x408:
            case 0x40A: case 0x40B: case 0x40C: case 0x40D: case 0x40E: {
                char* nmn = CText_Get( "FEC_NMN");
                if (nmn) {
                    int num = (keyCode == 0x40E) ? 0 : (keyCode - 0x403);
                    CMessages_InsertNumberInString(nmn, num, -1, -1, -1, -1, -1, g_NumberBuffer);
                    return g_NumberBuffer;
                }
                return nullptr;
            }
            case 0x409: return CText_Get("FEC_NLK");
            case 0x40F: return CText_Get("FEC_ETR");
            case 0x410: return CText_Get("FEC_SLK");
            case 0x411: return CText_Get("FEC_PSB");
            case 0x412: return CText_Get("FEC_BSP");
            case 0x413: return CText_Get("FEC_TAB");
            case 0x414: return CText_Get("FEC_CLK");
            case 0x415: return CText_Get("FEC_RTN");
            case 0x416: return CText_Get("FEC_LSF");
            case 0x417: return CText_Get("FEC_RSF");
            case 0x418: return CText_Get("FEC_SFT");
            case 0x419: return CText_Get("FEC_LCT");
            case 0x41A: return CText_Get("FEC_RCT");
            case 0x41B: return CText_Get("FEC_LAL");
            case 0x41C: return CText_Get("FEC_RAL");
            case 0x41D: return CText_Get("FEC_LWD");
            case 0x41E: return CText_Get("FEC_RWD");
            case 0x41F: return CText_Get("FEC_WRC");
            default: return nullptr;
        }
    }
    
    // ASCII keys
    if (keyCode == '*') return CText_Get("FEC_AST");
    if (keyCode == '^' && *g_TextLanguage == 2) {
        g_KeyNameBuffer[0] = '|';
        g_KeyNameBuffer[1] = '\0';
        return g_KeyNameBuffer;
    }
    if (keyCode == 0xB2 && *g_TextLanguage == 1) {
        g_KeyNameBuffer[0] = '2';
        g_KeyNameBuffer[1] = '\0';
        return g_KeyNameBuffer;
    }
    
    // Check for sprite token
    const char* token = GetSpriteTokenForKeyCode(keyCode);
    if (token && g_Enabled && g_TexturesLoaded) {
        strcpy(g_SpriteTokenBuffer, token);
        return g_SpriteTokenBuffer;
    }
    
    // Fallback to character
    char charCode = static_cast<char>(keyCode);
    if (!charCode) charCode = '#';
    g_KeyNameBuffer[0] = charCode;
    g_KeyNameBuffer[1] = '\0';
    return g_KeyNameBuffer;
}

// Naked thunk - matches __thiscall with ret 8
__declspec(naked) void GetControllerSettingTextKeyBoard_Thunk() {
    __asm {
        mov g_ControllerThis, ecx
        mov eax, [esp+8]
        push eax
        mov eax, [esp+8]
        push eax
        call GetControllerSettingTextKeyBoard_Impl
        add esp, 8
        ret 8
    }
}

// ============================================================================
// ParseToken HOOK
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
            return text + 5;
        }
    }
    
    // Check for mouse token: ~Mxx~
    if (text[0] == '~' && text[1] == 'M' && text[4] == '~') {
        char d1 = text[2], d2 = text[3];
        if (d1 == '0' && d2 >= '0' && d2 <= '6') {
            *g_PS2Symbol = static_cast<uint8_t>(MOUSE_SPRITE_BASE + (d2 - '0'));
            return text + 5;
        }
    }
    
    return ParseToken_Original(text, color, isBlip, tag);
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
    
    // Replace GetControllerSettingTextKeyBoard entirely (0x52FE10)
    // This handles all ~k~~ACTION~ replacements automatically via game's InsertPlayerControlKeysInString
    patch::RedirectJump(0x52FE10, GetControllerSettingTextKeyBoard_Thunk);
    
    // Hook GetStringWidth to measure sprite tokens
    patch::RedirectCall(0x47B565, GetStringWidth_Reimplemented);
    patch::RedirectCall(0x47B73A, GetStringWidth_Reimplemented);
    patch::RedirectCall(0x57A49B, GetStringWidth_Reimplemented);
    patch::RedirectCall(0x57FB52, GetStringWidth_Reimplemented);
    patch::RedirectCall(0x57FE35, GetStringWidth_Reimplemented);
    patch::RedirectCall(0x5814A7, GetStringWidth_Reimplemented);
    patch::RedirectCall(0x581512, GetStringWidth_Reimplemented);
    patch::RedirectCall(0x58BCCC, GetStringWidth_Reimplemented);
    
    // Hook ParseToken to render our sprite tokens (~Kxx~, ~Mxx~)
    patch::RedirectCall(0x719965, ParseToken_Hooked);
    patch::RedirectCall(0x71A018, ParseToken_Hooked);
    patch::RedirectCall(0x71A2C4, ParseToken_Hooked);
}

// ============================================================================
// PUBLIC API
// ============================================================================

bool IsEnabled() { return g_Enabled; }
void SetEnabled(bool enabled) { g_Enabled = enabled; }

void ReloadTextures() {
    UnloadTextures();
    LoadTextures();
}

} // namespace ButtonIcons
