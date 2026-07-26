#include <PR/rcp.h>
#include <n64_types.h>

u32 osAiGetLength(void)
{
    return IO_READ(AI_LEN_REG);
}
