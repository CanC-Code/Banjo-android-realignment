#include <ultra64.h>
#include "core1/core1.h"
#include "functions.h"
#include "variables.h"

#include <unistd.h>   // for usleep

#define INIT_THREAD_STACK_SIZE 0x200

u8 sInitThreadStack[INIT_THREAD_STACK_SIZE]; // Size based on the previous symbol's address
OSThread sInitThread;

void initThread_entry(void *arg);

void initThread_create(void) {
    osCreateThread(&sInitThread, 1, initThread_entry, NULL, sInitThreadStack + INIT_THREAD_STACK_SIZE, OS_PRIORITY_IDLE);
    osStartThread(&sInitThread);
}

void piMgr_init(void);
void mainThread_create(void);
OSThread *mainThread_get(void);

// Bridge functions to release/acquire the global interpreter lock.
extern "C" {
    void BKA_DropEngineLock(void);
    void BKA_ClaimEngineLock(void);
}

void initThread_entry(void *arg) {
    piMgr_init();
    mainThread_create();
    osStartThread(mainThread_get());

    // FIXED: The original N64 idle loop was "while (1);" which is safe on
    // cooperative non-preemptive hardware.  On Android, this loop runs inside
    // a POSIX thread that holds the s_n64_gil mutex.  Holding the GIL
    // forever prevents the main game thread (and every other N64 thread)
    // from ever acquiring it, causing a permanent deadlock and ANR.
    //
    // Yield the GIL on each iteration so the scheduler can activate the
    // main thread.
    while (1) {
        BKA_DropEngineLock();
        usleep(1000);          // 1 ms — let other threads run
        BKA_ClaimEngineLock();
    }
}