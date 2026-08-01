#include <ultra64.h>
#include "functions.h"
#include "variables.h"

#define FF_QUESTION_ASKED_BITFIELD_SIZE 0x33

/* .bss */
u8* quizQuestionAskedBitfield;

/* .code */
n64_bool quizQuestionAskedBitfield_get(s32 index){
    return bitfieldarray_getBit(quizQuestionAskedBitfield, index);
}

void quizQuestionAskedBitfield_free(void){
    n64_free(quizQuestionAskedBitfield);
    quizQuestionAskedBitfield = NULL;
}

void quizQuestionAskedBitfield_init(void){
    int i;
    quizQuestionAskedBitfield =(u8*) n64_malloc(FF_QUESTION_ASKED_BITFIELD_SIZE);
    for(i = 0; i < FF_QUESTION_ASKED_BITFIELD_SIZE; i++){
        quizQuestionAskedBitfield[i] = 0;
    }
}

void quizQuestionAskedBitfield_set(s32 index, n64_bool value){
    bitfieldarray_setBit(quizQuestionAskedBitfield, index, value);
}

void quizQuestionAskedBitfield_defrag(void){
    if(quizQuestionAskedBitfield != NULL)
        quizQuestionAskedBitfield = (u8*)defrag(quizQuestionAskedBitfield);
}
