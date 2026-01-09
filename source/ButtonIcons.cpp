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
// DEBUG LOGGING
// ============================================================================

#define BUTTONICONS_DEBUG 1  // Set to 0 to disable logging
#define BUTTONICONS_VERBOSE 0  // Set to 1 for verbose logging (fills up fast)

#if BUTTONICONS_DEBUG
static FILE* g_DebugLog = nullptr;
static int g_LogCount = 0;
static const int MAX_LOG_ENTRIES = 100000;

// Statistics for periodic reporting
static int g_TotalParseTokenCalls = 0;
static int g_KeyboardTokens = 0;
static int g_MouseTokens = 0;
static int g_GInputTokens = 0;
static int g_OtherTokens = 0;
static int g_DrawHookCalls = 0;
static int g_LastReportTime = 0;

static void DebugLog(const char* format, ...) {
    if (!g_DebugLog || g_LogCount >= MAX_LOG_ENTRIES) return;

    va_list args;
    va_start(args, format);
    vfprintf(g_DebugLog, format, args);
    fprintf(g_DebugLog, "\n");
    fflush(g_DebugLog);
    va_end(args);
    g_LogCount++;
}

// Conditional logging - only logs if VERBOSE or if it's important
static void DebugLogVerbose(const char* format, ...) {
#if BUTTONICONS_VERBOSE
    if (!g_DebugLog || g_LogCount >= MAX_LOG_ENTRIES) return;
    va_list args;
    va_start(args, format);
    vfprintf(g_DebugLog, format, args);
    fprintf(g_DebugLog, "\n");
    fflush(g_DebugLog);
    va_end(args);
    g_LogCount++;
#endif
}

// Log statistics periodically (every ~30 seconds based on call count)
static void LogStatsPeriodically() {
    // Log stats every 10000 ParseToken calls
    if (g_TotalParseTokenCalls > 0 && (g_TotalParseTokenCalls % 10000) == 0) {
        DebugLog("=== STATS at %d calls: KB=%d, Mouse=%d, GInput=%d, Other=%d, DrawHook=%d ===",
                 g_TotalParseTokenCalls, g_KeyboardTokens, g_MouseTokens,
                 g_GInputTokens, g_OtherTokens, g_DrawHookCalls);
    }
}

static void InitDebugLog() {
    g_DebugLog = fopen("ButtonIcons_debug.log", "w");
    if (g_DebugLog) {
        fprintf(g_DebugLog, "=== ButtonIcons Debug Log (Corruption Detection Mode) ===\n");
        fprintf(g_DebugLog, "This log focuses on errors and anomalies. Stats logged every 10000 calls.\n\n");
        fflush(g_DebugLog);
    }
}

static void CloseDebugLog() {
    if (g_DebugLog) {
        fprintf(g_DebugLog, "\n=== FINAL STATS ===\n");
        fprintf(g_DebugLog, "Total ParseToken calls: %d\n", g_TotalParseTokenCalls);
        fprintf(g_DebugLog, "Keyboard tokens: %d\n", g_KeyboardTokens);
        fprintf(g_DebugLog, "Mouse tokens: %d\n", g_MouseTokens);
        fprintf(g_DebugLog, "GInput tokens: %d\n", g_GInputTokens);
        fprintf(g_DebugLog, "Other tokens: %d\n", g_OtherTokens);
        fprintf(g_DebugLog, "DrawHook calls: %d\n", g_DrawHookCalls);
        fprintf(g_DebugLog, "Log entries: %d\n", g_LogCount);
        fprintf(g_DebugLog, "=== End of Log ===\n");
        fclose(g_DebugLog);
        g_DebugLog = nullptr;
    }
}
#else
#define DebugLog(...) ((void)0)
#define DebugLogVerbose(...) ((void)0)
#define LogStatsPeriodically() ((void)0)
#define InitDebugLog() ((void)0)
#define CloseDebugLog() ((void)0)
#endif

// ============================================================================
// GINPUT COMPATIBILITY
// ============================================================================

static bool g_GInputLoaded = false;
static CSprite2d* g_SpriteArray = nullptr;  // Points to either our array or GInput's
static float* g_SpriteWidths = nullptr;     // Points to either our widths or GInput's

// Check if GInput is loaded
static bool IsGInputLoaded() {
    return GetModuleHandleA("GInputSA.asi") != nullptr;
}

