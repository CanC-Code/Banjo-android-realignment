#ifdef __cplusplus
extern "C" {
#endif

#ifndef STRING_H
#define STRING_H
#ifdef __cplusplus
}
#endif
#include <n64_types.h>

#include "structs.h"

#ifdef __cplusplus
extern "C" {
#endif
void n64_strcat(char *dst, char *src);
void strcatc(char *dst, char src);
void strFToA(char *dst, f32 val);
void _strFToA(char *dst, f32 val, s32 decPlaces);
void strIToA(char *str, s32 num);
void _strIToA(char *str, s32 num, char base);
void n64_strcpy(char *dst, char *src);
s32 n64_strlen(char *str);
void strToUpper(char *str);



#endif


#ifdef __cplusplus
}
#endif
