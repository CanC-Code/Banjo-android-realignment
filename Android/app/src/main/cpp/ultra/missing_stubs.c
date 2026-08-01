// File: Banjo-android-realignment/Android/app/src/main/cpp/ultra/missing_stubs.c
//
// LAST-RESORT fallbacks only.
//
// Rules:
//   - Do NOT define anything that has a real implementation in:
//       exceptasm.cpp    (initInterruptTables, __osPopThread, __osEnqueueThread,
//                         __osDispatchThread, __osEnqueueAndYield)
//       setintmask.cpp   (osSetIntMask)
//       libm_vals.cpp    (__libm_qnan_f)
//       lowlevel_bridge.cpp (osPiReadIo, osPiWriteIo)
//       audio_bridge.cpp (n_alSynAddPlayer, n_alSynRemovePlayer, etc.)
//       stubs.cpp        (initInterruptTables fallback, stub_void, etc.)
//
//   CMakeLists.txt lists this file LAST so the linker always prefers
//   the real implementations above when --allow-multiple-definition is set.

#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <android/log.h>

#define LOG_TAG "BKA_STUBS"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

typedef uint32_t OSIntMask;

// Forward declaration for RDRAM access
extern uint8_t* gN64_RDRAM;

// -----------------------------------------------------------------------
// OS / Hardware — only stubs not covered by any other translation unit
// -----------------------------------------------------------------------

// __osDisableInt / __osRestoreInt: no real impl elsewhere
OSIntMask __osDisableInt(void) { return 0; }
void      __osRestoreInt(OSIntMask mask) { (void)mask; }

// Standard BSD memory aliases used by old N64 SDK code
void bzero(void *s, int n)                    { memset(s, 0, (size_t)n); }
void bcopy(const void *src, void *dest, int n){ memmove(dest, src, (size_t)n); }

// Cache invalidation — no-ops on Android (cache is coherent from the CPU side)
void osWritebackDCache(void *vaddr, int32_t nbytes)  { (void)vaddr; (void)nbytes; }
void osInvalICache(void *vaddr, int32_t nbytes)       { (void)vaddr; (void)nbytes; }
void osInvalDCache(void *vaddr, int32_t nbytes)       { (void)vaddr; (void)nbytes; }
void osWriteBackDCacheAll(void)                       {}
void __osInitialize_autodetect(void)                  {}

// Thread helpers not covered by exceptasm.cpp
void __osCleanupThread(void) {}

// Timer / coprocessor registers — no real impl
uint32_t osGetCount(void)              { return 0; }
uint32_t __osGetSR(void)               { return 0; }
uint32_t ___osGetSR(void)              { return 0; }
void     __osSetSR(uint32_t sr)        { (void)sr; }
void     __osSetFpcCsr(uint32_t csr)   { (void)csr; }
void     __osSetCompare(uint32_t val)  { (void)val; }

// TLB — no-ops on Android
void     osMapTLBRdb(void)             {}
uint32_t __osProbeTLB(void* a)         { (void)a; return 0; }

// FIXED: osPiGetStatus was missing entirely. The DMA in func_80000450
// spins on "while(osPiGetStatus() & PI_STATUS_DMA_BUSY);" — with no
// implementation the call returned garbage (often with the busy bit set),
// causing an infinite busy loop at 98% CPU and ANR.
// Since our osPiRawStartDma completes synchronously, status is never busy.
#define PI_STATUS_DMA_BUSY  0x01
uint32_t osPiGetStatus(void)           { return 0; }

// -----------------------------------------------------------------------
// VI stubs — the original functions crash because they dereference
// N64‑specific OSViMode structures that don't exist on Android.
// These are safe no‑ops so the game can initialise the VI manager
// without crashing.
// -----------------------------------------------------------------------
void osViSetMode(OSViMode *modep)                        { (void)modep; }
void osViSetSpecialFeatures(u32 func)                    { (void)func; }
void osViSwapBuffer(void *vaddr)                         { (void)vaddr; }
void osViSetEvent(OSMesgQueue *mq, OSMesg m, u32 count) { (void)mq; (void)m; (void)count; }
void osCreateViManager(OSPri pri)                        { (void)pri; }

