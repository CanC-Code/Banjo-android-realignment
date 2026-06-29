#include <cstdint>
#include <cstdlib>

// ============================================================
// 1. MACRO SHIELDING
// If the compiler injects macros via -include, we must undef them
// immediately before using them.
// ============================================================
#undef SP_STATUS_REG
#undef DPC_START_REG
#undef DPC_END_REG
#undef DPC_CURRENT_REG
#undef DPC_STATUS_REG
#undef DPC_CLOCK_REG
#undef DPC_BUFBUSY_REG
#undef DPC_PIPEBUSY_REG
#undef DPC_TMEM_REG
#undef MI_INIT_MODE_REG
#undef MI_VERSION_REG
#undef MI_INTR_REG
#undef MI_INTR_MASK_REG
#undef VI_STATUS_REG
// ... (Add any other registers causing errors here)

#define RECOMP_SYMBOL __attribute__((used)) __attribute__((visibility("default")))

// Access the dynamic pointers from HardwareRegs.cpp
extern "C" {
    extern uint32_t* gN64_Reg_Base;
    
    // The engine expects these pointers to be defined as global symbols
    RECOMP_SYMBOL uint32_t* SP_STATUS_REG        = nullptr;
    RECOMP_SYMBOL uint32_t* DPC_START_REG        = nullptr;
    RECOMP_SYMBOL uint32_t* DPC_END_REG          = nullptr;
    RECOMP_SYMBOL uint32_t* MI_INTR_MASK_REG     = nullptr;
    RECOMP_SYMBOL uint32_t* VI_STATUS_REG        = nullptr;

    // Bridge initialization
    void BKA_Register_Init_Bridge(const char* assetDir);
}

// Initialize the pointers by mapping them to the Reg_Base offset
void BKA_Register_Init_Bridge(const char* assetDir) {
    if (!gN64_Reg_Base) return;

    // RCP Register Offsets (e.g., SP is 0x04040000, mapped in Reg_Base)
    SP_STATUS_REG        = gN64_Reg_Base + (0x00040000 / 4);
    DPC_START_REG        = gN64_Reg_Base + (0x00100000 / 4);
    DPC_END_REG          = gN64_Reg_Base + (0x00100004 / 4);
    MI_INTR_MASK_REG     = gN64_Reg_Base + (0x0030000C / 4);
    VI_STATUS_REG        = gN64_Reg_Base + (0x00400000 / 4);
}
