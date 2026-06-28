#include "HardwareRegs.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <android/log.h>

#define LOG_TAG "HWRegs"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// ── Replicate struct huft from inflate.c so we can size the pool ──────────────
// Must match the layout in the compiled recompilation exactly.
struct huft {
    uint8_t  e;       // extra bits or op
    uint8_t  b;       // bits in code
    uint16_t _pad;    // alignment padding
    union {
        uint16_t  n;  // literal / length / distance base
        struct huft* t; // next table pointer
    } v;
};

// ── Inflate globals that exist in libbkawrapper.so ───────────────────────────
// Declared extern so the linker resolves them to the .so's BSS symbols.
// We assign them here before the engine starts.
extern "C" {
    extern uint8_t*  inbuf;
    extern uint8_t*  D_80007284;
    extern huft*     D_80007290;
    extern uint32_t  inptr;
}

// ── Public globals referenced by NativeBridge and stubs ──────────────────────
#define N64_RDRAM_SIZE   (8u * 1024u * 1024u)
#define HUFT_POOL_COUNT  4096
#define INFLATE_WSIZE    (32u * 1024u)

uint8_t*  gN64_RDRAM    = nullptr;
uint32_t* gN64_Reg_Base = nullptr;

static uint32_t s_regFile[0x500 / 4];
static huft     s_huftPool[HUFT_POOL_COUNT];
static uint8_t  s_inflateWindow[INFLATE_WSIZE];

// ── InitN64Registers ──────────────────────────────────────────────────────────
extern "C" void InitN64Registers(const char* assetDir) {
    if (gN64_RDRAM == nullptr) {
        gN64_RDRAM = static_cast<uint8_t*>(calloc(N64_RDRAM_SIZE, 1));
        if (!gN64_RDRAM) {
            LOGE("FATAL: calloc(%u) for RDRAM failed", N64_RDRAM_SIZE);
            return;
        }
        LOGI("gN64_RDRAM allocated: %p (%u bytes)", gN64_RDRAM, N64_RDRAM_SIZE);
    }

    gN64_Reg_Base = s_regFile;
    memset(s_regFile, 0, sizeof(s_regFile));

    // Wire inflate.c globals to valid memory.
    // These were raw RDRAM pointers on N64 hardware; here we back them
    // with static allocations to avoid collision with game data.
    inbuf      = gN64_RDRAM;
    D_80007284 = s_inflateWindow;
    D_80007290 = s_huftPool;
    inptr      = 0;

    memset(s_inflateWindow, 0, sizeof(s_inflateWindow));
    memset(s_huftPool,      0, sizeof(s_huftPool));

    LOGI("inflate wired: inbuf=%p D_80007284=%p D_80007290=%p",
         inbuf, (void*)D_80007284, (void*)D_80007290);
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

// ── Register I/O stubs ───────────────────────────────────────────────────────
extern "C" u32 ReadHardwareRegister(u32 addr) {
    uint32_t offset = (addr & 0x00FFFFFFu) / 4;
    if (offset < (sizeof(s_regFile) / sizeof(s_regFile[0])))
        return s_regFile[offset];
    return 0;
}

extern "C" void WriteHardwareRegister(u32 addr, u32 value) {
    uint32_t offset = (addr & 0x00FFFFFFu) / 4;
    if (offset < (sizeof(s_regFile) / sizeof(s_regFile[0])))
        s_regFile[offset] = value;
}

extern "C" void InitHardwareRegs(void) {
    InitN64Registers(nullptr);
}