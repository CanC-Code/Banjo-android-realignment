#include "HardwareRegs.h"
#include "bka_safe_base.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <android/log.h>

#define LOG_TAG "BKA_HardwareRegs"

// Define the global pointers as exported symbols
extern "C" {
    uint8_t* gN64_RDRAM    = nullptr;
    uint32_t* gN64_Reg_Base = nullptr;
    uint32_t* gN64_PIF_Base = nullptr;
    uint8_t* gN64_ROM_Base = nullptr;
}

static uint32_t s_regFile[0x500 / 4];

extern "C" void InitN64Registers(const char* assetDir) {
    // Prevent double-initialization
    if (gN64_RDRAM != nullptr) return;

    __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "Initializing BKA Memory System...");

    // 1. Allocate RDRAM
    gN64_RDRAM = static_cast<uint8_t*>(calloc(BKA_RDRAM_ALLOC_SIZE, 1));
    if (!gN64_RDRAM) abort();

    // 2. Allocate ROM Base
    gN64_ROM_Base = static_cast<uint8_t*>(calloc(BKA_ROM_ALLOC_SIZE, 1));
    if (!gN64_ROM_Base) abort();

    // 3. Allocate PIF Base
    gN64_PIF_Base = static_cast<uint32_t*>(calloc(0x1000, 1));
    if (!gN64_PIF_Base) abort();

    // 4. Initialize Registers
    gN64_Reg_Base = s_regFile;
    memset(s_regFile, 0, sizeof(s_regFile));

    __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "Memory System initialized: RDRAM=%p, ROM=%p", gN64_RDRAM, gN64_ROM_Base);
}

extern "C" void HardwareRegs_Shutdown(void) {
    if (gN64_RDRAM) { free(gN64_RDRAM); gN64_RDRAM = nullptr; }
    if (gN64_ROM_Base) { free(gN64_ROM_Base); gN64_ROM_Base = nullptr; }
    if (gN64_PIF_Base) { free(gN64_PIF_Base); gN64_PIF_Base = nullptr; }
    gN64_Reg_Base = nullptr;
}
