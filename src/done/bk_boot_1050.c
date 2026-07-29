#include <ultra64.h>
#include "rarezip.h"
#include <android/log.h>

#define LOG_TAG "BKA_BOOT"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

#define ENTRY_STACK_LEN 0x2000
#define ENTRY_STACK_LEN_U64 (ENTRY_STACK_LEN / sizeof(u64))

u64 gEntryStack[ENTRY_STACK_LEN_U64];

#define CORE1_RZIP_ROM_START  0x00001050
#define CORE1_RZIP_ROM_END    0x000E0000
#define CORE1_RZIP_SIZE       (CORE1_RZIP_ROM_END - CORE1_RZIP_ROM_START)
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

    osPiRawStartDma(OS_READ, (u32)CORE1_RZIP_ROM_START, tmp, CORE1_RZIP_SIZE);
    LOGI("func_80000450: DMA done, starting decompress");

    func_8000055C();
    func_80000594(&tmp, &dst);
    D_803FFE00[0] = crc1;
    D_803FFE00[1] = crc2;
    LOGI("func_80000450: first decompress done");

    func_80000594(&tmp, &dst);
    D_803FFE00[2] = crc1;
    D_803FFE00[3] = crc2;
    LOGI("func_80000450: second decompress done");

    overlay_table_init();
    LOGI("func_80000450: overlay table done, jumping to core1_main");

    (&func_8023DA20)(arg0);
    LOGI("func_80000450: returned from core1_main (should never happen)");
}