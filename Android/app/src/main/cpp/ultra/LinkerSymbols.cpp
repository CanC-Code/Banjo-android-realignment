#include <cstdint>
#include <cstdlib>

#define RECOMP_SYMBOL __attribute__((used)) __attribute__((visibility("default")))

extern "C" {
    // ------------------------------------------------------------
    // 1. FORWARD DECLARATIONS / BRIDGES
    // ------------------------------------------------------------
    void InitN64Registers(const char* assetDir);

    // Bridge for the name mismatch (The recompiled code calls InitHardwareRegs)
    RECOMP_SYMBOL void InitHardwareRegs() {
        InitN64Registers(nullptr);
    }

    // ------------------------------------------------------------
    // 2. MATH & ENGINE GLOBALS
    // ------------------------------------------------------------
    RECOMP_SYMBOL uint32_t __libm_qnan_f = 0x7FC00000;

    RECOMP_SYMBOL uintptr_t core1_VRAM           = 0x80001000;
    RECOMP_SYMBOL uintptr_t core1_rzip_ROM_START = 0x00001050;
    RECOMP_SYMBOL uintptr_t core1_rzip_ROM_END   = 0x000E0000;

    RECOMP_SYMBOL uintptr_t core2_rzip_ROM_START = 0x000F0000;
    RECOMP_SYMBOL uintptr_t core2_rzip_ROM_END   = 0x001F0000;

    // ------------------------------------------------------------
    // 3. ROM MARKERS & OVERLAYS
    // ------------------------------------------------------------
    RECOMP_SYMBOL uintptr_t gOverlayTable = 0x01200000;

    RECOMP_SYMBOL uintptr_t SM_rzip_ROM_START    = 0x00400000;
    RECOMP_SYMBOL uintptr_t SM_rzip_ROM_END      = 0x00410000;
    RECOMP_SYMBOL uintptr_t MM_rzip_ROM_START    = 0x00500000;
    RECOMP_SYMBOL uintptr_t MM_rzip_ROM_END      = 0x00510000;
    RECOMP_SYMBOL uintptr_t TTC_rzip_ROM_START   = 0x00600000;
    RECOMP_SYMBOL uintptr_t TTC_rzip_ROM_END     = 0x00610000;
    RECOMP_SYMBOL uintptr_t CC_rzip_ROM_START    = 0x00700000;
    RECOMP_SYMBOL uintptr_t CC_rzip_ROM_END      = 0x00710000;
    RECOMP_SYMBOL uintptr_t MMM_rzip_ROM_START   = 0x00B00000;
    RECOMP_SYMBOL uintptr_t MMM_rzip_ROM_END     = 0x00B10000;
    RECOMP_SYMBOL uintptr_t GV_rzip_ROM_START    = 0x00A00000;
    RECOMP_SYMBOL uintptr_t GV_rzip_ROM_END      = 0x00A10000;
    RECOMP_SYMBOL uintptr_t emptyLvl_rzip_ROM_START  = 0x01100000;
    RECOMP_SYMBOL uintptr_t emptyLvl_rzip_ROM_END    = 0x01110000;

    // Add other missing ROM markers as needed by the linker errors
    RECOMP_SYMBOL uintptr_t BGS_rzip_ROM_START   = 0x00800000;
    RECOMP_SYMBOL uintptr_t BGS_rzip_ROM_END     = 0x00810000;
}
