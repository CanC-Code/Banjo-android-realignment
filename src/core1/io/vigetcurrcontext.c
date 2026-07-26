#include <PR/os_internal.h>
#include "viint.h"
#include <n64_types.h>

__OSViContext *__osViGetCurrentContext(void)
{
    return __osViCurr;
}
