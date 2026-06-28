#pragma once
/*
 * bka_safe_base.h  –  BKA Android N64 address translation layer
 *
 * This header provides atomic pointer translation for the recompiled N64 environment.
 * It is included by both legacy C (decompiled) and modern C++ (Android bridge) code.
 */

#include <android/log.h>
#include <stdint.h>
#include <stdlib.h>

#define BKA_RDRAM_ALLOC_SIZE  (0x1000000u)   /* 16 MB */
#define BKA_ROM_ALLOC_SIZE    (0x4000000u)   /* 64 MB */

#ifdef __cplusplus
extern "C" {
#endif

/* Globals initialized by InitN64Registers() */
extern uint8_t* gN64_RDRAM;
extern uint32_t* gN64_Reg_Base;
extern uint32_t* gN64_PIF_Base;
extern uint8_t* gN64_ROM_Base;

extern void InitN64Registers(const char* assetDir);

#ifdef __cplusplus
}
#endif

/* ── Address translation ──────────────────────────────────────────────── */

static inline uintptr_t BKA_Validate_And_Translate(
        uintptr_t addr, const char* file, int line)
{
    uint32_t mask32 = (uint32_t)(addr & 0xFFFFFFFFu);
    if (mask32 == 0u) return 0u;

    /* Acquire-load ensures we see the pointers set by InitN64Registers() */
    uint8_t* ram_ptr  = __atomic_load_n(&gN64_RDRAM,    __ATOMIC_ACQUIRE);
    uint32_t* reg_ptr  = __atomic_load_n(&gN64_Reg_Base, __ATOMIC_ACQUIRE);
    uint32_t* pif_ptr  = __atomic_load_n(&gN64_PIF_Base, __ATOMIC_ACQUIRE);
    uint8_t* rom_ptr  = __atomic_load_n(&gN64_ROM_Base, __ATOMIC_ACQUIRE);

    /* Defensive: If RAM isn't ready, we cannot safely translate. */
    if (!ram_ptr) {
        __android_log_print(ANDROID_LOG_FATAL, "BKA_MEM", "[%s:%d] RDRAM NOT INIT! addr=0x%08x", file, line, mask32);
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

    /* Fallback: If we reach here, it's an unmapped access. 
       We return a masked pointer into RDRAM instead of 0 to prevent SIGSEGV. 
       This will log a warning, allowing the engine to continue with "zeroed" data. */
    __android_log_print(ANDROID_LOG_WARN, "BKA_MEM", "[%s:%d] UNMAPPED ACCESS: 0x%08x. Redirecting to RDRAM.", file, line, mask32);
    
    return ram + (mask32 & 0x00FFFFFFu); 
}

#define BKA_TRANSLATE_ADDR(addr) BKA_Validate_And_Translate((uintptr_t)(addr), __FILE__, __LINE__)
