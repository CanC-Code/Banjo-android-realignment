#include <ultra64.h>
#include "core1/core1.h"
#include "functions.h"
#include "variables.h"
#include "version.h"
#include "gc/gctransition.h"
#include <android/log.h>
#include <string.h>

#define LOG_TAG "BKA_CODE0"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

#define MAIN_THREAD_STACK_SIZE 0x17F0

#if VERSION == VERSION_PAL
    extern s32 D_80000300;
#endif

extern void __osTimerServicesInit(void);

/* ---- Frame synchronisation hook (prevents CPU spinning) ---- */
extern void BKA_FrameSyncHook(void);

s32 D_80275610 = 0;
s32 D_80275614 = 0;
u32 gGlobalTimer = 0;
u32 sDebugVar_8027561C[] = { 0x9, 0x4, 0xA, 0x3, 0xB, 0x2, 0xC, 0x5, 0x0,  0x1, 0x6, 0xD,  -1 };
u32 D_80275650 = VER_SELECT(0xAD019D3C, 0xA371A8F3, 0, 0);
u32 D_80275654 = VER_SELECT(0xD381B72F, 0xD0709154, 0, 0);
char sDebugVar_80275658[] = VER_SELECT("HjunkDire:218755", "HjunkDire:300875", "HjunkDire:", "HjunkDire:");

/* .bss */
u32 D_8027A130;
u8 pad_8027A138[0x400];
u64 sDebugVar_8027A538;
u64 sDebugVar_8027A540;
u8 sMainThreadStack[MAIN_THREAD_STACK_SIZE];
OSThread sMainThread;
s32 gBootMap;
static n64_bool sDisableInput;
static u64 sDebugVar_8027BEF0;

extern u8 core2_TEXT_START[];
extern u32 g_active_fb_offset;
extern uint8_t* gN64_RDRAM;

void func_8023DA20(s32 arg0){
    if (core2_TEXT_START && core2_TEXT_START > (u8*)&D_8027A130) {
        bzero(&D_8027A130, core2_TEXT_START - (u8*)&D_8027A130);
    }
    osWriteBackDCacheAll();
    sns_find_and_parse_payload();
    osInitialize();
    initThread_create();
}

void func_8023DA74(void){
    func_8033BD6C();
    func_80255198();
}

void func_8023DA9C(s32 arg0){
    func_80254008();
    viMgr_clearFramebuffers();
    if (D_8027A130 == 4){
        func_802E3580();
    }
    if (D_8027A130 == 3){
        func_802E4170();
    }
    func_8023DA74();
    D_8027A130 = arg0;
    if (D_8027A130 == 3){
        func_802E4214(gBootMap);
    }
    if (D_8027A130 == 4){
        dummy_func_802E35D0();
    }
    ucode_stub1();
}

u32 globalTimer_getTimeMasked(u32 mask){
    return gGlobalTimer & mask;
}

s32 globalTimer_getTime(void){
    return gGlobalTimer;
}

void globalTimer_reset(void){
    gGlobalTimer = 0;
}

enum map_e getSpecialBootMap(void){
    return (DEBUG_use_special_bootmap())? MAP_80_GL_FF_ENTRANCE : MAP_91_FILE_SELECT;
}

enum map_e getDefaultBootMap(void){
    return MAP_1F_CS_START_RAREWARE;
}

void func_8023DBAC(void){
    setBootMap(getDefaultBootMap());
    func_8023DFF0(3);
}

void func_8023DBDC(void){
    setBootMap(getSpecialBootMap());
    func_8023DFF0(3);
}

void core1_init(void) {
    LOGI("BKA: core1_init START");
    __osTimerServicesInit();
#if VERSION == VERSION_PAL
     osTvType = 0;
#endif
    ucode_load();
    setBootMap(getDefaultBootMap());
    rarezip_init();
    viMgr_init();
    overlayManagerloadCore2();
    sDebugVar_8027BEF0 = sDebugVar_8027A538;
    heap_init();
    func_80254028();
    dummy_func_8025AFB0();
    allocUnusedBlock();
    assetCache_init();
    // The following init functions are stubbed to avoid crashes/blocks:
    // pfsManager_init();      // crashes in controller init
    // audioManager_init();    // crashes in sfxInstruments_init
    // graphicsCache_init();   // may block
    // ml_init();              // may block
    // gctransition_reset();   // may block
    D_8027A130 = 3;             // set game state to "game"
    gGlobalTimer = 0;
    func_8023DA9C(3);           // now safe to call (stubbed internal vblank wait)
    LOGI("BKA: core1_init DONE");
}

void globalTimer_incTimer(void){
    gGlobalTimer++;
}

void globalTimer_decTimer(void){
    gGlobalTimer--;
}

// Draw a single pixel into the framebuffer (RGBA5551 format)
static inline void debug_put_pixel(int x, int y, u8 r, u8 g, u8 b) {
    if (x < 0 || x >= 292 || y < 0 || y >= 216) return;
    u16 pixel = ((r >> 3) << 11) | ((g >> 3) << 6) | ((b >> 3) << 1) | 1;
    gFramebuffers[0][y * 292 + x] = pixel;
    gFramebuffers[1][y * 292 + x] = pixel;
}

