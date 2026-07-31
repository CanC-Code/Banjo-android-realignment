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
    // Stubbed: the real implementation blocks waiting for vblank
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
    // All other init functions are stubbed to avoid crashes/blocks.
    // pfsManager_init();      // crashes in controller init
    // audioManager_init();    // crashes in sfxInstruments_init
    // graphicsCache_init();   // may block
    // ml_init();              // may block
    // gctransition_reset();   // may block
    // func_8023DA9C(3);       // blocks on vblank
    D_8027A130 = 3;             // set game state to "game" so mainLoop runs
    gGlobalTimer = 0;
    LOGI("BKA: core1_init DONE");
}

void globalTimer_incTimer(void){
    gGlobalTimer++;
}

void globalTimer_decTimer(void){
    gGlobalTimer--;
}

void mainLoop(void){
    static int frameCount = 0;
    frameCount++;

    // Fill RDRAM with solid red and tell the video plugin.
    u8 *fb = gN64_RDRAM + 0x1000;
    s32 w = 320;
    s32 h = 240;
    for (s32 y = 0; y < h; y++) {
        for (s32 x = 0; x < w; x++) {
            fb[(x + y * w) * 2 + 0] = 0xF8;
            fb[(x + y * w) * 2 + 1] = 0x01;
        }
    }
    g_active_fb_offset = 0x1000;
    gFramebufferWidth  = w;
    gFramebufferHeight = h;

    if (frameCount <= 3) {
        LOGI("BKA: frame %d — SET fb_offset=0x%04X", frameCount, g_active_fb_offset);
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