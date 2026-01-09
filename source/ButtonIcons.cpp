#include "ButtonIcons.h"

#include <plugin.h>
#include <Events.h>
#include <CFont.h>
#include <CSprite2d.h>
#include <CTxdStore.h>
#include <RenderWare.h>

#include <cstring>
#include <cstdio>
#include <intrin.h>  // For _ReturnAddress()

using namespace plugin;

namespace ButtonIcons {

// ============================================================================
// EXTENDED SPRITE ARRAY
// ============================================================================
// Layout:
// 0:       Unused (PS2Symbol=0 means no sprite)
// 1-14:    PS2 controller buttons (original game) - DO NOT USE
// 15-82:   Keyboard keys (68 keys)
// 83-89:   Mouse buttons (7 buttons)
// 90-95:   Reserved
// ============================================================================

static const int MAX_EXTENDED_SPRITES = 96;
static CSprite2d g_ExtendedSprites[MAX_EXTENDED_SPRITES];
static float g_ExtendedSpriteWidths[MAX_EXTENDED_SPRITES];
static float g_SymbolSpriteWidth = 17.0f;
static unsigned int g_TokenWidth = 3;  // Token length for GetNumberLines sub esi fix

// Start at 15 to avoid ALL original PS2 symbol indices (1-14)
static const int KEYBOARD_SPRITE_BASE = 15;
static const int MOUSE_SPRITE_BASE = KEYBOARD_SPRITE_BASE + KEYBOARD_COUNT;  // 15 + 68 = 83

// ============================================================================
// KEYBOARD SPRITE NAMES
// ============================================================================

static const char* const g_KeyboardSpriteNames[KEYBOARD_COUNT] = {
    "W", "A", "S", "D",           // Movement WASD (0-3)
    "38", "40", "37", "39",       // Arrow keys (4-7)
    "E", "Q", "F", "G", "H",      // Letter keys (8-12)
    "N", "Y", "X", "Z", "V", "C", // Letter keys (13-18)
    "B", "I", "J", "K", "L", "M", // Letter keys (19-24)
    "O", "P", "R", "T", "U",      // Letter keys (25-29)
    "96", "97", "98", "99",       // Numpad 0-9 (30-33)
    "100", "101", "102", "103",   // Numpad 4-7 (34-37)
    "104", "105", "110",          // Numpad 8-9, dot (38-40)
    "162", "163", "160", "164",   // Modifiers (41-44)
    "32", "9", "20",              // Space, Tab, CapsLock (45-47)
    "46", "36", "35",             // Del, Home, End (48-50)
    "33", "34", "13", "padenter", // PgUp, PgDn, Enter, NumEnter (51-54)
    "48", "49", "50", "51", "52", // Number row 0-4 (55-59)
    "53", "54", "55", "56", "57", // Number row 5-9 (60-64)
    "107", "106", "109"           // NUM+, NUM*, NUM- (65-67)
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
    // Copy original sprite array (15 PS2 button sprites)
    CSprite2d* originalArray = reinterpret_cast<CSprite2d*>(0xC71AD8);
    memcpy(g_ExtendedSprites, originalArray, 15 * sizeof(CSprite2d));

    // Copy original sprite widths (game stores these at 0xC71A90)
    float* originalWidths = reinterpret_cast<float*>(0xC71A90);
    memcpy(g_ExtendedSpriteWidths, originalWidths, 15 * sizeof(float));

    // Initialize extended sprite widths to default
    for (int i = 15; i < MAX_EXTENDED_SPRITES; i++) {
        g_ExtendedSpriteWidths[i] = 17.0f;
    }

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
        int idx = KEYBOARD_SPRITE_BASE + i;
        g_ExtendedSprites[idx].SetTexture(const_cast<char*>(g_KeyboardSpriteNames[i]));
    }
    // Load mouse sprites
    for (int i = 0; i < MOUSE_COUNT; i++) {
        int idx = MOUSE_SPRITE_BASE + i;
        g_ExtendedSprites[idx].SetTexture(const_cast<char*>(g_MouseSpriteNames[i]));
    }

