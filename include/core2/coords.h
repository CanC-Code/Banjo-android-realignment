#ifdef __cplusplus
extern "C" {
#endif

#ifdef __cplusplus
}
#endif
#include <ultra64.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Redirected */ #include <n64_bool.h>

void coords_copy(s32 min_out[3], s32 max_out[3], s32 min_in[3], s32 max_in[3]);
s32 coords_func_8033EAF8(s32 min[3], s32 max[3], f32 position[3], f32 direction[3]);
n64_bool coords_isPointInside(s32 min[3], s32 max[3], s32 point[3]);
n64_bool coords_isPointInsideWithOffset(s32 min[3], s32 max[3], s32 point[3], s32 offset);
void coords_scale(s32 min[3], s32 max[3], s32 ratio);
void coords_translate(s32 min[3], s32 max[3], f32 translation[3]);


#ifdef __cplusplus
}
#endif
