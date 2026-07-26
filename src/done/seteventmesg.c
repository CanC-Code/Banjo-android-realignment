#include <PR/os_internal.h>
#include "osint.h"
#include <n64_types.h>
__OSEventState __osEventStateTab[OS_NUM_EVENTS];
void osSetEventMesg(OSEvent event, OSMesgQueue *mq, OSMesg msg)
{
	register u32 saveMask = __osDisableInt();
	__OSEventState *es;

	es = &__osEventStateTab[event];
	es->messageQueue = mq;
	es->message = msg;
	__osRestoreInt(saveMask);
}