    // Calculate widths based on texture aspect ratio (like GInput does)
    for (int i = KEYBOARD_SPRITE_BASE; i < MOUSE_SPRITE_BASE + MOUSE_COUNT; i++) {
        RwTexture* tex = g_ExtendedSprites[i].m_pTexture;
        if (tex) {
            RwRaster* raster = RwTextureGetRaster(tex);
            if (raster) {
                int width = RwRasterGetWidth(raster);
                int height = RwRasterGetHeight(raster);
                if (height > 0) {
                    float aspectRatio = static_cast<float>(width) / static_cast<float>(height);
                    g_ExtendedSpriteWidths[i] = 17.0f * aspectRatio;
                }
            }
        }
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
    // ASCII letter keys (all A-Z)
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
        case 'B': case 'b': return "~K19~";
        case 'I': case 'i': return "~K20~";
        case 'J': case 'j': return "~K21~";
        case 'K': case 'k': return "~K22~";
        case 'L': case 'l': return "~K23~";
        case 'M': case 'm': return "~K24~";
        case 'O': case 'o': return "~K25~";
        case 'P': case 'p': return "~K26~";
        case 'R': case 'r': return "~K27~";
        case 'T': case 't': return "~K28~";
        case 'U': case 'u': return "~K29~";
        case ' ': return "~K45~";
        // Number row keys
        case '0': return "~K55~";
        case '1': return "~K56~";
        case '2': return "~K57~";
        case '3': return "~K58~";
        case '4': return "~K59~";
        case '5': return "~K60~";
        case '6': return "~K61~";
        case '7': return "~K62~";
        case '8': return "~K63~";
        case '9': return "~K64~";
    }
    
    // Non-ASCII keys (RsKeyCodes)
    switch (keyCode) {
        case rsUP:    return "~K04~";
        case rsDOWN:  return "~K05~";
        case rsLEFT:  return "~K06~";
        case rsRIGHT: return "~K07~";
        case rsPADINS:   return "~K30~";
        case rsPADEND:   return "~K31~";
        case rsPADDOWN:  return "~K32~";
        case rsPADPGDN:  return "~K33~";
        case rsPADLEFT:  return "~K34~";
        case rsPAD5:     return "~K35~";
        case rsPADRIGHT: return "~K36~";
        case rsPADHOME:  return "~K37~";
        case rsPADUP:    return "~K38~";
        case rsPADPGUP:  return "~K39~";
        case rsPADDEL:   return "~K40~";
        case rsLCTRL:  return "~K41~";
        case rsRCTRL:  return "~K42~";
        case rsLSHIFT: return "~K43~";
        case rsLALT:   return "~K44~";
        case rsTAB:      return "~K46~";
        case rsCAPSLK:   return "~K47~";
        case rsDEL:      return "~K48~";
        case rsHOME:     return "~K49~";
        case rsEND:      return "~K50~";
        case rsPGUP:     return "~K51~";
        case rsPGDN:     return "~K52~";
        case rsENTER:    return "~K53~";
        case rsPADENTER: return "~K54~";
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
// Font scale Y pointer (used by AddTokenToWidth)
// ============================================================================

static float* g_FontScaleY = reinterpret_cast<float*>(0xC71A68);

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
            sprintf(g_KeyNameBuffer, "F%d", keyCode - 1000);
            return g_KeyNameBuffer;
        }
        
