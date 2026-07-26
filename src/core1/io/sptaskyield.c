#include <PR/os_internal.h>
#include <PR/rcp.h>
#include <n64_types.h>

void osSpTaskYield(void)
{
    __osSpSetStatus(SP_SET_YIELD);
}
