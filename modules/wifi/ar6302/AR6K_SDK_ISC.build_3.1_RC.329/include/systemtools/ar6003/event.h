/* event.h - contains definitions for event.c */

/* Copyright (c) 2000 Atheros Communications, Inc., All Rights Reserved */

#ident  "ACI $Id: //depot/sw/releases/olca3.1-RC/include/systemtools/ar6003/event.h#1 $, $Header: //depot/sw/releases/olca3.1-RC/include/systemtools/ar6003/event.h#1 $"

/* 
modification history
--------------------
00a    10oct00    fjc    Created.
*/

/*
DESCRIPTION
Contains the definitions for the low level event handling functions
*/

#ifdef __cplusplus
extern "C" {
#endif

#ifndef __INCeventh
#define __INCeventh

#include "wlantype.h"


typedef struct eHandle {
    A_UINT16 eventID;
    A_UINT16 f2Handle;
} EVT_HANDLE;

struct eventArrayStruct ;

typedef struct eventStruct {
    EVT_HANDLE          eventHandle;
    A_UINT32            type;
    A_UINT32            persistent;
    A_UINT32            param1;
    A_UINT32            param2;
    A_UINT32            param3;

#if defined(ART_BUILD)
    A_UINT32            result[6];
#else
    A_UINT32            result;
#ifdef MAUI
	A_UINT32	additionalParams[5];
#endif
    struct eventStruct  *pNext;         // pointer to next event
    struct eventStruct  *pLast;         // backward pointer to pervious event
    int free;
    struct eventArrayStruct  *eventArrayPtr; 	
#endif
} EVENT_STRUCT;

#ifdef AR6000
#define EVENT_ARRAY_SIZE    50
#else
#define EVENT_ARRAY_SIZE    100
#endif

typedef struct eventArrayStruct  {
	EVENT_STRUCT  eventElement[EVENT_ARRAY_SIZE];   /* eventArray Allocation  , MAX=100  */
        void	*arrayLock; // lock to make Array access mutually exclusive
} EVENT_ARRAY ;


typedef struct eventQueue {
    EVENT_STRUCT    *pHead;     // pointer to first event in queue
    EVENT_STRUCT    *pTail;     // pointer to last event in queue
    void			*queueLock; // lock to make queue access mutually exclusive
    A_UINT16        queueSize;  // count of how many items are in queue
    A_BOOL          queueScan;  // set to true if in middle of a queue scan
} EVENT_QUEUE;


#ifndef MALLOC_ABSENT
EVENT_QUEUE *initEventQueue
    (
    void
    );
#else
void initEventQueue
    (
    EVENT_QUEUE        *pQueue
    );
#endif

void deleteEventQueue
    (
    EVENT_QUEUE        *pQueue,                /* pointer to the queue to delete */
    A_BOOL        protect  
    );


EVENT_STRUCT *createEvent
    (
    A_UINT32    type,          // the event ID
    A_UINT32    persistent,    // set if want a persistent event
    A_UINT32    param1,        // optional args
    A_UINT32    param2,
    A_UINT32    param3,
    EVT_HANDLE  eventHandle ,   // unique handle of event
    EVENT_ARRAY *eventArray ,
    A_BOOL        protect 
    );

EVENT_STRUCT *copyEvent
    (
    EVENT_STRUCT *pExistingEvent, // pointer to event to copy
    EVENT_ARRAY *eventArray ,	
    A_BOOL        protect 	
    );

A_UINT16 pushEvent
    (
    EVENT_STRUCT    *pEvent,    // pointer to event to add
    EVENT_QUEUE     *pQueue,     // pointer to queue to add to
    A_BOOL        protect  
    );

EVENT_STRUCT *popEvent
    (
    EVENT_QUEUE *pQueue , // pointer to queue to add to
    A_BOOL        protect  
    );


A_UINT16 removeEvent
    (
    EVENT_STRUCT    *pEvent,
    EVENT_QUEUE     *pQueue,
    A_BOOL          protect
    );

EVENT_STRUCT *startQueueScan
    (
    EVENT_QUEUE *pQueue ,
    A_BOOL        protect  
    );

A_UINT16 stopQueueScan
    (
    EVENT_QUEUE *pQueue ,
    A_BOOL        protect  
    );

A_UINT16 checkForEvents
    (
    EVENT_QUEUE *pQueue ,
    A_BOOL        protect  
    );



void initEventArray(EVENT_ARRAY * eventArray);
EVENT_STRUCT *getEvent(EVENT_ARRAY * eventArray,A_BOOL protect);
void freeEvent(EVENT_STRUCT * event,A_BOOL protect);


#endif /*__INCeventh */

#ifdef __cplusplus
}
#endif