// ============================================================================
// EXTENDED SPRITE ARRAY
// ============================================================================
// Layout (without GInput):
// 0:       Unused (PS2Symbol=0 means no sprite)
// 1-14:    PS2 controller buttons (original game) - DO NOT USE
// 15-82:   Keyboard keys (68 keys)
// 83-89:   Mouse buttons (7 buttons)
// 90-95:   Reserved
//
// Layout (with GInput):
// 0-49:    GInput sprites (controller buttons)
// 50-117:  Keyboard keys (68 keys)
// 118-124: Mouse buttons (7 buttons)
// ============================================================================

static const int MAX_EXTENDED_SPRITES = 128;  // Increased for GInput compatibility
static CSprite2d g_ExtendedSprites[MAX_EXTENDED_SPRITES];
static float g_ExtendedSpriteWidths[MAX_EXTENDED_SPRITES];
static float g_SymbolSpriteWidth = 17.0f;
static unsigned int g_TokenWidth = 3;  // Token length for GetNumberLines sub esi fix

// Base indices - adjusted at runtime if GInput is present
static int KEYBOARD_SPRITE_BASE = 15;
static int MOUSE_SPRITE_BASE = 15 + KEYBOARD_COUNT;  // 15 + 68 = 83

// GInput-compatible bases (after GInput's ~50 sprites)
static const int GINPUT_KEYBOARD_SPRITE_BASE = 50;
static const int GINPUT_MOUSE_SPRITE_BASE = 50 + KEYBOARD_COUNT;  // 50 + 68 = 118

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
    g_GInputLoaded = IsGInputLoaded();

    if (g_GInputLoaded) {
        // GInput is loaded - use higher sprite indices to avoid conflicts with GInput's sprites
        KEYBOARD_SPRITE_BASE = GINPUT_KEYBOARD_SPRITE_BASE;
        MOUSE_SPRITE_BASE = GINPUT_MOUSE_SPRITE_BASE;

        // Read GInput's sprite array pointer from the patched address
        // GInput patches 0x718AE1 with its PS2Sprite array address
        CSprite2d* ginputArray = *reinterpret_cast<CSprite2d**>(0x718AE1);

        // Copy GInput's sprites (indices 0-49) into our extended array
        // This preserves GInput's controller button sprites
        if (ginputArray) {
            memcpy(g_ExtendedSprites, ginputArray, 50 * sizeof(CSprite2d));
        }

        // Initialize sprite widths for our keyboard/mouse sprites
        for (int i = 0; i < MAX_EXTENDED_SPRITES; i++) {
            g_ExtendedSpriteWidths[i] = 17.0f;
        }

        g_SpriteArray = g_ExtendedSprites;
        g_SpriteWidths = g_ExtendedSpriteWidths;

        // Override GInput's patch to use our larger array
        // This is safe because our array contains GInput's sprites at indices 0-49
        patch::SetPointer(0x718AE1, g_ExtendedSprites);

        DebugLog("Init: GInput mode - KB_BASE=%d, MOUSE_BASE=%d, GInputArray=%p",
                 KEYBOARD_SPRITE_BASE, MOUSE_SPRITE_BASE, (void*)ginputArray);

    } else {
        // No GInput - use our own array starting at index 15
        KEYBOARD_SPRITE_BASE = 15;
        MOUSE_SPRITE_BASE = 15 + KEYBOARD_COUNT;

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

        g_SpriteArray = g_ExtendedSprites;
        g_SpriteWidths = g_ExtendedSpriteWidths;

        // Patch game to use our extended array
        patch::SetPointer(0x718AE1, g_ExtendedSprites);

        DebugLog("Init: Standalone mode - KB_BASE=%d, MOUSE_BASE=%d",
                 KEYBOARD_SPRITE_BASE, MOUSE_SPRITE_BASE);
    }
}

// ============================================================================
// TEXTURE LOADING
// ============================================================================

