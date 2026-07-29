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
// Overlay memory boundaries — actual N64 virtual addresses
// These are needed by overlaymanager.c to know where to decompress overlays.
// -----------------------------------------------------------------------

// core1 runs at its fixed VRAM range
u32 core1_VRAM     = 0x8023DA20;
u32 core1_VRAM_END = 0x80286F90;

// core2 is the first dynamically loaded overlay
u32 core2_VRAM     = 0x80286F90;
u32 core2_VRAM_END = 0x80386DD0;

// All level overlays share the same base VRAM address.
// Their sizes differ, but the start is always right after core2.
#define LEVEL_VRAM      0x80386DD0

u32 emptyLvl_VRAM        = LEVEL_VRAM;
u32 emptyLvl_VRAM_END    = 0x80386DD0;   // small / unused

u32 SM_VRAM              = LEVEL_VRAM;
u32 SM_VRAM_END          = 0x8038C010;
u32 MM_VRAM              = LEVEL_VRAM;
u32 MM_VRAM_END          = 0x8038A680;
u32 TTC_VRAM             = LEVEL_VRAM;
u32 TTC_VRAM_END         = 0x8038E120;
u32 CC_VRAM              = LEVEL_VRAM;
u32 CC_VRAM_END          = 0x8038A9E0;
u32 BGS_VRAM             = LEVEL_VRAM;
u32 BGS_VRAM_END         = 0x80391C30;
u32 FP_VRAM              = LEVEL_VRAM;
u32 FP_VRAM_END          = 0x80393FD0;
u32 GV_VRAM              = LEVEL_VRAM;
u32 GV_VRAM_END          = 0x803924F0;
u32 MMM_VRAM             = LEVEL_VRAM;
u32 MMM_VRAM_END         = 0x8038CF10;
u32 RBB_VRAM             = LEVEL_VRAM;
u32 RBB_VRAM_END         = 0x80391CD0;
u32 CCW_VRAM             = LEVEL_VRAM;
u32 CCW_VRAM_END         = 0x803907D0;
u32 lair_VRAM            = LEVEL_VRAM;
u32 lair_VRAM_END        = 0x80395E50;
u32 fight_VRAM           = LEVEL_VRAM;
u32 fight_VRAM_END       = 0x80393390;
u32 cutscenes_VRAM       = LEVEL_VRAM;
u32 cutscenes_VRAM_END   = 0x8038F3D0;

// The remaining fields (ROM_START, TEXT_START, etc.) are currently unused
// and can stay zero. If you encounter linker errors about missing symbols,
// define them here using the pattern above with the proper addresses.
u32 core2_ROM_START=0, core2_ROM_END=0;
u32 core2_TEXT_START=0, core2_TEXT_END=0;
u32 core2_DATA_START=0, core2_RODATA_END=0;
u32 core2_BSS_START=0, core2_BSS_END=0;
// (Similar zero definitions for other overlays if needed)