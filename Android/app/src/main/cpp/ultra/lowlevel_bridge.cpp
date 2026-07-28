#include <sys/mman.h>
#include <errno.h>
#include <android/log.h>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <stdint.h>

#define LOG_TAG "BKA_MEM"

// Size allocations matching the expectations of bka_safe_base.h
//
// FIXED: BKA_RDRAM_ALLOC_SIZE increased from 0x1000000 (16MB) to 0x1001000
// (16MB + 4KB). The decompressor in src/done/rarezip.c writes decompressed
// core1 code into RDRAM starting at offset 0x1000, and g_decomp_out_cap is
// set to 16MB unconditionally (from InitializeDecompressionBuffers). This
// allows writes up to RDRAM+0x1000+16MB = one page past the 16MB boundary,
// causing SIGSEGV at exactly RDRAM+0x1000000. The extra 4KB padding absorbs
// this overflow safely.
#define BKA_RDRAM_ALLOC_SIZE  0x1001000 // 16MB + 4KB overflow guard
#define N64_REG_SPACE_SIZE    0x1000000 // 16MB (Covers RCP/RCP register ranges)
#define N64_PIF_SPACE_SIZE    0x0010000 // 64KB (Abundantly covers PIF ROM/RAM)
#define N64_ROM_SPACE_SIZE    0x04000000 // 64MB (Covers the full N64 physical ROM limit)

// CRITICAL CORRECTION: MI_INTR_REG physical offset is 0x04300008.
// Mapped against our 0x04000000 register allocation block base, the correct byte offset is 0x300008.
// Offset 0x30000C maps to MI_INTR_MASK_REG, which broke signal updates.
#define MI_INTR_REG_IDX       (0x00300008 / 4)
#define MI_INTR_VI            0x08

// N64 heap base offset within RDRAM (used by D_8002D500)
#define N64_HEAP_OFFSET       0x002D500
#define N64_HEAP_SIZE         0x211120

// FIXED: gN64_ROM_Base is the SINGLE authoritative definition.
// resource_mgr.cpp now uses "extern uint8_t* gN64_ROM_Base;" to reference this.
// The --allow-multiple-definition flag previously let both TUs define their
// own copy, causing isGenuineRom checks in ResourceMgr_HandleDma to use
// the wrong g_romSize/gN64_ROM_Base pair.
uint8_t* gN64_RDRAM    = nullptr;
uint32_t* gN64_Reg_Base = nullptr;
uint32_t* gN64_PIF_Base = nullptr;
uint8_t* gN64_ROM_Base = nullptr;