static void LoadTextures() {
    if (g_TexturesLoaded) return;
    if (!g_SpriteArray) {
        DebugLog("ERROR: LoadTextures called but g_SpriteArray is NULL!");
        return;
    }

    g_TxdSlot = CTxdStore::AddTxdSlot("buttonicons");
    if (g_TxdSlot == -1) {
        DebugLog("ERROR: Failed to add TXD slot!");
        return;
    }

    if (!CTxdStore::LoadTxd(g_TxdSlot, "models\\pcbtns.txd")) {
        DebugLog("ERROR: Failed to load pcbtns.txd!");
        CTxdStore::RemoveTxdSlot(g_TxdSlot);
        g_TxdSlot = -1;
        return;
    }

    CTxdStore::AddRef(g_TxdSlot);
    CTxdStore::SetCurrentTxd(g_TxdSlot);

    // Load keyboard sprites into the active sprite array
    int loadedKeyboard = 0;
    for (int i = 0; i < KEYBOARD_COUNT; i++) {
        int idx = KEYBOARD_SPRITE_BASE + i;
        g_SpriteArray[idx].SetTexture(const_cast<char*>(g_KeyboardSpriteNames[i]));
        if (g_SpriteArray[idx].m_pTexture) loadedKeyboard++;
    }

    // Load mouse sprites
    int loadedMouse = 0;
    for (int i = 0; i < MOUSE_COUNT; i++) {
        int idx = MOUSE_SPRITE_BASE + i;
        g_SpriteArray[idx].SetTexture(const_cast<char*>(g_MouseSpriteNames[i]));
        if (g_SpriteArray[idx].m_pTexture) loadedMouse++;
    }

    // Calculate widths based on texture aspect ratio (like GInput does)
    for (int i = KEYBOARD_SPRITE_BASE; i < MOUSE_SPRITE_BASE + MOUSE_COUNT; i++) {
        RwTexture* tex = g_SpriteArray[i].m_pTexture;
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
    DebugLog("Init: Loaded %d/%d keyboard, %d/%d mouse sprites",
             loadedKeyboard, KEYBOARD_COUNT, loadedMouse, MOUSE_COUNT);
}

static void UnloadTextures() {
    if (!g_TexturesLoaded) return;
    if (!g_SpriteArray) return;

    for (int i = 0; i < KEYBOARD_COUNT; i++) {
        g_SpriteArray[KEYBOARD_SPRITE_BASE + i].Delete();
    }
    for (int i = 0; i < MOUSE_COUNT; i++) {
        g_SpriteArray[MOUSE_SPRITE_BASE + i].Delete();
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
static int g_KeyboardCallCount = 0;  // For debug throttling

static char* __cdecl GetControllerSettingTextKeyBoard_Impl(int action, int type) {
    void* thisPtr = g_ControllerThis;
    g_KeyboardCallCount++;
    
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
        DebugLogVerbose("GetControllerSettingTextKeyBoard: action=%d, keyCode=%u -> %s", action, keyCode, token);
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
static int g_MouseCallCount = 0;  // For debug throttling

static char* __cdecl GetControllerSettingTextMouse_Impl(int action) {
    void* thisPtr = g_ControllerThis;
    g_MouseCallCount++;

    if (!thisPtr || thisPtr == reinterpret_cast<void*>(0xFFFFFFFF)) return nullptr;
    if (action < 0 || action > 58) return nullptr;

    unsigned int mouseCode = GetMouseButtonAssociatedWithAction(thisPtr, action);
    if (mouseCode == 0) return nullptr;  // Unbound

    // Return sprite token if enabled
    const char* token = GetSpriteTokenForMouseCode(mouseCode);
    if (token && g_Enabled && g_TexturesLoaded) {
        strcpy(g_MouseTokenBuffer, token);
        DebugLogVerbose("GetControllerSettingTextMouse: action=%d, mouseCode=%u -> %s", action, mouseCode, token);
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

// Pointers to GInput's variables (if GInput is loaded)
// Read at runtime from patched addresses
static float* g_GInputSpriteWidth = nullptr;
static unsigned int* g_GInputTokenWidth = nullptr;  // GInput's nTokenWidth variable

using ParseToken_t = char*(__cdecl*)(char*, CRGBA&, bool, char*);
static ParseToken_t ParseToken_Original = reinterpret_cast<ParseToken_t>(0x718F00);

char* __cdecl ParseToken_Hooked(char* text, CRGBA& color, bool isBlip, char* tag) {
    // Always set a safe default first
    g_TokenWidth = 3;
    g_TotalParseTokenCalls++;

    if (!text || !g_Enabled || !g_TexturesLoaded) {
        return ParseToken_Original(text, color, isBlip, tag);
    }

    // Check for keyboard token: ~Kxx~
    if (text[0] == '~' && text[1] == 'K') {
        int keyIndex = ParseKeyboardToken(text);
        if (keyIndex >= 0 && keyIndex < KEYBOARD_COUNT) {
            int spriteIdx = KEYBOARD_SPRITE_BASE + keyIndex;
            if (spriteIdx >= 0 && spriteIdx < MAX_EXTENDED_SPRITES) {
                *g_PS2Symbol = static_cast<uint8_t>(spriteIdx);
                g_SymbolSpriteWidth = g_ExtendedSpriteWidths[spriteIdx];
                g_TokenWidth = 5;  // Our tokens are 5 chars: ~Kxx~
                g_KeyboardTokens++;
                DebugLogVerbose("ParseToken: Keyboard token K%02d -> spriteIdx=%d", keyIndex, spriteIdx);
                LogStatsPeriodically();
                return text + 5;
            } else {
                // ERROR: Invalid sprite index!
                DebugLog("ERROR: Keyboard token K%02d -> INVALID spriteIdx=%d (base=%d, max=%d)",
                         keyIndex, spriteIdx, KEYBOARD_SPRITE_BASE, MAX_EXTENDED_SPRITES);
            }
        }
    }

    // Check for mouse token: ~Mxx~
    if (text[0] == '~' && text[1] == 'M' && text[4] == '~') {
        char d1 = text[2], d2 = text[3];
        if (d1 == '0' && d2 >= '0' && d2 <= '6') {
            int spriteIdx = MOUSE_SPRITE_BASE + (d2 - '0');
            if (spriteIdx >= 0 && spriteIdx < MAX_EXTENDED_SPRITES) {
                *g_PS2Symbol = static_cast<uint8_t>(spriteIdx);
                g_SymbolSpriteWidth = g_ExtendedSpriteWidths[spriteIdx];
                g_TokenWidth = 5;  // Our tokens are 5 chars: ~Mxx~
                g_MouseTokens++;
                DebugLogVerbose("ParseToken: Mouse token M%c%c -> spriteIdx=%d", d1, d2, spriteIdx);
                LogStatsPeriodically();
                return text + 5;
            } else {
                // ERROR: Invalid sprite index!
                DebugLog("ERROR: Mouse token M%c%c -> INVALID spriteIdx=%d (base=%d, max=%d)",
                         d1, d2, spriteIdx, MOUSE_SPRITE_BASE, MAX_EXTENDED_SPRITES);
            }
        }
    }

    // For original/GInput tokens, let the chained handler deal with it
    // g_TokenWidth stays at 3 (set at start of function)
    char* result = ParseToken_Original(text, color, isBlip, tag);
    uint8_t symbolAfter = *g_PS2Symbol;

    // If a sprite was set by the original/GInput handler
    if (symbolAfter != 0) {
        // If GInput handled it (sprite index in GInput's range), read GInput's variables
        if (g_GInputLoaded && symbolAfter < KEYBOARD_SPRITE_BASE) {
            // Copy GInput's sprite width
            if (g_GInputSpriteWidth) {
                g_SymbolSpriteWidth = *g_GInputSpriteWidth;
            }
            // CRITICAL: Copy GInput's token width! GInput uses 3 or 4 char tokens
            if (g_GInputTokenWidth) {
                g_TokenWidth = *g_GInputTokenWidth;
            }
            g_GInputTokens++;
            DebugLogVerbose("ParseToken: GInput token -> symbolIdx=%d, tokenWidth=%d", symbolAfter, g_TokenWidth);
        } else if (symbolAfter < MAX_EXTENDED_SPRITES) {
            // Bounds check before array access
            g_SymbolSpriteWidth = g_ExtendedSpriteWidths[symbolAfter];
            g_OtherTokens++;
            DebugLogVerbose("ParseToken: Original token -> symbolIdx=%d", symbolAfter);
        } else {
            // ERROR: Out of bounds!
            g_SymbolSpriteWidth = 17.0f;
            DebugLog("ERROR: OUT OF BOUNDS! symbolIdx=%d (max=%d) at call #%d",
                     symbolAfter, MAX_EXTENDED_SPRITES, g_TotalParseTokenCalls);
        }
    }

    LogStatsPeriodically();
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
// Enforces minimum size for our extended sprites
void __fastcall ButtonSprite_Draw_Hook(CSprite2d* sprite, void* edx, const CRect& rect, const CRGBA& color) {
    g_DrawHookCalls++;

    // Check if this sprite is one of our extended sprites
    // by comparing the sprite pointer to the active sprite array
    if (g_SpriteArray && sprite) {
        ptrdiff_t offset = reinterpret_cast<uintptr_t>(sprite) - reinterpret_cast<uintptr_t>(g_SpriteArray);
        int spriteIndex = static_cast<int>(offset / sizeof(CSprite2d));

        // Only log anomalies
        if (spriteIndex < 0 || spriteIndex >= MAX_EXTENDED_SPRITES) {
            DebugLog("ERROR: ButtonSprite_Draw_Hook: INVALID spriteIndex=%d (offset=%d)",
                     spriteIndex, (int)offset);
        }

        // Bounds check and verify it's one of our keyboard/mouse sprites
        if (spriteIndex >= KEYBOARD_SPRITE_BASE &&
            spriteIndex < KEYBOARD_SPRITE_BASE + KEYBOARD_COUNT + MOUSE_COUNT &&
            spriteIndex < MAX_EXTENDED_SPRITES) {

            float currentHeight = rect.bottom - rect.top;

            if (currentHeight < MIN_SPRITE_HEIGHT && currentHeight > 0.0f) {
                // Get aspect ratio from our stored widths (with bounds check)
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
    }

    // Default: call original with unmodified rect
    CSprite2d_Draw_Original(sprite, rect, color);
}

// ============================================================================
// PUBLIC API: Direct Drawing
// ============================================================================

void DrawIcon(MouseButton button, float x, float y, float size) {
    if (!g_TexturesLoaded || !g_SpriteArray || button < 0 || button >= MOUSE_COUNT) return;

    CSprite2d* sprite = &g_SpriteArray[MOUSE_SPRITE_BASE + button];
    if (!sprite->m_pTexture) return;
    
    CRect rect(x - size * 0.5f, y - size * 0.5f, x + size * 0.5f, y + size * 0.5f);
    sprite->Draw(rect, CRGBA(255, 255, 255, 255));
}

void DrawIconColored(MouseButton button, float x, float y, float size,
                     unsigned char r, unsigned char g, unsigned char b, unsigned char a) {
    if (!g_TexturesLoaded || !g_SpriteArray || button < 0 || button >= MOUSE_COUNT) return;

    CSprite2d* sprite = &g_SpriteArray[MOUSE_SPRITE_BASE + button];
    if (!sprite->m_pTexture) return;

    CRect rect(x - size * 0.5f, y - size * 0.5f, x + size * 0.5f, y + size * 0.5f);
    sprite->Draw(rect, CRGBA(r, g, b, a));
}

// ============================================================================
// AddTokenToWidth HOOK - For proper text width calculation
// ============================================================================

// This is called during text width/layout calculation to account for sprite width
char* __stdcall AddTokenToWidth(char* pText, float& fPos) {
    if (!pText) {
        DebugLog("ERROR: AddTokenToWidth called with NULL pText!");
        return pText;
    }

    CRGBA tempColor;
    char* pNewPtr = ParseToken_Hooked(pText, tempColor, true, nullptr) - 1;

    uint8_t symbolIdx = *g_PS2Symbol;
    if (symbolIdx != 0) {
        // Get current font scale Y from game
        float fontScaleY = g_FontScaleY ? *g_FontScaleY : 1.0f;
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
        mov     eax, dword ptr [g_TokenWidth]  // Load token width value
        sub     esi, eax                        // Subtract from text pointer
    TokenWidthHook_Return:
        ret
    }
}

// ============================================================================
// INSTALLATION
// ============================================================================

static bool g_HooksInstalled = false;

// Deferred initialization - called after all ASIs are loaded
static void InstallGInputCompatibleHooks() {
    if (g_HooksInstalled) return;
    g_HooksInstalled = true;

    // Now check for GInput - all ASIs should be loaded by now
    ExpandButtonSpriteArray();

    if (g_GInputLoaded) {
        // GInput is loaded - chain with GInput's ParseToken hook
        // Read GInput's ParseToken hook target from one of the patched call sites
        // The call instruction at 0x719965 has been patched by GInput
        // Format: E8 [4-byte relative offset]
        int32_t ginputOffset = *reinterpret_cast<int32_t*>(0x719965 + 1);
        uintptr_t ginputParseToken = 0x719965 + 5 + ginputOffset;
        ParseToken_Original = reinterpret_cast<ParseToken_t>(ginputParseToken);

        // Read GInput's sprite width variable pointer BEFORE we patch it
        // GInput patches 0x718A98 to point to its fSymbolSpriteWidth
        g_GInputSpriteWidth = *reinterpret_cast<float**>(0x718A98);

        // Read GInput's TokenWidthHook function address to find nTokenWidth
        // The call at 0x71A336 points to GInput's TokenWidthHook
        // GInput's TokenWidthHook assembly:
        //   test dl, dl     ; 84 D2 (offset 0-1)
        //   jz short ret    ; 74 xx (offset 2-3)
        //   sub esi, [nTokenWidth] ; 2B 35 [addr] (offset 4-9)
        //   ret             ; C3 (offset 10)
        int32_t tokenHookOffset = *reinterpret_cast<int32_t*>(0x71A336 + 1);
        uintptr_t ginputTokenWidthHook = 0x71A336 + 5 + tokenHookOffset;

        // Validate the expected instruction pattern: 84 D2 74 xx 2B 35
        uint8_t* hookBytes = reinterpret_cast<uint8_t*>(ginputTokenWidthHook);
        if (hookBytes[0] == 0x84 && hookBytes[1] == 0xD2 &&  // test dl, dl
            hookBytes[2] == 0x74 &&                          // jz short
            hookBytes[4] == 0x2B && hookBytes[5] == 0x35) {  // sub esi, [mem32]
            g_GInputTokenWidth = *reinterpret_cast<unsigned int**>(ginputTokenWidthHook + 6);
            DebugLog("Init: GInput nTokenWidth at %p (validated)", (void*)g_GInputTokenWidth);
        } else {
            DebugLog("WARNING: GInput TokenWidthHook pattern mismatch, tokenWidth sync disabled");
            g_GInputTokenWidth = nullptr;
        }

        // Now hook ParseToken to chain: our hook -> GInput's hook -> original
        patch::RedirectCall(0x719965, ParseToken_Hooked);
        patch::RedirectCall(0x71A018, ParseToken_Hooked);
        patch::RedirectCall(0x71A2C4, ParseToken_Hooked);

        // Override GInput's sprite width patches to use our variable
        // We copy GInput's width to ours when GInput handles tokens
        patch::SetPointer(0x718A98, &g_SymbolSpriteWidth);
        patch::SetPointer(0x719A55, &g_SymbolSpriteWidth);

    } else {
        // No GInput - install all hooks normally

        // Hook ParseToken to render our sprite tokens (~Kxx~, ~Mxx~)
        patch::RedirectCall(0x719965, ParseToken_Hooked);
        patch::RedirectCall(0x71A018, ParseToken_Hooked);
        patch::RedirectCall(0x71A2C4, ParseToken_Hooked);

        // Variable-width sprite patches
        // These make the game read our g_SymbolSpriteWidth instead of a fixed value
        patch::SetPointer(0x718A98, &g_SymbolSpriteWidth);
        patch::SetPointer(0x719A55, &g_SymbolSpriteWidth);
    }

    // These hooks are ALWAYS needed for our 5-character tokens (~Kxx~, ~Mxx~)
    // even with GInput, because the game's default "sub esi, 3" assumes 3-char tokens

    // Hook for proper text width calculation with tokens
    // Patches at 0x71A181-0x71A18C create new code calling AddTokenToWidth
    patch::SetUInt(0x71A181, 0x0C24448D);   // lea eax, [esp+0Ch]
    patch::SetUShort(0x71A185, 0x5650);     // push eax; push esi

    // Write call instruction manually (E8 + relative offset)
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
    // Note: This hook is safe with GInput - GInput doesn't hook the Draw call itself
    patch::RedirectCall(0x718AE5, ButtonSprite_Draw_Hook);
}

void InstallHooks() {
    // Initialize debug logging first
    InitDebugLog();

    // Install non-GInput-dependent hooks immediately
    // These don't conflict with GInput and can be installed early

    // Replace GetControllerSettingTextKeyBoard entirely (0x52FE10)
    // This handles all ~k~~ACTION~ replacements automatically via game's InsertPlayerControlKeysInString
    patch::RedirectJump(0x52FE10, GetControllerSettingTextKeyBoard_Thunk);

    // Replace GetControllerSettingTextMouse entirely (0x52F390)
    // This handles mouse button sprite tokens
    patch::RedirectJump(0x52F390, GetControllerSettingTextMouse_Thunk);

    // Defer GInput-dependent hooks to initRwEvent when all ASIs are loaded
    Events::initRwEvent += []() {
        InstallGInputCompatibleHooks();
        LoadTextures();
    };

    Events::shutdownRwEvent += []() {
        UnloadTextures();
        CloseDebugLog();
    };
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
