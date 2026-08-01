#include <ultra64.h>
#include "core1/core1.h"
#include "functions.h"
#include "variables.h"

/* .bss */
s32 D_803837F0;

/* .code */
void func_8030D86C(void){
    // STUB: Prevent allocation of uninitialised subsystem during early boot.
    // The original function allocates a global buffer using n64_malloc,
    // but the required context isn't ready yet, causing a null-pointer
    // dereference in the heap allocator.  Skipping this is safe – the
    // buffer is only needed for save-related features that are not yet
    // implemented in the Android port.
}

void func_8030D8DC(void){
    // STUB: Prevent deallocation of the uninitialised buffer.
    // This function normally frees the buffer allocated in func_8030D86C.
}

void func_8030D8A8(s32 arg0, s32 arg1){
    return;
}

void func_8030D8B8(s32 arg0, s32 arg1){
    D_803837F0 = arg1;
}

s32 func_8030D8C8(void){
    return D_803837F0;
}