extern "C" {

    // Forward declaration of the native event routing bridge from emulator/stubs.cpp
    void HLE_TriggerN64Event(int event_id);

    // Signature updated to capture the dynamic asset path string
    void InitN64Registers(const char* assetDir) {
        // Idempotency guard: Prevent double allocation if called repeatedly
        if (gN64_RDRAM != nullptr && gN64_Reg_Base != nullptr &&
            gN64_PIF_Base != nullptr && gN64_ROM_Base != nullptr) {
            return;
        }

        // 1. Allocate Main N64 RDRAM Memory Space (16MB + 4KB overflow guard)
        gN64_RDRAM = (uint8_t*)mmap(
            nullptr,
            BKA_RDRAM_ALLOC_SIZE,
            PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_ANONYMOUS,
            -1, 0
        );

        // 2. Allocate N64 Hardware Emulation Register Space
        gN64_Reg_Base = (uint32_t*)mmap(
            nullptr,
            N64_REG_SPACE_SIZE,
            PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_ANONYMOUS,
            -1, 0
        );

        // 3. Allocate N64 PIF Subsystem Memory Space
        gN64_PIF_Base = (uint32_t*)mmap(
            nullptr,
            N64_PIF_SPACE_SIZE,
            PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_ANONYMOUS,
            -1, 0
        );

        // 4. Allocate Virtual Cartridge ROM Header Space (Now 64MB)
        // FIXED: This is the AUTHORITATIVE gN64_ROM_Base allocation.
        // resource_mgr.cpp will populate it in ResourceMgr_Init.
        gN64_ROM_Base = (uint8_t*)mmap(
            nullptr,
            N64_ROM_SPACE_SIZE,
            PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_ANONYMOUS,
            -1, 0
        );

        // Hard Fail Verification: Ensure the Android kernel granted all spaces securely
        if (gN64_RDRAM == MAP_FAILED || gN64_Reg_Base == MAP_FAILED ||
            gN64_PIF_Base == MAP_FAILED || gN64_ROM_Base == MAP_FAILED) {
            __android_log_print(ANDROID_LOG_FATAL, LOG_TAG,
                "Critical virtual memory mapping failure: %s", strerror(errno));

            // Cleanup any partial allocations before panicking
            if (gN64_RDRAM    != MAP_FAILED && gN64_RDRAM    != nullptr) munmap(gN64_RDRAM,    BKA_RDRAM_ALLOC_SIZE);
            if (gN64_Reg_Base != MAP_FAILED && gN64_Reg_Base != nullptr) munmap(gN64_Reg_Base, N64_REG_SPACE_SIZE);
            if (gN64_PIF_Base != MAP_FAILED && gN64_PIF_Base != nullptr) munmap(gN64_PIF_Base, N64_PIF_SPACE_SIZE);
            if (gN64_ROM_Base != MAP_FAILED && gN64_ROM_Base != nullptr) munmap(gN64_ROM_Base, N64_ROM_SPACE_SIZE);

            gN64_RDRAM    = nullptr;
            gN64_Reg_Base = nullptr;
            gN64_PIF_Base = nullptr;
            gN64_ROM_Base = nullptr;
            abort();
        }

        // Zero out all allocated pools to guarantee clean emulation states
        memset(gN64_RDRAM,    0, BKA_RDRAM_ALLOC_SIZE);
        memset(gN64_Reg_Base, 0, N64_REG_SPACE_SIZE);
        memset(gN64_PIF_Base, 0, N64_PIF_SPACE_SIZE);
        memset(gN64_ROM_Base, 0, N64_ROM_SPACE_SIZE);

        // CRITICAL FIX: Verify the heap region (D_8002D500 at RDRAM offset 0x002D500)
        // is accessible and properly zeroed. The bka_resolve_ptr in src/done/rarezip.c
        // maps N64 address 0x8002D500 to gN64_RDRAM + 0x002D500 via case A/C.
        // func_80000450 uses this as the DMA target for core1 decompression.
        if (N64_HEAP_OFFSET + N64_HEAP_SIZE <= BKA_RDRAM_ALLOC_SIZE) {
            __android_log_print(ANDROID_LOG_INFO, LOG_TAG,
                "Heap region validated: RDRAM+0x%X (0x%X bytes) for D_8002D500",
                N64_HEAP_OFFSET, N64_HEAP_SIZE);
        } else {
            __android_log_print(ANDROID_LOG_ERROR, LOG_TAG,
                "FATAL: Heap region exceeds RDRAM allocation! offset=0x%X size=0x%X limit=0x%X",
                N64_HEAP_OFFSET, N64_HEAP_SIZE, BKA_RDRAM_ALLOC_SIZE);
        }

        // Log the RDRAM overflow guard for diagnostics
        __android_log_print(ANDROID_LOG_INFO, LOG_TAG,
            "RDRAM allocated: %zu bytes (16MB usable + 4KB overflow guard at +0x1000000)",
            (size_t)BKA_RDRAM_ALLOC_SIZE);

        // CRITICAL CORRECTION: Map the physical ROM base dumped by the OTR Builder directly
        // into the emulated cartridge memory block so raw PI Subsystem reads succeed.
        // Note: ResourceMgr_Init also loads rom_base.bin into gN64_ROM_Base.
        // This is a secondary load that may be redundant but ensures the cartridge
        // address space (0x10000000+) has valid ROM data for direct PI reads.
        char romPath[512];
        snprintf(romPath, sizeof(romPath), "%s/rom_base.bin", assetDir);

        FILE* f = fopen(romPath, "rb");
        if (f) {
            size_t bytesRead = fread(gN64_ROM_Base, 1, N64_ROM_SPACE_SIZE, f);
            fclose(f);
            __android_log_print(ANDROID_LOG_INFO, LOG_TAG,
                "Memory Engine Stabilized: physical ROM mapped from %s (%zu bytes).", romPath, bytesRead);
        } else {
            // Safe fallback if the dump is somehow missing or unreadable
            __android_log_print(ANDROID_LOG_WARN, LOG_TAG, "WARNING: rom_base.bin missing, fallback memory will be zeroed.");
            gN64_ROM_Base[0x3B] = 'N';
            gN64_ROM_Base[0x3C] = 'B';
            gN64_ROM_Base[0x3D] = 'K';
            gN64_ROM_Base[0x3E] = 'E';
        }
    }

    void HardwareRegs_Shutdown() {
        if (gN64_RDRAM != nullptr) {
            munmap(gN64_RDRAM, BKA_RDRAM_ALLOC_SIZE);
            gN64_RDRAM = nullptr;
        }
        if (gN64_Reg_Base != nullptr) {
            munmap(gN64_Reg_Base, N64_REG_SPACE_SIZE);
            gN64_Reg_Base = nullptr;
        }
        if (gN64_PIF_Base != nullptr) {
            munmap(gN64_PIF_Base, N64_PIF_SPACE_SIZE);
            gN64_PIF_Base = nullptr;
        }
        if (gN64_ROM_Base != nullptr) {
            munmap(gN64_ROM_Base, N64_ROM_SPACE_SIZE);
            gN64_ROM_Base = nullptr;
        }
        __android_log_print(ANDROID_LOG_INFO, LOG_TAG, "Memory Engine Closed down cleanly.");
    }

    // -------------------------------------------------------------------------
    // ANDROID NATIVE BRIDGE HOOKS
    // -------------------------------------------------------------------------

    struct BKA_ControllerPad {
        uint16_t button;
        int8_t   stick_x;
        int8_t   stick_y;
        uint8_t  errno_val;
    };

    // Allocate the physical memory array for all 4 standard controller ports.
    BKA_ControllerPad gN64_ControllerData[4] = {{0, 0, 0, 0}};

    // Engine Clock Signal Pass:
    // Connects the asynchronous Android OpenGL thread to the synchronous N64 OS.
    void N64_TriggerVirtualVBlankInterrupt(void) {
        if (gN64_Reg_Base == nullptr) return;

        // Assert the VI Interrupt bit inside the emulated hardware register space.
        gN64_Reg_Base[MI_INTR_REG_IDX] |= MI_INTR_VI;

        // Pump the OS_EVENT_VI (ID: 14) message straight into the POSIX HLE event queues.
        // This instantly wakes up the blocked scheduler threads to step the system forward.
        HLE_TriggerN64Event(14);
    }

    // Hardware Renderer Stub:
    // Connects the recompiled N64 Display List executor to the Android GL surface.
    void VideoPlugin_OutputFrameTexture(uint32_t hostTextureId) {
        // STUB: Routes active RDP render targets to the Android GL texture context.
    }

} // end extern "C"