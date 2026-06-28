#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <android/log.h>

#include "HardwareRegs.h"

// Pull in the inflate.c globals that need backing memory
extern "C" {
    #include "rarezip.h"  // for struct huft

    extern uint8_t*     inbuf;
    extern uint8_t*     D_80007284;
    extern struct huft* D_80007290;
    extern uint32_t     inptr;
}

#define LOG_TAG "HWRegs"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// ── Public globals referenced by NativeBridge and stubs ──────────────────────
// N64 RDRAM: 8MB expansion pak
#define N64_RDRAM_SIZE  (8u * 1024u * 1024u)

// Huft pool: inflate.c uses this as a flat allocator.
// hufts tracks usage; N64 boot inflate never needs more than ~2KB worth.
#define HUFT_POOL_COUNT 4096

// Sliding window for inflate output: deflate uses a 32KB window.
#define INFLATE_WSIZE   (32u * 1024u)

uint8_t*  gN64_RDRAM    = nullptr;
uint32_t* gN64_Reg_Base = nullptr;

static uint32_t  s_regFile[0x500 / 4];  // covers MIPS interface / VI / AI / PI ranges
static huft      s_huftPool[HUFT_POOL_COUNT];
static uint8_t   s_inflateWindow[INFLATE_WSIZE];

// ── InitN64Registers ─────────────────────────────────────────────────────────
// Called from NativeBridge.nativeGameBoot BEFORE the game thread is created
// and BEFORE ResourceMgr_Init, so that by the time BKA_StartEngine runs
// the inflate globals are already valid.
extern "C" void InitN64Registers(const char* assetDir) {
    // 1. Allocate RDRAM
    if (gN64_RDRAM == nullptr) {
        gN64_RDRAM = static_cast<uint8_t*>(calloc(N64_RDRAM_SIZE, 1));
        if (!gN64_RDRAM) {
            LOGE("FATAL: Failed to allocate %u bytes for N64 RDRAM", N64_RDRAM_SIZE);
            return;
        }
        LOGI("gN64_RDRAM allocated: %p (%u bytes)", gN64_RDRAM, N64_RDRAM_SIZE);
    }

    // 2. Point hardware register base at our stub file
    gN64_Reg_Base = s_regFile;
    memset(s_regFile, 0, sizeof(s_regFile));

    // 3. Wire up inflate.c globals.
    //
    //    On real N64 hardware these were raw RDRAM addresses:
    //      D_80007284 = 0x80007284  →  RDRAM offset 0x7284  (inflate output window)
    //      D_80007290 = 0x80007290  →  RDRAM offset 0x7290  (huft pool)
    //
    //    We use static allocations instead so that:
    //      a) The 32KB window doesn't overlap game data in RDRAM.
    //      b) The huft pool is a known safe size.
    //
    inbuf      = gN64_RDRAM;          // compressed ROM data starts at RDRAM base
    D_80007284 = s_inflateWindow;     // inflate writes decompressed output here
    D_80007290 = s_huftPool;          // huft_build allocates entries from here
    inptr      = 0;

    memset(s_inflateWindow, 0, sizeof(s_inflateWindow));
    memset(s_huftPool,      0, sizeof(s_huftPool));

    LOGI("inflate globals wired: inbuf=%p D_80007284=%p D_80007290=%p",
         inbuf, D_80007284, (void*)D_80007290);
}

// ── HardwareRegs_Shutdown ─────────────────────────────────────────────────────
extern "C" void HardwareRegs_Shutdown(void) {
    if (gN64_RDRAM) {
        free(gN64_RDRAM);
        gN64_RDRAM = nullptr;
    }
    gN64_Reg_Base = nullptr;
    inbuf         = nullptr;
    D_80007284    = nullptr;
    D_80007290    = nullptr;
    LOGI("HardwareRegs_Shutdown complete.");
}

// ── Stub register I/O ─────────────────────────────────────────────────────────
extern "C" u32 ReadHardwareRegister(u32 addr) {
    uint32_t offset = (addr & 0x00FFFFFF) / 4;
    if (offset < (sizeof(s_regFile) / sizeof(s_regFile[0]))) {
        return s_regFile[offset];
    }
    return 0;
}

extern "C" void WriteHardwareRegister(u32 addr, u32 value) {
    uint32_t offset = (addr & 0x00FFFFFF) / 4;
    if (offset < (sizeof(s_regFile) / sizeof(s_regFile[0]))) {
        s_regFile[offset] = value;
    }
}

extern "C" void InitHardwareRegs(void) {
    // Thin alias kept for header compatibility
    InitN64Registers(nullptr);
}