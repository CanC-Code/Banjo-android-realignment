#include <ultra64.h>
#include "bka_safe_base.h"
#include "functions.h"
#include "variables.h"

void levelSpecificFlags_set(s32 index, s32 val);

//levelSpecificFlags
/* .bss */
struct{
    u32 unk0;
    u32 unk4;
    u8  unk8[8];
}D_80383320;

/* .code */

// STUB: CRC calculation uses N64-specific address scrambling that
// dereferences invalid pointers on Android. We skip the CRC entirely
// since the validation stubs always return success.
u32 _levelSpecificFlags_calcCRC1(void) {
    return 0;
}

void _levelSpecificFlags_updateCRC1(void) {
}

s32 _levelSpecificFlags_calcCRC2(void) {
    return 0;
}

void _levelSpecificFlags_updateCRC2(void) {
}

s32 levelSpecificFlags_get(s32 i){
    return bitfieldarray_getBit(D_80383320.unk8, i);
}

s32 levelSpecificFlags_getN(s32 i, s32 n){
    return bitfieldarray_getNBits(D_80383320.unk8, i, n);
}

s32 levelSpecificFlags_getSet(s32 arg0, s32 arg1){
    s32 retVal = levelSpecificFlags_get(arg0);
    levelSpecificFlags_set(arg0, arg1);
    return retVal;
}

void levelSpecificFlags_clear(void){
    s32 i;
    for(i = 0; i < 8; i++){
        D_80383320.unk8[i] = 0;
    }
    _levelSpecificFlags_updateCRC1();
    _levelSpecificFlags_updateCRC2();
}

void levelSpecificFlags_set(s32 index, s32 val){
    bitfieldarray_setBit(&D_80383320.unk8, index, val);
    _levelSpecificFlags_updateCRC1();
    _levelSpecificFlags_updateCRC2();
}

void levelSpecificFlags_setN(s32 index, s32 val, s32 n){
    bitfieldarray_setNBits(&D_80383320.unk8, index, val, n);
    _levelSpecificFlags_updateCRC1();
    _levelSpecificFlags_updateCRC2();
}

// Stubbed: CRC1 is invalid after recompilation, always pass.
s32 levelSpecificFlags_validateCRC1(void) {
    return 1;
}

// Stubbed: CRC2 is invalid after recompilation, always pass.
s32 levelSpecificFlags_validateCRC2(void){
    return 1;
}