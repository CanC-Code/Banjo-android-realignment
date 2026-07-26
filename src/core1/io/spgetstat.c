#include <PR/os_internal.h>
#include <PR/rcp.h>
#include <n64_types.h>

u32 __osSpGetStatus()
{
    return IO_READ(SP_STATUS_REG);
}
