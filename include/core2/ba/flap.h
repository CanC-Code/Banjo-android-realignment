#ifdef __cplusplus
extern "C" {
#endif

#ifndef __BA_FLAP_H__
#define __BA_FLAP_H__

#ifdef __cplusplus
}
#endif
#include <ultra64.h>

#ifdef __cplusplus
extern "C" {
#endif
s32 baflap_getCount(void);
bool baflap_add(f32 duration);
void baflap_reset(void);
void baflap_activate(bool active);
void baflap_update(void);

#endif // __BA_FLAP_H__


#ifdef __cplusplus
}
#endif
