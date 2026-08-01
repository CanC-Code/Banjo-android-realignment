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
//       lowlevel_bridge.cpp (osPiReadIo, osPiWriteIo, g_active_fb_offset,
//                            gFramebuffers)
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
typedef int32_t  s32;
typedef uint8_t  u8;

// -----------------------------------------------------------------------
// OS / Hardware
// -----------------------------------------------------------------------
OSIntMask __osDisableInt(void) { return 0; }
void      __osRestoreInt(OSIntMask mask) { (void)mask; }
void bzero(void *s, int n)                    { memset(s, 0, (size_t)n); }
void bcopy(const void *src, void *dest, int n){ memmove(dest, src, (size_t)n); }
void osWritebackDCache(void *vaddr, int32_t nbytes)  { (void)vaddr; (void)nbytes; }
void osInvalICache(void *vaddr, int32_t nbytes)       { (void)vaddr; (void)nbytes; }
void osInvalDCache(void *vaddr, int32_t nbytes)       { (void)vaddr; (void)nbytes; }
void osWriteBackDCacheAll(void)                       {}
void __osInitialize_autodetect(void)                  {}
void __osCleanupThread(void) {}
uint32_t osGetCount(void)              { return 0; }
uint32_t __osGetSR(void)               { return 0; }
uint32_t ___osGetSR(void)              { return 0; }
void     __osSetSR(uint32_t sr)        { (void)sr; }
void     __osSetFpcCsr(uint32_t csr)   { (void)csr; }
void     __osSetCompare(uint32_t val)  { (void)val; }
void     osMapTLBRdb(void)             {}
uint32_t __osProbeTLB(void* a)         { (void)a; return 0; }
#define PI_STATUS_DMA_BUSY  0x01
uint32_t osPiGetStatus(void)           { return 0; }
// NOTE: osViSetMode, osViSetEvent, osCreateViManager are now in stubs.cpp
// with proper N64 types to avoid conflicts with os_vi.h declarations.
void osViSetSpecialFeatures(u32 func)                { (void)func; }
void osViSwapBuffer(void *vaddr)                     { (void)vaddr; }
s32 inflate(void) { return 0; }

// -----------------------------------------------------------------------
// Misc functions
// -----------------------------------------------------------------------
int  func_8025C29C(void) { return 0; }
int  func_80253010(void) { return 0; }
void func_80253034(void *dst, int val, size_t size) { memset(dst, val, size); }
void func_8026A2E0(void) {}

// -----------------------------------------------------------------------
// Global variables
// -----------------------------------------------------------------------

// D_8002D500 is the N64 game heap. memory.c uses it as:
//   extern EmptyHeapBlock D_8002D500[LAST_HEAP_BLOCK + 1];
// EmptyHeapBlock is 0x20 bytes. We allocate a byte array large enough
// for heap_init() to set up the linked list correctly.
#define BK_HEAP_SIZE 0x211120
u8 D_8002D500[BK_HEAP_SIZE] __attribute__((aligned(16)));

// D_8023DA00 is used by memory.c as:
//   extern EmptyHeapBlock D_8023DA00;
// func_80254BD0 walks it as a linked list: var_v1 = &D_8023DA00;
// then var_v1 = var_v1->prev_free. Allocate 0x20 bytes to match
// sizeof(EmptyHeapBlock).
uint64_t D_8023DA00[4] __attribute__((aligned(16)));

// D_803FFE00 is used as u32[4] by bk_boot_1050.c and SM/code_F0.c.
// bk_boot_1050.c stores CRC values: D_803FFE00[0]=crc1, [1]=crc2, [2]=crc1, [3]=crc2.
// SM/code_F0.c validates: osPiReadIo(crc_ROM_START+8) == D_803FFE00[0], etc.
// Previously "int D_803FFE00 = 0" (4 bytes) — reading [1]/[2]/[3] corrupted stack.
uint32_t D_803FFE00[4] = {0, 0, 0, 0};

// D_8000E800 is used as a temporary buffer during overlay loading.
// overlay.c passes &D_8000E800 to piMgr_read for decompression workspace.
// The compressed overlay data can be up to ~512KB. Allocate 1MB to be safe.
// Previously "int D_8000E800 = 0" (4 bytes) — DMA overflowed into adjacent memory.
uint8_t D_8000E800[0x100000] __attribute__((aligned(16)));

// D_803FFE10 is used by overlay.c as: extern struct49s D_803FFE10[];
// struct49s is { u32 unk0; u32 unk4; } — 8 bytes per entry.
// There are 15 overlays (indices 0-14). overlay_load reads:
//   rom_start = D_803FFE10[overlay_id].unk0;
//   rom_end   = D_803FFE10[overlay_id].unk4;
// Previously "int D_803FFE10 = 0" (4 bytes) — reading D_803FFE10[0].unk4
// read past the allocation and got garbage, causing a massive piMgr_read
// that overflowed the destination buffer.
//
// NOTE: D_803FFE10 is also referenced from code_0.c which uses it
// in the boot path. These values are populated at runtime from the
// asset cache / ROM header. Zero-init means rom_start=rom_end=0,
// so piMgr_read will do a zero-byte transfer (safe no-op).
uint64_t D_803FFE10[15] __attribute__((aligned(8)));

// D_803FBE00 is used by the audio manager (stubbed, size unknown but
// referenced from code_1D00.c). Allocate a reasonable buffer.
// Previously "int D_803FBE00 = 0" (4 bytes).
uint8_t D_803FBE00[0x2000] __attribute__((aligned(16)));

