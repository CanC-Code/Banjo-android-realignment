#ifdef __cplusplus
extern "C" {
#endif

#ifndef BANJO_KAZOOIE_CORE1_SPRITE_H
#define BANJO_KAZOOIE_CORE1_SPRITE_H
#ifdef __cplusplus
}
#endif
#include <n64_types.h>

#ifdef __cplusplus
extern "C" {
#endif
u32 sprite_getUnk8(BKSprite *this);
u32 sprite_getUnkA(BKSprite *this);
u32 sprite_getUnk6(BKSprite *this);
u32 sprite_getUnk4(BKSprite *this);
s32 sprite_getFrameCount(BKSprite *this);
BKSpriteFrame *sprite_getFramePtr(BKSprite *this, u32 frame_id);

#endif


#ifdef __cplusplus
}
#endif
