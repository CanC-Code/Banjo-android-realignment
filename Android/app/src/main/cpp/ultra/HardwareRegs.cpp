#include "HardwareRegs.h"
#include "bka_safe_base.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <android/log.h>

#define LOG_TAG "HWRegs"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

struct huft {
    uint8_t  e;
    uint8_t  b;
    uint16_t _pad;
    union {
        uint16_t  n;
        struct huft* t;
    } v;
};

extern "C" {
    extern uint8_t* inbuf;
    extern uint8_t* D_80007284;
    extern huft* D_80007290;
    extern uint32_t  inptr;
}

#define HUFT_POOL_COUNT  4096

// Enforce C linkage for compatibility with legacy C modules
extern "C" {
    uint8_t* gN64_RDRAM    = nullptr;
    uint32_t* gN64_Reg_Base = nullptr;
    uint32_t* gN64_PIF_Base = nullptr;
    uint8_t* gN64_ROM_Base = nullptr;
}

static uint32_t s_regFile[0x500 / 4];
static huft     s_huftPool[HUFT_POOL_COUNT];

extern "C" void InitN64Registers(const char* assetDir) {
    // 1. Allocate RDRAM (Main memory)
    if (gN64_RDRAM == nullptr) {
        gN64_RDRAM = static_cast<uint8_t*>(calloc(BKA_RDRAM_ALLOC_SIZE, 1));
        if (!gN64_RDRAM) {
            LOGE("FATAL: calloc(%u) for RDRAM failed", BKA_RDRAM_ALLOC_SIZE);
            abort();
        }
        LOGI("gN64_RDRAM allocated: %p", gN64_RDRAM);
    }

    // 2. Allocate ROM Base (Dummy buffer to prevent crash on ROM access)
    if (gN64_ROM_Base == nullptr) {
        gN64_ROM_Base = static_cast<uint8_t*>(calloc(BKA_ROM_ALLOC_SIZE, 1));
        if (!gN64_ROM_Base) {
            LOGE("FATAL: calloc(%u) for ROM failed", BKA_ROM_ALLOC_SIZE);
            abort();
        }
    }

    // 3. Allocate PIF Base (Dummy buffer for Peripheral Interface)
    if (gN64_PIF_Base == nullptr) {
        gN64_PIF_Base = static_cast<uint32_t*>(calloc(0x1000, 1));
        if (!gN64_PIF_Base) {
            LOGE("FATAL: calloc(PIF) failed");
            abort();
        }
    }

    gN64_Reg_Base = s_regFile;
    memset(s_regFile, 0, sizeof(s_regFile));

    // Wiring
    inbuf      = gN64_RDRAM;
    D_80007284 = gN64_RDRAM; 
    D_80007290 = s_huftPool;
    inptr      = 0;

    memset(s_huftPool, 0, sizeof(s_huftPool));

    LOGI("Hardware regs initialized and wired.");
}

extern "C" void HardwareRegs_Shutdown(void) {
    if (gN64_RDRAM) { free(gN64_RDRAM); gN64_RDRAM = nullptr; }
    if (gN64_ROM_Base) { free(gN64_ROM_Base); gN64_ROM_Base = nullptr; }
    if (gN64_PIF_Base) { free(gN64_PIF_Base); gN64_PIF_Base = nullptr; }
    
    gN64_Reg_Base = nullptr;
    inbuf         = nullptr;
    D_80007290    = nullptr;
    LOGI("HardwareRegs_Shutdown complete.");
}

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
