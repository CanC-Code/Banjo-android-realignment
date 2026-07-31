#include <ultra64.h>
#include "core1/core1.h"
#include "functions.h"
#include "variables.h"
#include "version.h"
#include "gc/gctransition.h"
#include <android/log.h>

#define LOG_TAG "BKA_CODE0"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

#define MAIN_THREAD_STACK_SIZE 0x17F0

#if VERSION == VERSION_PAL
    extern s32 D_80000300;
#endif

extern void __osTimerServicesInit(void);

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
    // Stubbed: calls func_802E4214 which blocks waiting for vblank
    LOGI("BKA: func_8023DA9C SKIPPED");
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
    // pfsManager_init();      // STUBBED
    LOGI("BKA: pfsManager_init SKIPPED");
    baMotor_init();
    // audioManager_init();    // STUBBED
    LOGI("BKA: audioManager_init SKIPPED");
    // graphicsCache_init();   // STUBBED (blocks)
    LOGI("BKA: graphicsCache_init SKIPPED");
    // ml_init();              // STUBBED (may block)
    LOGI("BKA: ml_init SKIPPED");
    // gctransition_reset();   // STUBBED (may block)
    LOGI("BKA: gctransition_reset SKIPPED");
    D_8027A130 = 0;
    gGlobalTimer = 0;
    // func_8023DA9C(3);       // STUBBED (blocks on vblank)
    LOGI("BKA: func_8023DA9C SKIPPED");
    LOGI("BKA: core1_init DONE");
}

void globalTimer_incTimer(void){
    gGlobalTimer++;
}

void globalTimer_decTimer(void){
    gGlobalTimer--;
}

void mainLoop(void){
    s32 x, y;

    viMgr_clearFramebuffers();

    static int diagFrame = 0;
    if (diagFrame < 5) {
        LOGI("BKA: mainLoop frame %d — setting fb_offset", diagFrame);
        u8 *fb = gN64_RDRAM + 0x1000;
        s32 w = 320;
        s32 h = 240;
        for (y = 0; y < h; y++) {
            for (x = 0; x < w; x++) {
                fb[(x + y * w) * 2 + 0] = 0xF8;
                fb[(x + y * w) * 2 + 1] = 0x01;
            }
        }
        g_active_fb_offset = 0x1000;
        gFramebufferWidth  = w;
        gFramebufferHeight = h;
        LOGI("BKA: SET fb_offset=0x%04X", g_active_fb_offset);
        diagFrame++;
    }

    // Skip the game logic that depends on uninitialized subsystems.
    // Just let the diagnostic loop run.
    if(D_80275610){
        // func_8023DA9C(D_80275610 - 1);
        D_80275610 = 0;
    }
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