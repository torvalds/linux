/*-
 * SPDX-License-Identifier: ISC
 *
 * Copyright (C) 2004, 2005, 2008  Internet Systems Consortium, Inc. ("ISC")
 * Copyright (C) 1995-1999, 2001, 2003  Internet Software Consortium.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE
 * OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

/* eventlib.h - exported interfaces for eventlib
 * vix 09sep95 [initial]
 *
 * $Id: eventlib.h,v 1.7 2008/11/14 02:36:51 marka Exp $
 */

#ifndef _EVENTLIB_H
#define _EVENTLIB_H

#include <sys/types.h>
#include <sys/uio.h>
#include <sys/time.h>
#include <stdio.h>

#include <isc/platform.h>

#ifndef __P
# define __EVENTLIB_P_DEFINED
# ifdef __STDC__
#  define __P(x) x
# else
#  define __P(x) ()
# endif
#endif

/* In the absence of branded types... */
typedef struct { void *opaque; } evConnID;
typedef struct { void *opaque; } evFileID;
typedef struct { void *opaque; } evStreamID;
typedef struct { void *opaque; } evTimerID;
typedef struct { void *opaque; } evWaitID;
typedef struct { void *opaque; } evContext;
typedef struct { void *opaque; } evEvent;

#define	evInitID(id) ((id)->opaque = NULL)
#define	evTestID(id) ((id).opaque != NULL)

typedef void (*evConnFunc)__P((evContext, void *, int, const void *, int,
			       const void *, int));
typedef void (*evFileFunc)__P((evContext, void *, int, int));
typedef	void (*evStreamFunc)__P((evContext, void *, int, int));
typedef void (*evTimerFunc)__P((evContext, void *,
				struct timespec, struct timespec));
typedef	void (*evWaitFunc)__P((evContext, void *, const void *));

typedef	struct { unsigned char mask[256/8]; } evByteMask;
#define	EV_BYTEMASK_BYTE(b) ((b) / 8)
#define	EV_BYTEMASK_MASK(b) (1 << ((b) % 8))
#define	EV_BYTEMASK_SET(bm, b) \
	((bm).mask[EV_BYTEMASK_BYTE(b)] |= EV_BYTEMASK_MASK(b))
#define	EV_BYTEMASK_CLR(bm, b) \
	((bm).mask[EV_BYTEMASK_BYTE(b)] &= ~EV_BYTEMASK_MASK(b))
#define	EV_BYTEMASK_TST(bm, b) \
	((bm).mask[EV_BYTEMASK_BYTE(b)] & EV_BYTEMASK_MASK(b))

#define	EV_POLL		1
#define	EV_WAIT		2
#define	EV_NULL		4

#define	EV_READ		1
#define	EV_WRITE	2
#define	EV_EXCEPT	4

#define EV_WASNONBLOCKING 8	/* Internal library use. */

/* eventlib.c */
#define evCreate	__evCreate
#define evSetDebug	__evSetDebug
#define evDestroy	__evDestroy
#define evGetNext	__evGetNext
#define evDispatch	__evDispatch
#define evDrop		__evDrop
#define evMainLoop	__evMainLoop
#define evHighestFD	__evHighestFD
#define evGetOption	__evGetOption
#define evSetOption	__evSetOption

int  evCreate __P((evContext *));
void evSetDebug __P((evContext, int, FILE *));
int  evDestroy __P((evContext));
int  evGetNext __P((evContext, evEvent *, int));
int  evDispatch __P((evContext, evEvent));
void evDrop __P((evContext, evEvent));
int  evMainLoop __P((evContext));
int  evHighestFD __P((evContext));
int  evGetOption __P((evContext *, const char *, int *));
int  evSetOption __P((evContext *, const char *, int));

/* ev_connects.c */
#define evListen	__evListen
#define evConnect	__evConnect
#define evCancelConn	__evCancelConn
#define evHold		__evHold
#define evUnhold	__evUnhold
#define evTryAccept	__evTryAccept