        // Numpad operators - check for sprite token first
        if (keyCode == 0x400 && g_Enabled && g_TexturesLoaded) { strcpy(g_SpriteTokenBuffer, "~K66~"); return g_SpriteTokenBuffer; }  // NUM*
        if (keyCode == 0x401 && g_Enabled && g_TexturesLoaded) { strcpy(g_SpriteTokenBuffer, "~K65~"); return g_SpriteTokenBuffer; }  // NUM+
        if (keyCode == 0x402 && g_Enabled && g_TexturesLoaded) { strcpy(g_SpriteTokenBuffer, "~K67~"); return g_SpriteTokenBuffer; }  // NUM-
        
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
// GetControllerSettingTextMouse HOOK (0x52F390)
// ============================================================================

// GetMouseButtonAssociatedWithAction at 0x52F580
using GetMouseButton_t = unsigned int(__thiscall*)(void*, int);
static GetMouseButton_t GetMouseButtonAssociatedWithAction = reinterpret_cast<GetMouseButton_t>(0x52F580);

static char g_MouseTokenBuffer[8];

static char* __cdecl GetControllerSettingTextMouse_Impl(int action) {
    void* thisPtr = g_ControllerThis;
    
    if (!thisPtr || thisPtr == reinterpret_cast<void*>(0xFFFFFFFF)) return nullptr;
    if (action < 0 || action > 58) return nullptr;
    
    unsigned int mouseCode = GetMouseButtonAssociatedWithAction(thisPtr, action);
    if (mouseCode == 0) return nullptr;  // Unbound
    
    // Return sprite token if enabled
    const char* token = GetSpriteTokenForMouseCode(mouseCode);
    if (token && g_Enabled && g_TexturesLoaded) {
        strcpy(g_MouseTokenBuffer, token);
        return g_MouseTokenBuffer;
    }
    
    // Fallback to GXT text
    switch (mouseCode) {
        case rsMOUSE_LEFT_BUTTON:       return CText_Get("FEC_MSL");
        case rsMOUSE_MIDDLE_BUTTON:     return CText_Get("FEC_MSM");
        case rsMOUSE_RIGHT_BUTTON:      return CText_Get("FEC_MSR");
        case rsMOUSE_WHEEL_UP_BUTTON:   return CText_Get("FEC_MWF");
        case rsMOUSE_WHEEL_DOWN_BUTTON: return CText_Get("FEC_MWB");
        case rsMOUSE_X1_BUTTON:         return CText_Get("FEC_MXO");
        case rsMOUSE_X2_BUTTON:         return CText_Get("FEC_MXT");
        default: return nullptr;
    }
}

// Naked thunk for mouse - __thiscall with ret 4 (only 1 parameter)
__declspec(naked) void GetControllerSettingTextMouse_Thunk() {
    __asm {
        mov g_ControllerThis, ecx
        mov eax, [esp+4]
        push eax
        call GetControllerSettingTextMouse_Impl
        add esp, 4
        ret 4
    }
}

// ============================================================================
// ParseToken HOOK
// ============================================================================

static uint8_t* g_PS2Symbol = reinterpret_cast<uint8_t*>(0xC71A54);

using ParseToken_t = char*(__cdecl*)(char*, CRGBA&, bool, char*);
static ParseToken_t ParseToken_Original = reinterpret_cast<ParseToken_t>(0x718F00);

char* __cdecl ParseToken_Hooked(char* text, CRGBA& color, bool isBlip, char* tag) {
    if (!text || !g_Enabled || !g_TexturesLoaded) {
        g_TokenWidth = 3;  // Default for original tokens
        return ParseToken_Original(text, color, isBlip, tag);
    }

    // Check for keyboard token: ~Kxx~
    if (text[0] == '~' && text[1] == 'K') {
        int keyIndex = ParseKeyboardToken(text);
        if (keyIndex >= 0 && keyIndex < KEYBOARD_COUNT) {
            uint8_t spriteIdx = static_cast<uint8_t>(KEYBOARD_SPRITE_BASE + keyIndex);
            *g_PS2Symbol = spriteIdx;
            g_SymbolSpriteWidth = g_ExtendedSpriteWidths[spriteIdx];
            g_TokenWidth = 5;  // Our tokens are 5 chars: ~Kxx~
            return text + 5;
        }
    }

    // Check for mouse token: ~Mxx~
    if (text[0] == '~' && text[1] == 'M' && text[4] == '~') {
        char d1 = text[2], d2 = text[3];
        if (d1 == '0' && d2 >= '0' && d2 <= '6') {
            uint8_t spriteIdx = static_cast<uint8_t>(MOUSE_SPRITE_BASE + (d2 - '0'));
            *g_PS2Symbol = spriteIdx;
            g_SymbolSpriteWidth = g_ExtendedSpriteWidths[spriteIdx];
            g_TokenWidth = 5;  // Our tokens are 5 chars: ~Mxx~
            return text + 5;
        }
    }

    // For original game tokens, let original handler deal with it
    g_TokenWidth = 3;  // Original tokens are 3 chars: ~x~
    char* result = ParseToken_Original(text, color, isBlip, tag);
    if (*g_PS2Symbol != 0) {
        g_SymbolSpriteWidth = g_ExtendedSpriteWidths[*g_PS2Symbol];
    }
    return result;
}

// ============================================================================
// MINIMUM SIZE ENFORCEMENT - Hook for CSprite2d::Draw
// ============================================================================

// Minimum sprite height (in screen pixels)
static const float MIN_SPRITE_HEIGHT = 22.0f;

// Original Draw function
using CSprite2d_Draw_t = void(__thiscall*)(CSprite2d*, const CRect&, const CRGBA&);
static CSprite2d_Draw_t CSprite2d_Draw_Original = reinterpret_cast<CSprite2d_Draw_t>(0x728350);

// Hook for CSprite2d::Draw called from PrintChar (0x718AE5)
// Enforces minimum size and crisp filtering for our extended sprites
void __fastcall ButtonSprite_Draw_Hook(CSprite2d* sprite, void* edx, const CRect& rect, const CRGBA& color) {
    // Check if this sprite is one of our extended sprites (index >= 15)
    // by comparing the sprite pointer to our array
    ptrdiff_t offset = reinterpret_cast<uintptr_t>(sprite) - reinterpret_cast<uintptr_t>(g_ExtendedSprites);
    int spriteIndex = static_cast<int>(offset / sizeof(CSprite2d));

    // Only apply minimum size to our extended sprites (keyboard/mouse icons)
    if (spriteIndex >= KEYBOARD_SPRITE_BASE && spriteIndex < MAX_EXTENDED_SPRITES) {
        float currentHeight = rect.bottom - rect.top;

        if (currentHeight < MIN_SPRITE_HEIGHT && currentHeight > 0.0f) {
            // Get aspect ratio from our stored widths
            float aspectRatio = g_ExtendedSpriteWidths[spriteIndex] / 17.0f;

            // Create enlarged rect, keeping top-left position
            float newHeight = MIN_SPRITE_HEIGHT;
            float newWidth = newHeight * aspectRatio;

            CRect enlargedRect;
            enlargedRect.left = rect.left;
            enlargedRect.top = rect.top;
            enlargedRect.right = rect.left + newWidth;
            enlargedRect.bottom = rect.top + newHeight;

            CSprite2d_Draw_Original(sprite, enlargedRect, color);
            return;
        }
    }

    // Default: call original with unmodified rect
    CSprite2d_Draw_Original(sprite, rect, color);
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
// AddTokenToWidth HOOK - For proper text width calculation
// ============================================================================

// This is called during text width/layout calculation to account for sprite width
char* __stdcall AddTokenToWidth(char* pText, float& fPos) {
    CRGBA tempColor;
    char* pNewPtr = ParseToken_Hooked(pText, tempColor, true, nullptr) - 1;

    if (*g_PS2Symbol != 0) {
        // Get current font scale Y from game
        float fontScaleY = *g_FontScaleY;
        float widthToAdd = g_SymbolSpriteWidth * fontScaleY;
        *g_PS2Symbol = 0;  // Reset after measuring
        fPos += widthToAdd;
    }

    return pNewPtr;
}

// ============================================================================
// TokenWidthHook - Fix "sub esi, 3" in GetNumberLines for variable-length tokens
// ============================================================================

// GetNumberLines does "sub esi, 3" when PS2Symbol is set, assuming 3-char tokens.
// Our tokens are 5 chars (~Kxx~, ~Mxx~), so we hook to use g_TokenWidth instead.
void __declspec(naked) TokenWidthHook() {
    __asm {
        test    dl, dl
        jz      TokenWidthHook_Return
        sub     esi, g_TokenWidth
    TokenWidthHook_Return:
        ret
    }
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

    // Replace GetControllerSettingTextMouse entirely (0x52F390)
    // This handles mouse button sprite tokens
    patch::RedirectJump(0x52F390, GetControllerSettingTextMouse_Thunk);

    // Hook ParseToken to render our sprite tokens (~Kxx~, ~Mxx~)
    patch::RedirectCall(0x719965, ParseToken_Hooked);
    patch::RedirectCall(0x71A018, ParseToken_Hooked);
    patch::RedirectCall(0x71A2C4, ParseToken_Hooked);

    // Variable-width sprite patches (like GInput)
    // These make the game read our g_SymbolSpriteWidth instead of a fixed value
    patch::SetPointer(0x718A98, &g_SymbolSpriteWidth);
    patch::SetPointer(0x719A55, &g_SymbolSpriteWidth);

    // Hook for proper text width calculation with tokens
    // Patches at 0x71A181-0x71A18C create new code calling AddTokenToWidth
    patch::SetUInt(0x71A181, 0x0C24448D);   // lea eax, [esp+0Ch]
    patch::SetUShort(0x71A185, 0x5650);     // push eax; push esi

    // Write call instruction manually (E8 + relative offset)
    // patch::RedirectCall doesn't work here because there's no existing call to redirect
    patch::SetUChar(0x71A187, 0xE8);  // call opcode
    uintptr_t callTarget = reinterpret_cast<uintptr_t>(&AddTokenToWidth);
    uintptr_t callAddr = 0x71A187;
    int32_t relativeOffset = static_cast<int32_t>(callTarget - (callAddr + 5));
    patch::SetInt(0x71A188, relativeOffset);

    patch::SetUInt(0x71A18C, 0x08EBF08B);   // mov esi, eax; jmp +8

    // Hook for GetNumberLines to use correct token width (fixes "sub esi, 3" for 5-char tokens)
    patch::RedirectCall(0x71A336, TokenWidthHook);

    // Hook CSprite2d::Draw call in PrintChar (0x718AE5) to enforce minimum sprite size
    // This lets the game handle render state while we just modify the rect size
    patch::RedirectCall(0x718AE5, ButtonSprite_Draw_Hook);
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