// Original N64 inflate — we have a safe replacement in rarezip.c
// (bkboot_inflate_unlocked). The original writes directly to raw N64
// addresses and will crash. This stub prevents the linker from pulling
// in the dangerous original implementation.
s32 inflate(void) { return 0; }

// -----------------------------------------------------------------------
// Unknown decompiled functions
// -----------------------------------------------------------------------
int  func_8025C29C(void) { return 0; }
int  func_80253010(void) { return 0; }
void func_80253034(void *dst, int val, size_t size) { memset(dst, val, size); }
void func_8026A2E0(void) {}

// -----------------------------------------------------------------------
// Missing global variables & decompiled addresses
// -----------------------------------------------------------------------
#define BK_HEAP_SIZE 0x211120
u8 D_8002D500[BK_HEAP_SIZE] __attribute__((aligned(16)));

int   D_803FFE00  = 0;
int   D_803FBE00  = 0;
int   D_8000E800  = 0;
int   D_8023DA00  = 0;
int   D_803FFE10  = 0;

// -----------------------------------------------------------------------
// FRAMEBUFFER ALLOCATION IN RDRAM
// -----------------------------------------------------------------------
#define FB_WIDTH   292
#define FB_HEIGHT  216
#define FB_SIZE    (FB_WIDTH * FB_HEIGHT * sizeof(u16))
#define FB0_OFFSET 0x400000
#define FB1_OFFSET (FB0_OFFSET + FB_SIZE)

u16 gFramebuffers[2][FB_WIDTH * FB_HEIGHT];
u32 g_active_fb_offset = FB0_OFFSET;

// -----------------------------------------------------------------------
// Linker script symbols (ROM region boundaries)
// -----------------------------------------------------------------------
int crc_ROM_START            = 0;
int soundfont1ctl_ROM_START  = 0;
int soundfont1ctl_ROM_END    = 0;
int soundfont1tbl_ROM_START  = 0;
int soundfont2ctl_ROM_START  = 0;
int soundfont2ctl_ROM_END    = 0;
int soundfont2tbl_ROM_START  = 0;
int assets_ROM_START         = 0x5E90;  // FIXED: literal ROM offset from YAML
int boot_bk_boot_ROM_START   = 0;
int boot_bk_boot_ROM_END     = 0;
int n_aspMainTextStart        = 0;
int n_aspMainDataStart        = 0;
int gSPF3DEX_fifoTextStart   = 0;
int gSPF3DEX_fifoDataStart   = 0;
int gSPL3DEX_fifoTextStart   = 0;
int gSPL3DEX_fifoDataStart   = 0;
int gSPL3DEX_fifoTextEnd     = 0;

// -----------------------------------------------------------------------
// Overlay memory boundaries
// -----------------------------------------------------------------------
#define DEFINE_OVERLAY_VRAM(name, vram_start, vram_end) \
    u32 name##_VRAM        = vram_start; \
    u32 name##_VRAM_END    = vram_end;   \
    u32 name##_ROM_START   = 0; \
    u32 name##_ROM_END     = 0; \
    u32 name##_TEXT_START  = 0; \
    u32 name##_TEXT_END    = 0; \
    u32 name##_DATA_START  = 0; \
    u32 name##_RODATA_END  = 0; \
    u32 name##_BSS_START   = 0; \
    u32 name##_BSS_END     = 0;

DEFINE_OVERLAY_VRAM(core2,     0x80286F90, 0x80386DD0)
DEFINE_OVERLAY_VRAM(emptyLvl,  0x80386DD0, 0x80386DD0)
DEFINE_OVERLAY_VRAM(CC,        0x80386DD0, 0x8038A9E0)
DEFINE_OVERLAY_VRAM(MMM,       0x80386DD0, 0x8038CF10)
DEFINE_OVERLAY_VRAM(GV,        0x80386DD0, 0x803924F0)
DEFINE_OVERLAY_VRAM(TTC,       0x80386DD0, 0x8038E120)
DEFINE_OVERLAY_VRAM(MM,        0x80386DD0, 0x8038A680)
DEFINE_OVERLAY_VRAM(BGS,       0x80386DD0, 0x80391C30)
DEFINE_OVERLAY_VRAM(RBB,       0x80386DD0, 0x80391CD0)
DEFINE_OVERLAY_VRAM(FP,        0x80386DD0, 0x80393FD0)
DEFINE_OVERLAY_VRAM(CCW,       0x80386DD0, 0x803907D0)
DEFINE_OVERLAY_VRAM(SM,        0x80386DD0, 0x8038C010)
DEFINE_OVERLAY_VRAM(cutscenes, 0x80386DD0, 0x8038F3D0)
DEFINE_OVERLAY_VRAM(lair,      0x80386DD0, 0x80395E50)
DEFINE_OVERLAY_VRAM(fight,     0x80386DD0, 0x80393390)

