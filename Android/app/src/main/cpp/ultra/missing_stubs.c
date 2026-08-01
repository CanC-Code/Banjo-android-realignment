// missing_stubs.c
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
// FIXED: func_80253034 must actually clear the framebuffer. The old stub
// returned 0 and accepted no arguments, leaving the framebuffer uninitialised
// and displaying whatever garbage was in memory. The real implementation
// (missing from the decompilation) is a simple memset with size passed in.
void func_80253034(void *dst, int val, size_t size) {
    memset(dst, val, size);
}
void func_8026A2E0(void) {}

// -----------------------------------------------------------------------
// Missing global variables & decompiled addresses
// -----------------------------------------------------------------------

// CRITICAL FIX: D_8002D500 is the N64 game heap at RDRAM offset 0x002D500.
// The heap is HEAP_SIZE bytes (~2.1MB). func_80000450 writes core1 compressed
// ROM data here via osPiRawStartDma, then decompresses it in-place.
// memory.c uses it as: extern EmptyHeapBlock D_8002D500[LAST_HEAP_BLOCK + 1];
// bk_boot_1050.c uses it as: extern u8 D_8002D500; tmp = &D_8002D500;
//
// Previously this was "int D_8002D500 = 0" (4 bytes!) causing the DMA to
// overflow into adjacent memory and crash. It was then changed to
// "int* const D_8002D500 = (int*)g_heap_backing;" but that made &D_8002D500
// return the address of the pointer variable (8 bytes) instead of the buffer
// address, so the DMA corrupted the pointer and crashed.
//
// The fix: D_8002D500 MUST be the buffer itself, so that &D_8002D500
// returns the buffer address. We allocate it as a properly sized array.
// The address resolver in src/done/rarezip.c (bka_resolve_ptr) handles
// mapping N64 address 0x8002D500 to gN64_RDRAM + 0x002D500 when RDRAM
// is active, or falls back to this buffer for direct host-pointer access.
#define BK_HEAP_SIZE 0x211120  // VER_SELECT: 0x210520 (v10) or 0x211120 (pal)
u8 D_8002D500[BK_HEAP_SIZE] __attribute__((aligned(16)));

int   D_803FFE00  = 0;
int   D_803FBE00  = 0;
int   D_8000E800  = 0;
int   D_8023DA00  = 0;
int   D_803FFE10  = 0;

// -----------------------------------------------------------------------
// FRAMEBUFFER ALLOCATION IN RDRAM
// The game draws into a double-buffered framebuffer in N64 RDRAM.
// Each buffer is 320×240×2 bytes (153,600 bytes), placed at a safe
// offset past the heap (0x400000 = 4 MB). The video plugin reads
// from gN64_RDRAM + g_active_fb_offset to upload each frame to GL.
// -----------------------------------------------------------------------
#define FB_WIDTH   292
#define FB_HEIGHT  216
#define FB_SIZE    (FB_WIDTH * FB_HEIGHT * sizeof(u16))  // 126,144 bytes
#define FB0_OFFSET 0x400000  // 4 MB – well past the ~2.1 MB heap
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
int assets_ROM_START         = 0;
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
// Overlay memory boundaries — actual N64 virtual addresses
// overlaymanager.c requires all these symbols to exist.
// We provide the real VRAM start/end and zero for the rest.
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
DEFINE_OVERLAY_VRAM(emptyLvl,  0x80386DD0, 0x80386DD0)   // unused, size 0
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

// core1 is not in the overlay table but may be referenced elsewhere.
u32 core1_VRAM     = 0x8023DA20;
u32 core1_VRAM_END = 0x80286F90;

// -----------------------------------------------------------------------
// Audio/SFX function stubs
// These are called by many actor files but were originally part of
// audioManager_init which is currently stubbed. Provide empty stubs
// so the game can run without sound for now.
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

// Additional stubs needed after build iteration
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

// Third batch of audio/SFX stubs from the latest build
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
// Music player stubs – prevent early allocation crashes
// -----------------------------------------------------------------------
void coMusicPlayer_init(void)  {}
void coMusicPlayer_free(void)  {}
void coMusicPlayer_update(void) {}

// -----------------------------------------------------------------------
// itemPrint stubs – prevent null-pointer crash during early main loop
// -----------------------------------------------------------------------
void itemPrint_init(void)  {}
void itemPrint_update(void) {}
void itemPrint_free(void) {}
void itemPrint_draw(void *a, void *b, void *c) { (void)a; (void)b; (void)c; }
void itemPrint_defrag(void) {}

// -----------------------------------------------------------------------
// Graphics helper stubs – prevent null-pointer crash during rendering
// -----------------------------------------------------------------------
void func_80253208(void *a, int b, int c, int d, int e, void *f) {
    (void)a; (void)b; (void)c; (void)d; (void)e; (void)f;
}
void zBuffer_set(void *a)                           { (void)a; }
void func_802476EC(void *a)                         { (void)a; }
void func_80246670(void *a)                         { (void)a; }
// Additional print buffer stubs
void func_802E67AC(void) {}
void func_802E67C4(void) {}
void func_802E5F10(void *a) { (void)a; }
void func_802E53EC4(void *a, void *b) { (void)a; (void)b; }
void printbuffer_draw(void *a, void *b, void *c) { (void)a; (void)b; (void)c; }
void printbuffer_defrag(void) {}
// print_init stub — missing from Android source
void print_init(void) {}
// Additional stubs needed for func_802E4214 re-enablement
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
void gsworld_set(int a, int b, int c) { (void)a; (void)b; (void)c; }
void func_803216D0(int a) { (void)a; }
void func_8030AFA0(int a) { (void)a; }
void func_8030AFD8(int a) { (void)a; }
void func_80321854(void) {}
int level_get(void) { return 0; }
int map_getLevel(int a) { (void)a; return 0; }
void func_8033B5FC(void) {}
void func_8033B61C(void) {}
void func_8033B268(void) {}
void mapSavestate_defrag_all(void) {}
void gctransition_defrag(void) {}
void comusic_defrag(void) {}
void func_80350E00(void) {}
void func_8033DC10(void) {}
// Audio callback chain stubs for game_setMode
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