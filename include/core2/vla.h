#ifdef __cplusplus
extern "C" {
#endif

#ifndef _STATIC_LENGTH_ARRAY_H_
#define _STATIC_LENGTH_ARRAY_H_

/* Redirected */ #include <n64_types.h>

typedef struct variable_length_array{
    s32 elem_size;
    void * begin;
    void * end;
    void * mem_end;
    u8  data[];
}VLA;

#define vector(T) struct variable_length_array
//^defined to keep element type with vla

#endif


#ifdef __cplusplus
}
#endif