u32 core1_VRAM     = 0x8023DA20;
u32 core1_VRAM_END = 0x80286F90;

// -----------------------------------------------------------------------
// Audio/SFX function stubs
// -----------------------------------------------------------------------
void gcsfx_playWithPitch(int a, float b, int c, float d)  { (void)a; (void)b; (void)c; (void)d; }
void func_8030E878(void)                                   {}
int  sfx_playFadeShorthand(void)                           { return 0; }
void gcsfx_playAtSampleRate(int a, int b, int c)           { (void)a; (void)b; (void)c; }
void func_8030E624(int a, float b, int c)                  { (void)a; (void)b; (void)c; }
void gcsfx_play(int a, float b, int c)                     { (void)a; (void)b; (void)c; }
void sfxSource_triggerCallbackByIndex(int a)               { (void)a; }
void func_8030E760(void)                                   {}
void func_8030DD90(int a, int b)                           { (void)a; (void)b; }
void sfxsource_playSfxAtVolume(int a, float b)             { (void)a; (void)b; }
void sfxsource_setSfxId(int a, int b)                      { (void)a; (void)b; }
void sfxSource_setunk43_7ByIndex(int a, int b)              { (void)a; (void)b; }
void sfxsource_setSampleRate(int a, int b)                 { (void)a; (void)b; }
void sfxSource_func_8030E2C4(int a)                        { (void)a; }
void sfxsource_freeSfxsourceByIndex(int a)                 { (void)a; }
int  sfxsource_createSfxsourceAndReturnIndex(void)         { return 0; }
void func_8030E9FC(void)                                   {}
void func_8030EA54(void)                                   {}
void func_8030E730(void)                                   {}
void func_8030DBFC(void)                                   {}
void sfxsource_set_fade_distances(int a, float b, float c) { (void)a; (void)b; (void)c; }
void sfxsource_set_position(int a, int b)                  { (void)a; (void)b; }
void func_8030E6D4(void)                                   {}
void func_8030ED2C(void)                                   {}
void func_8030DB04(void)                                   {}
void func_8030E200(int a)                                  { (void)a; }
void func_8030E0FC(void)                                   {}
void func_8030E3FC(int a)                                  { (void)a; }
void func_8030E58C(void)                                   {}
void sfxsource_playHighPriority(int a)                     { (void)a; }
void func_8030E988(void)                                   {}
void func_8030ED70(void)                                   {}
void sfxSource_setCallbackByIndex(int a, int b)            { (void)a; (void)b; }
void func_8030E5F4(void)                                   {}
void func_8030EB88(void)                                   {}
void func_8030EAAC(void)                                   {}
void func_8030E560(void)                                   {}
void func_8030E4E4(void)                                   {}
void func_8030EBC8(void)                                   {}
void func_8030E04C(void)                                   {}
void func_8030EB00(void)                                   {}
void func_8030EC20(void)                                   {}
void func_8030E9C4(void)                                   {}
void func_8030DFF0(void)                                   {}
void func_8030DFB4(void)                                   {}
void func_8030ED0C(void)                                   {}
void func_8030EDAC(void)                                   {}
int  sfxSource_getSampleRate(int a)                         { (void)a; return 0; }
void func_8030DE44(void)                                   {}
void func_8030E704(void)                                   {}
void func_8030DCCC(void)                                   {}

// -----------------------------------------------------------------------
// Music player stubs
// -----------------------------------------------------------------------
void coMusicPlayer_init(void)  {}
void coMusicPlayer_free(void)  {}
void coMusicPlayer_update(void) {}

// -----------------------------------------------------------------------
// itemPrint stubs
// -----------------------------------------------------------------------
void itemPrint_init(void)  {}
void itemPrint_update(void) {}
void itemPrint_free(void) {}
void itemPrint_draw(void *a, void *b, void *c) { (void)a; (void)b; (void)c; }
void itemPrint_defrag(void) {}