int evListen __P((evContext, int, int, evConnFunc, void *, evConnID *));
int evConnect __P((evContext, int, const void *, int,
		   evConnFunc, void *, evConnID *));
int evCancelConn __P((evContext, evConnID));
int evHold __P((evContext, evConnID));
int evUnhold __P((evContext, evConnID));
int evTryAccept __P((evContext, evConnID, int *));

/* ev_files.c */
#define evSelectFD	__evSelectFD
#define evDeselectFD	__evDeselectFD

int evSelectFD __P((evContext, int, int, evFileFunc, void *, evFileID *));
int evDeselectFD __P((evContext, evFileID));

/* ev_streams.c */
#define evConsIovec	__evConsIovec
#define evWrite		__evWrite
#define evRead		__evRead
#define evTimeRW	__evTimeRW
#define evUntimeRW	__evUntimeRW
#define	evCancelRW	__evCancelRW

struct iovec evConsIovec __P((void *, size_t));
int evWrite __P((evContext, int, const struct iovec *, int,
		 evStreamFunc func, void *, evStreamID *));
int evRead __P((evContext, int, const struct iovec *, int,
		evStreamFunc func, void *, evStreamID *));
int evTimeRW __P((evContext, evStreamID, evTimerID timer));
int evUntimeRW __P((evContext, evStreamID));
int evCancelRW __P((evContext, evStreamID));

/* ev_timers.c */
#define evConsTime	__evConsTime
#define evAddTime	__evAddTime
#define evSubTime	__evSubTime
#define evCmpTime	__evCmpTime
#define	evTimeSpec	__evTimeSpec
#define	evTimeVal	__evTimeVal

#define evNowTime		__evNowTime
#define evUTCTime		__evUTCTime
#define evLastEventTime		__evLastEventTime
#define evSetTimer		__evSetTimer
#define evClearTimer		__evClearTimer
#define evConfigTimer		__evConfigTimer
#define evResetTimer		__evResetTimer
#define evSetIdleTimer		__evSetIdleTimer
#define evClearIdleTimer	__evClearIdleTimer
#define evResetIdleTimer	__evResetIdleTimer
#define evTouchIdleTimer	__evTouchIdleTimer

struct timespec evConsTime __P((time_t sec, long nsec));
struct timespec evAddTime __P((struct timespec, struct timespec));
struct timespec evSubTime __P((struct timespec, struct timespec));
struct timespec evNowTime __P((void));
struct timespec evUTCTime __P((void));
struct timespec evLastEventTime __P((evContext));
struct timespec evTimeSpec __P((struct timeval));
struct timeval evTimeVal __P((struct timespec));
int evCmpTime __P((struct timespec, struct timespec));
int evSetTimer __P((evContext, evTimerFunc, void *, struct timespec,
		    struct timespec, evTimerID *));
int evClearTimer __P((evContext, evTimerID));
int evConfigTimer __P((evContext, evTimerID, const char *param,
		      int value));
int evResetTimer __P((evContext, evTimerID, evTimerFunc, void *,
		      struct timespec, struct timespec));
int evSetIdleTimer __P((evContext, evTimerFunc, void *, struct timespec,
			evTimerID *));
int evClearIdleTimer __P((evContext, evTimerID));
int evResetIdleTimer __P((evContext, evTimerID, evTimerFunc, void *,
			  struct timespec));
int evTouchIdleTimer __P((evContext, evTimerID));

/* ev_waits.c */
#define evWaitFor	__evWaitFor
#define evDo		__evDo
#define evUnwait	__evUnwait
#define evDefer		__evDefer

int evWaitFor __P((evContext, const void *, evWaitFunc, void *, evWaitID *));
int evDo __P((evContext, const void *));
int evUnwait __P((evContext, evWaitID));
int evDefer __P((evContext, evWaitFunc, void *));

#ifdef __EVENTLIB_P_DEFINED
# undef __P
#endif

#endif /*_EVENTLIB_H*/

/*! \file */
