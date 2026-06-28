#pragma once

#include <android/log.h>
#include <stdint.h>
#include <stdlib.h>

/* Allocation Sizes */
#define BKA_RDRAM_ALLOC_SIZE  (0x1000000u)   /* 16 MB */
#define BKA_ROM_ALLOC_SIZE    (0x4000000u)   /* 64 MB */

#ifdef __cplusplus
extern "C" {
#endif

extern uint8_t* gN64_RDRAM;
extern uint32_t* gN64_Reg_Base;
extern uint32_t* gN64_PIF_Base;
extern uint8_t* gN64_ROM_Base;

extern void InitN64Registers(const char* assetDir);

#ifdef __cplusplus
}
#endif

/* ── Atomic Address translation ──────────────────────────────────────── */

static inline uintptr_t BKA_Validate_And_Translate(
        uintptr_t addr, const char* file, int line)
{
    uint32_t mask32 = (uint32_t)(addr & 0xFFFFFFFFu);
    if (mask32 == 0u) return 0u;

    uint8_t* ram_ptr  = __atomic_load_n(&gN64_RDRAM,    __ATOMIC_ACQUIRE);
    uint32_t* reg_ptr  = __atomic_load_n(&gN64_Reg_Base, __ATOMIC_ACQUIRE);
    uint32_t* pif_ptr  = __atomic_load_n(&gN64_PIF_Base, __ATOMIC_ACQUIRE);
    uint8_t* rom_ptr  = __atomic_load_n(&gN64_ROM_Base, __ATOMIC_ACQUIRE);

    if (!ram_ptr) {
        return 0u; 
    }

    uintptr_t ram = (uintptr_t)ram_ptr;
    
    /* 1. RDRAM Translation */
    if (mask32 < BKA_RDRAM_ALLOC_SIZE)             return ram + mask32;
    if (mask32 >= 0x80000000u && mask32 < 0x81000000u) return ram + (mask32 - 0x80000000u);
    if (mask32 >= 0xA0000000u && mask32 < 0xA1000000u) return ram + (mask32 - 0xA0000000u);

    /* 2. Registers Translation */
    if (reg_ptr) {
        uintptr_t reg = (uintptr_t)reg_ptr;
        if (mask32 >= 0x04000000u && mask32 < 0x05000000u) return reg + (mask32 - 0x04000000u);
        if (mask32 >= 0xA4000000u && mask32 < 0xA5000000u) return reg + (mask32 - 0xA4000000u);
    }

    /* 3. PIF Translation */
    if (pif_ptr) {
        uintptr_t pif = (uintptr_t)pif_ptr;
        if (mask32 >= 0x1FC00000u && mask32 < 0x1FC01000u) return pif + (mask32 - 0x1FC00000u);
        if (mask32 >= 0xBFC00000u && mask32 < 0xBFC01000u) return pif + (mask32 - 0xBFC00000u);
    }

    /* 4. ROM Translation */
    if (rom_ptr) {
        uintptr_t rom = (uintptr_t)rom_ptr;
        if (mask32 >= 0x10000000u && mask32 < 0x14000000u) return rom + (mask32 - 0x10000000u);
        if (mask32 >= 0xB0000000u && mask32 < 0xB4000000u) return rom + (mask32 - 0xB0000000u);
    }

    /* Soft-fallback to RDRAM to avoid SIGSEGV */
    return ram + (mask32 & 0x00FFFFFFu); 
}

#define BKA_TRANSLATE_ADDR(addr) BKA_Validate_And_Translate((uintptr_t)(addr), __FILE__, __LINE__)
