/* anwievent.h - contains definitions for event.c */

/*
 * Copyright (c) 2000 Atheros Communications, Inc., All Rights Reserved
 * $ATH_LICENSE_TARGET_C$
 */

#ifndef __ANWIEVENT_H_
#define __ANWIEVENT_H_

#include "ntddk.h"

#define INTERRUPT_F2    1
#define TIMEOUT         4
#define ISR_INTERRUPT   0x10
#define DEFAULT_TIMEOUT 0xff

typedef struct anwiEventHandle_ {
    USHORT eventID;
    USHORT f2Handle;
} anwiEventHandle;


struct anwiEventStruct_ {
	struct anwiEventStruct_ *pNext;         // pointer to next event
	struct anwiEventStruct_ *pLast;         // backward pointer to pervious event
	anwiEventHandle    eventHandle;
	ULONG32            type;
	ULONG32            persistent;
	ULONG32            param1;
	ULONG32            param2;
	ULONG32            param3;
	ULONG32            result[6];
};

typedef struct anwiEventStruct_ anwiEventStruct;
typedef struct anwiEventStruct_ *pAnwiEventStruct;

typedef struct anwiEventQueue_ {
	pAnwiEventStruct   pHead;     // pointer to first event in queue
	pAnwiEventStruct   pTail;     // pointer to last event in queue
	USHORT       queueSize;  // count of how many items are in queue
	KIRQL		 oldIrql;
	KIRQL		 syncIrql;
} anwiEventQueue, *pAnwiEventQueue;

void initEventQueue(pAnwiEventQueue,KIRQL);

void deleteEventQueue(pAnwiEventQueue);

pAnwiEventStruct createEvent
(
	ULONG32    type,          // the event ID
	ULONG32    persistent,    // set if want a persistent event
	ULONG32    param1,        // optional args
	ULONG32    param2,
	ULONG32    param3,
	anwiEventHandle eventHandle    // unique handle of event
);

pAnwiEventStruct copyEvent
(
	pAnwiEventStruct pExistingEvent // pointer to event to copy
);

USHORT pushEvent
(
	pAnwiEventStruct pEvent,    // pointer to event to add
	pAnwiEventQueue pQueue,     // pointer to queue to add to
	BOOLEAN          protect
);

pAnwiEventStruct popEvent
(
	pAnwiEventQueue pQueue, // pointer to queue to add to
	BOOLEAN          protect
);

USHORT removeEvent
(
	pAnwiEventStruct    pEvent,
	pAnwiEventQueue     pQueue,
	BOOLEAN          protect
);

USHORT checkForEvents
(
	pAnwiEventQueue pQueue,
	BOOLEAN          protect
);

#endif
