#ifdef __cplusplus
extern "C" {
#endif

#ifndef _SIINT_H
#define _SIINT_H
#ifdef __cplusplus
}
#endif
#include <n64_types.h>
#include <PR/os_internal.h>
#include <PR/rcp.h>

#ifdef __cplusplus
extern "C" {
#endif
void __osSiGetAccess(void);
void __osSiRelAccess(void);
int __osSiDeviceBusy(void);
void __osSiCreateAccessQueue(void);
#endif


#ifdef __cplusplus
}
#endif
