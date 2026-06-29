#include <cstdint>
#include <cstdlib>

// This macro ensures these variables are exported globally so the recompiled
// N64 object code can find them at runtime via the dynamic linker.
#define RECOMP_SYMBOL __attribute__((used)) __attribute__((visibility("default")))

// Access the symbols defined in HardwareRegs.cpp
extern "C" {
    extern uint8_t* gN64_RDRAM;
    extern uint32_t* gN64_Reg_Base;
    extern uint32_t* gN64_PIF_Base;
    extern uint8_t* gN64_ROM_Base;
    
    void InitN64Registers(const char* assetDir);
}

// ============================================================
// SYMBOL EXPORTS (The recompiled code expects these specific pointers)
// ============================================================

// N64 registers are mapped to the dynamic gN64_Reg_Base pointer.
// We provide these as aliases so the recompiled N64 machine code 
// correctly resolves its RCP register references.

RECOMP_SYMBOL uint32_t* SP_DMEM              = nullptr; // Dynamic resolution needed
RECOMP_SYMBOL uint32_t* SP_STATUS_REG        = nullptr;

// ... (Continue adding your RECOMP_SYMBOL aliases here, using gN64_Reg_Base offsets)
// Example for DPC:
// RECOMP_SYMBOL uint32_t* DPC_START_REG = (gN64_Reg_Base + 0x00040000 / 4);

// Wrapper for the engine to call
extern "C" RECOMP_SYMBOL void BKA_Register_Init_Bridge(const char* assetDir) {
    InitN64Registers(assetDir);
}
