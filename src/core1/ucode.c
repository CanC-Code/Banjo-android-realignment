#include <ultra64.h>
#include <PR/rcp.h>
#include "core1/core1.h"
#include "functions.h"
#include "variables.h"

#define UCODE_SIZE 256

static u8 sUcodeData[UCODE_SIZE];
static s32 D_80283380;
static s32 D_80283384;
static s32 D_80283388;

void ucode_load(void) {
    // Stubbed: the original code reads physical RCP registers to detect
    // the RSP microcode version, but those registers don't exist in our
    // HLE environment and cause a SIGSEGV.  The ucode is only needed for
    // the original N64 RSP audio/graphics tasks; we skip it entirely.
    return;
}

void ucode_stub1(void) {}

void ucode_stub2(void) {
    osPiReadIo(0, NULL);
}

s32 ucode_stub3(void) {
    return 0;
}

void ucode_getPtrAndSize(void **ptr, u32 *size) {
    *ptr = &sUcodeData;
    *size = UCODE_SIZE;
}