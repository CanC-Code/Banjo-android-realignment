#include <PR/os_internal.h>
#include <PR/rcp.h>
#include "piint.h"
#include <n64_types.h>

s32 osEPiRawReadIo(OSPiHandle *pihandle, u32 devAddr, u32 *data)
{
    register u32 stat;
    register u32 domain;

    WAIT_ON_IOBUSY(stat);
    
    *data = IO_READ(pihandle->baseAddress | devAddr);
    return 0;
}
