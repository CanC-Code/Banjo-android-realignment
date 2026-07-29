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

// -----------------------------------------------------------------------
// Unknown decompiled functions
// -----------------------------------------------------------------------
int  func_8025C29C(void) { return 0; }
int  func_80253010(void) { return 0; }
int  func_80253034(void) { return 0; }
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
void* gFramebuffers[3] = {0, 0, 0};

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
// Overlay memory boundaries
// -----------------------------------------------------------------------
#define DEFINE_OVERLAY(name) \
    int name##_VRAM        = 0; \
    int name##_VRAM_END    = 0; \
    int name##_ROM_START   = 0; \
    int name##_ROM_END     = 0; \
    int name##_TEXT_START  = 0; \
    int name##_TEXT_END    = 0; \
    int name##_DATA_START  = 0; \
    int name##_RODATA_END  = 0; \
    int name##_BSS_START   = 0; \
    int name##_BSS_END     = 0;

DEFINE_OVERLAY(core2)
DEFINE_OVERLAY(emptyLvl)
DEFINE_OVERLAY(SM)
DEFINE_OVERLAY(MM)
DEFINE_OVERLAY(TTC)
DEFINE_OVERLAY(CC)
DEFINE_OVERLAY(BGS)
DEFINE_OVERLAY(FP)
DEFINE_OVERLAY(GV)
DEFINE_OVERLAY(MMM)
DEFINE_OVERLAY(RBB)
DEFINE_OVERLAY(CCW)
DEFINE_OVERLAY(lair)
DEFINE_OVERLAY(fight)
DEFINE_OVERLAY(cutscenes)