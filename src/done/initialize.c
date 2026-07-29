#include <ultra64.h>
#include "core1/core1.h"
#include "functions.h"
#include "variables.h"

#include <unistd.h>

#define INIT_THREAD_STACK_SIZE 0x200

u8 sInitThreadStack[INIT_THREAD_STACK_SIZE];
OSThread sInitThread;

void initThread_entry(void *arg);

void initThread_create(void) {
    osCreateThread(&sInitThread, 1, initThread_entry, NULL, sInitThreadStack + INIT_THREAD_STACK_SIZE, OS_PRIORITY_IDLE);
    osStartThread(&sInitThread);
}

void piMgr_init(void);
void mainThread_create(void);
OSThread *mainThread_get(void);

// Declared in emulator/stubs.cpp with C linkage
void BKA_DropEngineLock(void);
void BKA_ClaimEngineLock(void);

void initThread_entry(void *arg) {
    piMgr_init();
    mainThread_create();
    osStartThread(mainThread_get());

    // The original N64 idle loop "while(1);" holds the s_n64_gil mutex
    // forever, deadlocking all other N64 threads. We yield the GIL so
    // the main game thread can run.
    while (1) {
        BKA_DropEngineLock();
        usleep(1000);
        BKA_ClaimEngineLock();
    }
}