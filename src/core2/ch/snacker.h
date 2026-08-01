#ifdef __cplusplus
extern "C" {
#endif

#ifndef __BANJO_KAZOOIE_CORE2_CH_SNACKER_H__
#define __BANJO_KAZOOIE_CORE2_CH_SNACKER_H__
#ifdef __cplusplus
}
#endif
#include <n64_types.h>

#include "prop.h" // for ActorInfo
#include "../snackerctl.h" //for SnackerCtlState

#ifdef __cplusplus
extern "C" {
#endif
extern ActorInfo gChSnacker;

void chSnacker_initialize(void);
void chsnacker_setControlState(SnackerCtlState nextState);

#endif // __BANJO_KAZOOIE_CORE2_CH_SNACKER_H__


#ifdef __cplusplus
}
#endif