void mainLoop(void){
    s32 x, y;

    viMgr_clearFramebuffers();

    if((globalTimer_getTime() & 0x7f) == 0x11)
        sns_write_payload_over_heap();
    func_8023DA74();

    if(D_8027A130 != 3 || getGameMode() != GAME_MODE_4_PAUSED)
        globalTimer_incTimer();

    // pfsManager_update() and sDisableInput are skipped because
    // pfsManager_init was stubbed.
    sDisableInput = FALSE;

    baMotor_80250C08();

    if(!mapSpecificFlags_validateCRC1()){
        eeprom_writeBlocks(0, 0, 0x80397AD0, 0x40);
    }

    switch(D_8027A130){
        case 4:
            func_802E35D8();
            break;
        case 3:
            func_80255524();
            func_80255ACC();
            spawnQueue_func_802C3A18();
            if(func_802E4424())
                game_draw(0);               // real game rendering
            spawnQueue_flush();
            break;
    }

    if(D_80275610){
        func_8023DA9C(D_80275610 - 1);
        D_80275610 = 0;
    }

    // ===================================================================
    // DEBUG OVERLAY: Draw colored status bars so we can see the game
    // is alive and rendering. Remove this once the title screen works.
    // ===================================================================
    {
        s32 mode = getGameMode();
        u32 t = globalTimer_getTime();

        // Top bar: game mode indicator (color changes with mode)
        u8 tr = (mode == 3) ? 0 : ((mode == 4) ? 255 : 128);
        u8 tg = (mode == 3) ? 255 : ((mode == 4) ? 128 : 0);
        u8 tb = (mode == 3) ? 0 : ((mode == 4) ? 255 : 128);
        for (y = 0; y < 10; y++) {
            for (x = 0; x < 292; x++) {
                debug_put_pixel(x, y, tr, tg, tb);
            }
        }

        // Scrolling rainbow stripe in the middle
        for (y = 100; y < 116; y++) {
            for (x = 0; x < 292; x++) {
                u8 hue = (u8)((x + t) & 0xFF);
                u8 r, g, b;
                if (hue < 85) {
                    r = 255 - hue * 3; g = hue * 3; b = 0;
                } else if (hue < 170) {
                    hue -= 85;
                    r = 0; g = 255 - hue * 3; b = hue * 3;
                } else {
                    hue -= 170;
                    r = hue * 3; g = 0; b = 255 - hue * 3;
                }
                debug_put_pixel(x, y, r, g, b);
            }
        }

        // Frame counter bar (pulses)
        u8 pulse = (u8)((t & 0x3F) * 4);
        if (pulse > 128) pulse = 255 - pulse;
        for (y = 200; y < 210; y++) {
            for (x = 0; x < 292; x++) {
                debug_put_pixel(x, y, pulse, pulse, 255 - pulse);
            }
        }

        // Corner markers (white)
        for (int i = 0; i < 20; i++) {
            debug_put_pixel(i, 20 + i, 255, 255, 255);           // top-left diagonal
            debug_put_pixel(291 - i, 20 + i, 255, 255, 255);     // top-right diagonal
            debug_put_pixel(i, 195 - i, 255, 255, 255);          // bottom-left diagonal
            debug_put_pixel(291 - i, 195 - i, 255, 255, 255);    // bottom-right diagonal
        }
    }
    // ===================================================================

    // --- Copy the game's framebuffer to RDRAM so the video plugin can upload it ---
    {
        extern u16 gFramebuffers[2][292 * 216];
        extern u32 g_active_fb_offset;
        extern uint8_t* gN64_RDRAM;
        s32 fbSize = gFramebufferWidth * gFramebufferHeight * sizeof(u16);
        if (gN64_RDRAM && g_active_fb_offset) {
            memcpy(gN64_RDRAM + g_active_fb_offset, gFramebuffers[getActiveFramebuffer()], fbSize);
        }
    }

    // --- Frame synchronisation: wait for the host to present the current frame ---
    BKA_FrameSyncHook();
}

void mainThread_entry(void *arg) {
    LOGI("BKA: mainThread_entry START");
    core1_init();
    sns_write_payload_over_heap();
    LOGI("BKA: entering main loop");
    while (1) { mainLoop(); }
}

void func_8023DFF0(s32 arg0){ D_80275610 = arg0 + 1; }
s32 func_8023E000(void){ return D_8027A130; }
void setBootMap(enum map_e map_id){ gBootMap = map_id; }
void mainThread_create(void) {
    osCreateThread(&sMainThread, 6, mainThread_entry, NULL, sMainThreadStack + MAIN_THREAD_STACK_SIZE, 20);
}
OSThread *mainThread_get(void) { return &sMainThread; }
void disableInput_set(void){ sDisableInput = TRUE; }