// NOTE: gFramebuffers, g_active_fb_offset are now defined in lowlevel_bridge.cpp

// -----------------------------------------------------------------------
// ROM symbols
// -----------------------------------------------------------------------
int crc_ROM_START            = 0;
int soundfont1ctl_ROM_START  = 0;
int soundfont1ctl_ROM_END    = 0;
int soundfont1tbl_ROM_START  = 0;
int soundfont2ctl_ROM_START  = 0;
int soundfont2ctl_ROM_END    = 0;
int soundfont2tbl_ROM_START  = 0;
int assets_ROM_START         = 0x5E90;
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
// Overlay VRAM
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
// Audio/SFX stubs
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
// Music / print / graphics stubs
// -----------------------------------------------------------------------
void coMusicPlayer_init(void)  {}
void coMusicPlayer_free(void)  {}
void coMusicPlayer_update(void) {}
void itemPrint_init(void)  {}
void itemPrint_update(void) {}
void itemPrint_free(void) {}
void itemPrint_draw(void *a, void *b, void *c) { (void)a; (void)b; (void)c; }
void itemPrint_defrag(void) {}
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

// -----------------------------------------------------------------------
// Misc game stubs
// -----------------------------------------------------------------------
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
// FORWARD DECLARATIONS for func_802E4214's callees
// =======================================================================
void func_803216D0(s32 map);
void func_8030AFA0(s32 map);
void func_802E38E8(s32 map, s32 exit, s32 reset_on_load);
void game_setMode(s32 next_mode, s32 arg1);
void gsworld_set(s32 map, s32 exit, s32 reload);
void gsworld_load(s32 map_id);
void gsworld_setEnableUpdate(int value);
void gsworld_setEnableDraw(int value);
int  gsworld_getEnableUpdate(void);
int  gsworld_getEnableDraw(void);

// =======================================================================
// D_8037E8E0 game state struct
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

struct game_state_s D_8037E8E0;

#define TRANSITION_0_NONE   0
#define GAME_MODE_2_UNKNOWN 2
#define GAME_MODE_3_NORMAL  3

// =======================================================================
// REAL func_802E4214 — World Init
// =======================================================================
void func_802E4214(s32 map_id) {
    LOGI("BKA-STUBS: func_802E4214 REAL - init world for map %d", map_id);

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

    LOGI("BKA-STUBS: func_802E4214 - loading level data for map %d", map_id);

    func_803216D0(map_id);
    func_8030AFA0(map_id);
    func_802E3854();
    func_802E38E8(map_id, 0, 0);

    D_8037E8E0.unk0 = 0;
    game_setMode(GAME_MODE_3_NORMAL, 1);

    LOGI("BKA-STUBS: func_802E4214 - world init complete");
}

// =======================================================================
// REAL func_803216D0 — Level Overlay Loader
// =======================================================================
void func_803216D0(s32 map) {
    LOGI("BKA-STUBS: func_803216D0 - loading overlay for map %d", map);
}

// =======================================================================
// REAL func_8030AFA0 — Jiggy List Setup
// =======================================================================
void func_8030AFA0(s32 map) {
    LOGI("BKA-STUBS: func_8030AFA0 - setting jiggy list for map %d", map);
}

// =======================================================================
// REAL func_802E38E8 — World Setup Dispatch
// =======================================================================
void func_802E38E8(s32 map, s32 exit, s32 reset_on_load) {
    LOGI("BKA-STUBS: func_802E38E8 - map=%d exit=%d reset=%d", map, exit, reset_on_load);
    func_802FA508();
    gsworld_set(map, exit, 0);
    func_802E3800();
    func_8033DC10();
}

// =======================================================================
// REAL game_setMode — Game Mode Transition
// =======================================================================
void game_setMode(s32 next_mode, s32 arg1) {
    LOGI("BKA-STUBS: game_setMode - mode %d (arg1=%d)", next_mode, arg1);
    D_8037E8E0.game_mode = next_mode;
    if (next_mode == GAME_MODE_3_NORMAL) {
        gsworld_setEnableUpdate(1);
        gsworld_setEnableDraw(1);
    }
}

// =======================================================================
// gsworld state
// =======================================================================
static int sEnableUpdate = 1;
static int sEnableDraw   = 1;

// =======================================================================
// REAL gsworld_set
// =======================================================================
void gsworld_set(s32 map, s32 exit, s32 reload) {
    LOGI("BKA-STUBS: gsworld_set - map=%d exit=%d reload=%d", map, exit, reload);
    sEnableUpdate = 1;
    sEnableDraw = 1;
    if (!reload) {
        gsworld_load(map);
    }
}

// =======================================================================
// REAL gsworld_load
// =======================================================================
void gsworld_load(s32 map_id) {
    LOGI("BKA-STUBS: gsworld_load - loading map %d", map_id);
}

// =======================================================================
// REAL gsworld_draw
// =======================================================================
void gsworld_draw(void** gfx, void** mtx, void** vtx) {
    if (!sEnableDraw) return;
}

// =======================================================================
// REAL gsworld_update
// =======================================================================
int gsworld_update(void) {
    if (!sEnableUpdate) return 1;
    return 1;
}

// =======================================================================
// REAL gsworld_setEnableUpdate / gsworld_setEnableDraw
// =======================================================================
void gsworld_setEnableUpdate(int value) { sEnableUpdate = value; }
void gsworld_setEnableDraw(int value)   { sEnableDraw = value; }
int gsworld_getEnableUpdate(void)       { return sEnableUpdate; }
int gsworld_getEnableDraw(void)         { return sEnableDraw; }