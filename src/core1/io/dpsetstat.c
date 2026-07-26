#include <PR/os_internal.h>
#include <PR/rcp.h>
#include <n64_types.h>

void osDpSetStatus(u32 data)
{
    IO_WRITE(DPC_STATUS_REG, data);
}
