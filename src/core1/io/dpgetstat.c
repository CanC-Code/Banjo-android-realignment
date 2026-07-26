#include <PR/os_internal.h>
#include <PR/rcp.h>
#include <n64_types.h>

u32 osDpGetStatus(){
    return IO_READ(DPC_STATUS_REG);
}
