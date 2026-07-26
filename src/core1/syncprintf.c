/* Redirected */ #include <n64_stdarg.h>
#include <PR/rmon.h>
#include <PR/os.h>
#include <n64_types.h>

void osSyncPrintf(const char *fmt, ...)
{
    int ans;
    va_list ap;
    // these functions intentionally left blank.  ifdeffed out in rom release
}
void rmonPrintf(const char *fmt, ...)
{
    int ans;
    va_list ap;
}
