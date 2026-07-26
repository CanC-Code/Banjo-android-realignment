#ifdef __cplusplus
extern "C" {
#endif

#ifndef BANJO_KAZOOIE_CORE1_MEMORY_H
#define BANJO_KAZOOIE_CORE1_MEMORY_H
#ifdef __cplusplus
}
#endif
#include <n64_types.h>

#ifdef __cplusplus
extern "C" {
#endif
/**
* copy memory area
*/
void n64_memcpy(void * dst, void *src, int size);

/**
* copy an array of wide_characters
*/
void wmemcpy(void * dst, void *src, int size); 


/**
 * copy memory area
*/
void n64_memmove(u8* dst, u8* src, s32 n);
#endif


#ifdef __cplusplus
}
#endif
