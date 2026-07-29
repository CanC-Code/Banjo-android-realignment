#include <ultra64.h>
#include "rarezip.h"
#include <android/log.h>

#define LOG_TAG "BKA_BOOT"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

#define ENTRY_STACK_LEN 0x2000
#define ENTRY_STACK_LEN_U64 (ENTRY_STACK_LEN / sizeof(u64))

u64 gEntryStack[ENTRY_STACK_LEN_U64];

// FIXED: On N64, core1_rzip_ROM_START/END are symbols placed at ROM addresses
// 0x00001050 and 0x000E0000 by the linker script. On Android (LinkerSymbols.cpp),
// they are uintptr_t variables CONTAINING these values. Using the array name
// gives the host address of the variable, not the ROM offset value.
// We use the known N64 ROM offsets directly.
#define CORE1_RZIP_ROM_START  0x00001050
#define CORE1_RZIP_ROM_END    0x000E0000
#define CORE1_RZIP_SIZE       (CORE1_RZIP_ROM_END - CORE1_RZIP_ROM_START)

// FIXED: On N64, core1_VRAM is a symbol placed at N64 address 0x80001000 by the
// linker script, so &core1_VRAM returns 0x80001000. On Android (LinkerSymbols.cpp),
// core1_VRAM is a uintptr_t variable whose VALUE is 0x80001000. Taking its address
// gives the host address of that variable, not 0x80001000. We use the N64 address
// directly. bka_resolve_ptr in rarezip.c will map this to gN64_RDRAM + 0x1000.
#define CORE1_VRAM_N64_ADDR   0x80001000

extern u8 D_8002D500;
extern u32 D_803FFE00[4];

void func_80000594(u8 **, u8 **);
void func_8023DA20(s32);

void func_80000450(s32 arg0){
    u8 *tmp;
    u8 *dst;

    LOGI("func_80000450: entry");
    tmp = &D_8002D500;
    dst = (u8*)(uintptr_t)CORE1_VRAM_N64_ADDR;

    osInitialize();
    LOGI("func_80000450: osInitialize done, starting DMA");

    // DMA: copy compressed core1 code from ROM offset 0x1050 into the D_8002D500 heap buffer.
    // ResourceMgr_HandleDma reads from rom_base.bin at the given offset.
    osPiRawStartDma(OS_READ, (u32)CORE1_RZIP_ROM_START, tmp, CORE1_RZIP_SIZE);

    // FIXED: The DMA completes synchronously in our HLE implementation
    // (osPiRawStartDma calls ResourceMgr_HandleDma which reads the file
    // and copies data before returning). The original N64 poll loop is
    // unnecessary and can spin forever if osPiGetStatus() returns garbage.
    // while(osPiGetStatus() & PI_STATUS_DMA_BUSY);

    LOGI("func_80000450: DMA done, starting decompress");
    func_8000055C();

    // First decompression pass: decompress .text section from heap buffer into RDRAM.
    func_80000594(&tmp, &dst);
    D_803FFE00[0] = crc1;
    D_803FFE00[1] = crc2;
    LOGI("func_80000450: first decompress done");

    // Second decompression pass: decompress .data section from heap buffer into RDRAM.
    func_80000594(&tmp, &dst);
    D_803FFE00[2] = crc1;
    D_803FFE00[3] = crc2;
    LOGI("func_80000450: second decompress done");

    overlay_table_init();
    LOGI("func_80000450: overlay table done, jumping to core1_main");

    (&func_8023DA20)(arg0);

    // We should never return from func_8023DA20; if we do, log it.
    LOGI("func_80000450: returned from core1_main (unexpected!)");
}