// -----------------------------------------------------------------------
// Graphics helper stubs
// -----------------------------------------------------------------------
void func_80253208(void *a, int b, int c, int d, int e, void *f) { (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; }
void zBuffer_set(void *a)                           { (void)a; }
void func_802476EC(void *a)                         { (void)a; }
void func_80246670(void *a)                         { (void)a; }
void func_802E67AC(void) {}
void func_802E67C4(void) {}
void func_802E5F10(void *a) { (void)a; }
void func_802E53EC4(void *a, void *b) { (void)a; (void)b; }
void printbuffer_draw(void *a, void *b, void *c) { (void)a; (void)b; (void)c; }
void printbuffer_defrag(void) {}
void print_init(void) {}
void depthbuffer_enable(int a) { (void)a; }
void func_802E5F38(void) {}
void func_802E4E54(int a) { (void)a; }
void modelRender_init(void) {}
void modelRender_free(void) {}
void modelRender_defrag(void) {}
void viewport_reset(void) {}
void viewport_setNearAndFar(float a, float b) { (void)a; (void)b; }
void viewport_setPosition_f3(float a, float b, float c) { (void)a; (void)b; (void)c; }
void viewport_setRotation_f3(float a, float b, float c) { (void)a; (void)b; (void)c; }
void viewport_moveAlongZAxis(float a) { (void)a; }
void viewport_update(void) {}
void viewport_debug(void) {}
void viewport_pushFramebufferExtendsToVpStack(void) {}
void func_8033B5FC(void) {}
void func_8033B61C(void) {}
void func_8033B268(void) {}
void mapSavestate_defrag_all(void) {}
void gctransition_defrag(void) {}
void comusic_defrag(void) {}
void func_80350E00(void) {}

// Audio callback chain stubs
void func_8025A9D4(int a, int b) { (void)a; (void)b; }
void func_8025A7DC(int a) { (void)a; }
void func_8025A23C(int a) { (void)a; }
void func_8024E698(int a) { (void)a; }
void func_8024F150(void) {}
void func_8024F764(int a) { (void)a; }
void func_8024F7C4(int a) { (void)a; }
void func_8024FB8C(void) {}
int func_803226E8(int a) { (void)a; return 0; }
int func_80322914(void) { return 0; }
void func_8025A430(int a, int b, int c) { (void)a; (void)b; (void)c; }
void func_8025A2B0(void) {}
int controller_getStartButton(int a) { (void)a; return 0; }

// Misc stubs
void func_80334E1C(int a, int b) { (void)a; (void)b; }
void func_80323140(int a, int b) { (void)a; (void)b; }
void func_8032278C(void) {}
int func_8034BDA4(int a, int b) { (void)a; (void)b; return 0; }
void func_80346CA8(void) {}
void func_8030C1A0(void) {}
void func_8030C204(void) {}
void gcpausemenu_init(void) {}
void gcpausemenu_free(void) {}
int gcpausemenu_80314B00(void) { return 1; }
int gcPauseMenu_update(void) { return 0; }
int cutscenetrigger_update(void) { return 0; }
int gctransition_8030BDC0(void) { return 0; }
int gctransition_done(void) { return 1; }
void gctransition_8030BEA4(int a) { (void)a; }
void gctransition_8030BD4C(void) {}
void gctransition_8030BE60(void) {}
void gctransition_update(void) {}
void gctransition_draw(void *a, void *b, void *c) { (void)a; (void)b; (void)c; }
int func_8028F070(void) { return 1; }
int func_8028EC04(void) { return 0; }
int player_isDead(void) { return 0; }
void mapSavestate_apply(int a) { (void)a; }
void mapSavestate_save(int a) { (void)a; }
int gsworld_get_map(void) { return 0; }

// World init sub-function stubs
void sns_save_and_update_global_data(void) {}
void func_8030D86C(void) {}
void func_80322764(void) {}
void timedFuncQueue_init(void) {}
void func_802F9CD8(void) {}
void func_8031B62C(void) {}
void defragManager_init(void) {}
void animCache_init(void) {}
void rand_reset(void) {}
void scissorBox_setDefault(void) {}
void func_80253FE8(void) {}
void time_reset(void) {}
void func_8033DC04(void) {}
void clearScoreStates(void) {}
void savedata_init(void) {}
void func_802E3854(void) {}
void func_802E3800(void) {}
void func_8033DC10(void) {}
void func_80324C58(void) {}
void picturebox_init(void) {}
void picturebox_free(void) {}
void func_802FA508(void) {}
void func_802E49E0(void) {}
int func_802E4A08(void) { return 0; }
int func_8032056C(void) { return 1; }
int func_8032190C(void) { return 0; }
int levelSpecificFlags_validateCRC1(void) { return 1; }
int dummy_func_80320248(void) { return 1; }
int func_80320240(void) { return 1; }
int map_getLevel(int a) { (void)a; return 0; }
int level_get(void) { return 0; }
void func_80321854(void) {}
void func_8030AFD8(int a) { (void)a; }

// =======================================================================
// D_8037E8E0 game state struct (from code_5C870.c)
// =======================================================================
struct game_state_s {
    s32 unk0;
    s32 game_mode;
    f32 unk8;
    s32 unkC;
    f32 unk10;
    u8 transition;
    u8 map;
    u8 exit;
    u8 unk17;
    u8 unk18;
    u8 unk19;
    u8 unk1A;
    u8 unk1B;
    u8 unk1C;
};

// Provide the actual struct instance — this is the single source of truth.
// The original code_5C870.c also defines D_8037E8E0 in its .bss.
// With --allow-multiple-definition, the linker picks one; we provide
// a correctly sized instance here so func_802E4214 can write to it.
struct game_state_s D_8037E8E0;

#define TRANSITION_0_NONE   0
#define GAME_MODE_2_UNKNOWN 2
#define GAME_MODE_3_NORMAL  3

// =======================================================================
// REAL func_802E4214 — World Init (from code_5C870.c)
// =======================================================================
void func_802E4214(s32 map_id) {
    LOGI("BKA-STUBS: func_802E4214 REAL — initialising game world for map %d", map_id);

    D_8037E8E0.transition = TRANSITION_0_NONE;
    D_8037E8E0.unk19 = 0;
    D_8037E8E0.unk18 = 0;
    D_8037E8E0.map = 0;
    D_8037E8E0.exit = 0;
    D_8037E8E0.unk17 = 0;
    D_8037E8E0.unk1B = 0;
    D_8037E8E0.unk1A = 0;
    D_8037E8E0.unkC = 0;
    D_8037E8E0.unk1C = 0;

    savedata_init();
    sns_save_and_update_global_data();
    func_8030D86C();
    coMusicPlayer_init();
    func_80322764();
    timedFuncQueue_init();
    func_802F9CD8();
    func_8031B62C();

    if (!func_802E4A08()) {
        print_init();
    }

    func_802E5F38();
    defragManager_init();
    modelRender_init();
    depthbuffer_enable(1);
    animCache_init();
    viewport_reset();
    viewport_setNearAndFar(1.0f, 10000.0f);
    rand_reset();
    scissorBox_setDefault();
    func_80253FE8();
    time_reset();
    func_8033DC04();
    clearScoreStates();

    D_8037E8E0.game_mode = GAME_MODE_2_UNKNOWN;
    D_8037E8E0.unk8 = 0.0f;

    LOGI("BKA-STUBS: func_802E4214 — loading level data for map %d", map_id);

    func_803216D0(map_id);
    func_8030AFA0(map_id);
    func_802E3854();
    func_802E38E8(map_id, 0, 0);

    D_8037E8E0.unk0 = 0;
    game_setMode(GAME_MODE_3_NORMAL, 1);

    LOGI("BKA-STUBS: func_802E4214 — world init complete, entering GAME_MODE_3_NORMAL");
}

// =======================================================================
// REAL func_803216D0 — Level Overlay Loader (from code_9A740.c)
// =======================================================================
void func_803216D0(s32 map) {
    LOGI("BKA-STUBS: func_803216D0 — loading overlay for map %d", map);
    // Real implementation would:
    //   D_80383300.level = map_getLevel(map);
    //   overlayManager_load(leveloverlay_getOverlayFromLevel(level));
    //   mapSavestate_init();
    //   itemscore_levelReset(level);
    //   jiggyscore_clearAllSpawned();
    //   levelSpecificFlags_clear();
    //   bsStoredState_clearTimers();
    //   func_803219A8();
    // For now, these are all stubbed — the level data will load
    // via the asset cache when gsworld_load is called.
}

// =======================================================================
// REAL func_8030AFA0 — Jiggy List Setup (from gc/section.c)
// =======================================================================
void func_8030AFA0(s32 map) {
    LOGI("BKA-STUBS: func_8030AFA0 — setting jiggy list for map %d", map);
    // Real implementation just calls:
    //   jiggylist_set_level(map);
}

// =======================================================================
// REAL func_802E38E8 — World Setup Dispatch (from code_5C870.c)
// =======================================================================
void func_802E38E8(s32 map, s32 exit, s32 reset_on_load) {
    LOGI("BKA-STUBS: func_802E38E8 — map=%d exit=%d reset=%d", map, exit, reset_on_load);
    // Real implementation:
    //   if (reset_on_load || level_get() != map_getLevel(map)) {
    //       func_8030AFD8(1);
    //       func_80321854();
    //       func_803216D0(map);
    //       func_8030AFA0(map);
    //   } else {
    //       func_8030AFD8(1);
    //       func_8030AFA0(map);
    //   }
    func_802FA508();
    gsworld_set(map, exit, 0);
    func_802E3800();
    func_8033DC10();
}

// =======================================================================
// REAL game_setMode — Game Mode Transition (from code_5C870.c)
// Simplified: just sets the mode and enables update/draw
// =======================================================================
void game_setMode(s32 next_mode, s32 arg1) {
    LOGI("BKA-STUBS: game_setMode — transitioning to mode %d (arg1=%d)", next_mode, arg1);
    s32 prev_mode = D_8037E8E0.game_mode;
    D_8037E8E0.game_mode = next_mode;

    if (next_mode == GAME_MODE_3_NORMAL) {
        gsworld_setEnableUpdate(1);
        gsworld_setEnableDraw(1);
    }
    (void)prev_mode;
    (void)arg1;
}

// =======================================================================
// gsworld state (from gsworld.c)
// =======================================================================
static int sGsWorldData_map   = 0;
static int sGsWorldData_exit  = 0;
static int sGsWorldData_unk0  = 0;
static int sEnableUpdate      = 1;
static int sEnableDraw        = 1;

// =======================================================================
// REAL gsworld_set — World Setup (from gsworld.c)
// Calls 40+ init functions, then gsworld_load(map)
// =======================================================================
void gsworld_set(s32 map, s32 exit, s32 reload) {
    LOGI("BKA-STUBS: gsworld_set — map=%d exit=%d reload=%d", map, exit, reload);
    sGsWorldData_map = map;
    sGsWorldData_exit = exit;
    sEnableUpdate = 1;
    sEnableDraw = 1;

    // The real gsworld_set calls ~40 init functions:
    //   leveloverlay_init, func_802D2CB8, core1_7090_alloc,
    //   musicTrack_load, AnimTextureListCache_init, func_80320B84,
    //   func_8034C97C, func_8030A078, func_8031B718, playerModel_set,
    //   itemPrint_init, dialogBin_initialize, spawnQueue_malloc,
    //   func_803329AC, func_80350BFC, func_80323190, func_80332894,
    //   func_803305AC, func_8031F9E8, func_80323230, commonParticleType_init,
    //   animBinCache_init, animsprite_init, func_80344C50, func_8033F9C0,
    //   ncCameraNodeList_init, nccamera_init, partEmitMgr_init,
    //   pem_setAllInactive, pem_initDependencies, func_802F7D30,
    //   propModelList_init, lighting_init, sky_reset, func_803343D0,
    //   cubeList_init, func_802FA69C, commonParticle_init,
    //   gsworld_load(map), func_80305990, func_8030C740, gcdialog_init,
    //   mapSpecificFlags_clearAll, func_803411B0, spawnQueue_reset, ...
    // All these are currently stubbed — they'll be un-stubbed incrementally.

    if (!reload) {
        gsworld_load(map);
    }
}

// =======================================================================
// Forward decls for gsworld_load dependencies
// =======================================================================
struct File { int mode; int last_expected; int unk80; void* asset_base_ptr; void* asset_current_ptr; void* base_ptr; void* current_ptr; void* end_ptr; };
typedef struct File File;
extern File* file_openMap(s32 map_id);
extern void file_close(File* f);
extern int file_isNextByteExpected(File* f, int expected);
extern void cubeList_fromFile(File* f);
extern void ncCameraNodeList_fromFile(File* f);
extern void lightingVectorList_fromFile(File* f);

// Stubs for the file reading helpers
void cubeList_fromFile(File* f)           { (void)f; }
void ncCameraNodeList_fromFile(File* f)   { (void)f; }
void lightingVectorList_fromFile(File* f) { (void)f; }

// =======================================================================
// REAL gsworld_load — Load Map Data File (from gsworld.c)
// Opens the map's asset file and reads cubes/cameras/lighting
// =======================================================================
void gsworld_load(s32 map_id) {
    LOGI("BKA-STUBS: gsworld_load — loading map %d", map_id);

    // The real implementation:
    //   File* f = file_openMap(map_id);
    //   while (!file_isNextByteExpected(f, 0)) {
    //       if (file_isNextByteExpected(f, 1)) cubeList_fromFile(f);
    //       else if (file_isNextByteExpected(f, 3)) ncCameraNodeList_fromFile(f);
    //       else if (file_isNextByteExpected(f, 4)) lightingVectorList_fromFile(f);
    //   }
    //   file_close(f);
    //
    // file_openMap calls assetcache_get(map_id + 0x71C) which uses
    // piMgr_read → ResourceMgr_HandleDma to read from rom_base.bin.
    // Once the asset cache is verified working, the File* calls above
    // will actually read level data and populate cubes/cameras/lighting.
}

// =======================================================================
// REAL gsworld_draw — Main Rendering Dispatch (from gsworld.c)
// Builds display lists for sky, map models, sprites, particles, HUD
// =======================================================================
void gsworld_draw(void** gfx, void** mtx, void** vtx) {
    if (!sEnableDraw) {
        // drawRectangle2D(gfx, 0, 0, gFramebufferWidth, gFramebufferHeight, 0, 0, 0);
        // viewport_setNearAndFar(near, far);
        // viewport_setRenderViewportAndPerspectiveMatrix(gfx, mtx);
        return;
    }

    // The real gsworld_draw calls in order:
    //   spawnQueue_unlock();
    //   sky_draw(gfx, mtx, vtx);
    //   viewport_setRenderViewportAndPerspectiveMatrix(gfx, mtx);
    //   if (mapModel_has_xlu_bin()) { ... XLU path ... }
    //   else {
    //       mapModel_opa_draw(gfx, mtx, vtx);
    //       leveloverlay_drawCallback(gfx, mtx, vtx);
    //       player_draw(gfx, mtx, vtx);
    //       func_80302C94(gfx, mtx, vtx);
    //       jiggylist_draw(gfx, mtx, vtx);
    //       func_803500D8(gfx, mtx, vtx);
    //       func_802D520C(gfx, mtx, vtx);  // 2D sprites/overlay
    //       partEmitMgr_draw(gfx, mtx, vtx);
    //   }
    //   spawnQueue_lock();
    //
    // All these draw functions are currently stubbed.
    // When un-stubbed, they will build N64 display lists into the
    // Gfx buffers, which Thread 5 sends to the RSP.
}

// =======================================================================
// REAL gsworld_update — Per-Frame Update (from gsworld.c)
// =======================================================================
int gsworld_update(void) {
    if (!sEnableUpdate) {
        return 1;
    }
    // The real gsworld_update calls ~30 update functions:
    //   commonParticle_update, pem_updateAll, animCache_update,
    //   ncCamera_update, sky_update, partEmitMgr_update, etc.
    // All currently stubbed.
    return 1;
}

// =======================================================================
// REAL gsworld_setEnableUpdate / gsworld_setEnableDraw (from gsworld.c)
// =======================================================================
void gsworld_setEnableUpdate(int value) { sEnableUpdate = value; }
void gsworld_setEnableDraw(int value)   { sEnableDraw = value; }
int gsworld_getEnableUpdate(void)       { return sEnableUpdate; }
int gsworld_getEnableDraw(void)         { return sEnableDraw; }