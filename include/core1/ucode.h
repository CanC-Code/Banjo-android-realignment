#ifdef __cplusplus
extern "C" {
#endif

#ifndef BANJO_KAZOOIE_CORE1_UCODE_H
#define BANJO_KAZOOIE_CORE1_UCODE_H
#ifdef __cplusplus
}
#endif
#include <n64_types.h>

#ifdef __cplusplus
extern "C" {
#endif
void ucode_load(void);
void ucode_stub1(void); // does nothing
void ucode_stub2(void); // performs dummy read on PI
s32 ucode_stub3(void); // returns always 0
void ucode_getPtrAndSize(void **ptr, u32 *size);

#endif


#ifdef __cplusplus
}
#endif
