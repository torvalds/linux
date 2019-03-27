/*-
 * SPDX-License-Identifier: ISC
 *
 * Copyright (c) 2005 by Internet Systems Consortium, Inc. ("ISC")
 * Copyright (c) 1995-1999 by Internet Software Consortium
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT
 * OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*! \file 
 * \brief private interfaces for eventlib
 * \author vix 09sep95 [initial]
 *
 * $Id: eventlib_p.h,v 1.9 2006/03/09 23:57:56 marka Exp $
 * $FreeBSD$
 */

#ifndef _EVENTLIB_P_H
#define _EVENTLIB_P_H

#include <sys/param.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <sys/un.h>

#define EVENTLIB_DEBUG 1

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _LIBC
#include <isc/list.h>
#include <isc/heap.h>
#include <isc/memcluster.h>
#endif

#define	EV_MASK_ALL	(EV_READ | EV_WRITE | EV_EXCEPT)
#define EV_ERR(e)		return (errno = (e), -1)
#define OK(x)		if ((x) < 0) EV_ERR(errno); else (void)NULL
#define OKFREE(x, y)	if ((x) < 0) { FREE((y)); EV_ERR(errno); } \
			else (void)NULL

#define	NEW(p)		if (((p) = memget(sizeof *(p))) != NULL) \
				FILL(p); \
			else \
				(void)NULL;
#define OKNEW(p)	if (!((p) = memget(sizeof *(p)))) { \
				errno = ENOMEM; \
				return (-1); \
			} else \
				FILL(p)
#define FREE(p)		memput((p), sizeof *(p))

#if EVENTLIB_DEBUG
#define FILL(p)		memset((p), 0xF5, sizeof *(p))
#else
#define FILL(p)
#endif

#ifdef USE_POLL
#ifdef HAVE_STROPTS_H
#include <stropts.h>
#endif
#include <poll.h>
#endif /* USE_POLL */

typedef struct evConn {
	evConnFunc	func;
	void *		uap;
	int		fd;
	int		flags;
#define EV_CONN_LISTEN		0x0001		/*%< Connection is a listener. */
#define EV_CONN_SELECTED	0x0002		/*%< evSelectFD(conn->file). */
#define EV_CONN_BLOCK		0x0004		/*%< Listener fd was blocking. */
	evFileID	file;
	struct evConn *	prev;
	struct evConn *	next;
} evConn;

#ifndef _LIBC
typedef struct evAccept {
	int		fd;
	union {
		struct sockaddr		sa;
		struct sockaddr_in	in;
#ifndef NO_SOCKADDR_UN
		struct sockaddr_un	un;
#endif
	}		la;
	ISC_SOCKLEN_T	lalen;
	union {
		struct sockaddr		sa;
		struct sockaddr_in	in;
#ifndef NO_SOCKADDR_UN
		struct sockaddr_un	un;
#endif
	}		ra;
	ISC_SOCKLEN_T	ralen;
	int		ioErrno;
	evConn *	conn;
	LINK(struct evAccept) link;
} evAccept;

typedef struct evFile {
	evFileFunc	func;
	void *		uap;
	int		fd;
	int		eventmask;
	int		preemptive;
	struct evFile *	prev;
	struct evFile *	next;
	struct evFile *	fdprev;
	struct evFile *	fdnext;
} evFile;

typedef struct evStream {
	evStreamFunc	func;
	void *		uap;
	evFileID	file;
	evTimerID	timer;
	int		flags;
#define EV_STR_TIMEROK	0x0001	/*%< IFF timer valid. */
	int		fd;
	struct iovec *	iovOrig;
	int		iovOrigCount;
	struct iovec *	iovCur;
	int		iovCurCount;
	int		ioTotal;
	int		ioDone;
	int		ioErrno;
	struct evStream	*prevDone, *nextDone;
	struct evStream	*prev, *next;
} evStream;

typedef struct evTimer {
	evTimerFunc	func;
	void *		uap;
	struct timespec	due, inter;
	int		index;
	int		mode;
#define EV_TMR_RATE	1
} evTimer;

typedef struct evWait {
	evWaitFunc	func;
	void *		uap;
	const void *	tag;
	struct evWait *	next;
} evWait;

typedef struct evWaitList {
	evWait *		first;
	evWait *		last;
	struct evWaitList *	prev;
	struct evWaitList *	next;
} evWaitList;

typedef struct evEvent_p {
	enum {  Accept, File, Stream, Timer, Wait, Free, Null  } type;
	union {
		struct {  evAccept *this;  }			accept;
		struct {  evFile *this; int eventmask;  }	file;
		struct {  evStream *this;  }			stream;
		struct {  evTimer *this;  }			timer;
		struct {  evWait *this;  }			wait;
		struct {  struct evEvent_p *next;  }		free;
		struct {  const void *placeholder;  }		null;
	} u;
} evEvent_p;
#endif

#ifdef USE_POLL
typedef struct { 
	void		*ctx;	/* pointer to the evContext_p   */ 
	uint32_t	type;	/* READ, WRITE, EXCEPT, nonblk  */ 
	uint32_t	result;	/* 1 => revents, 0 => events    */ 
} __evEmulMask; 

#define emulMaskInit(ctx, field, ev, lastnext) \
	ctx->field.ctx = ctx; \
	ctx->field.type = ev; \
	ctx->field.result = lastnext; 
  
extern short	*__fd_eventfield(int fd, __evEmulMask *maskp); 
extern short	__poll_event(__evEmulMask *maskp); 
extern void		__fd_clr(int fd, __evEmulMask *maskp); 
extern void		__fd_set(int fd, __evEmulMask *maskp); 

#undef  FD_ZERO 
#define FD_ZERO(maskp) 
  
#undef  FD_SET 
#define FD_SET(fd, maskp) \
	__fd_set(fd, maskp) 

#undef  FD_CLR 
#define FD_CLR(fd, maskp) \
	__fd_clr(fd, maskp) 

#undef  FD_ISSET 
#define FD_ISSET(fd, maskp) \
	((*__fd_eventfield(fd, maskp) & __poll_event(maskp)) != 0) 

#endif /* USE_POLL */

#ifndef _LIBC
typedef struct {
	/* Global. */
	const evEvent_p	*cur;
	/* Debugging. */
	int		debug;
	FILE		*output;
	/* Connections. */
	evConn		*conns;
	LIST(evAccept)	accepts;
	/* Files. */
	evFile		*files, *fdNext;
#ifndef USE_POLL
	fd_set		rdLast, rdNext;
	fd_set		wrLast, wrNext;
	fd_set		exLast, exNext;
	fd_set		nonblockBefore;
	int		fdMax, fdCount, highestFD;
	evFile		*fdTable[FD_SETSIZE];
#else
	struct pollfd	*pollfds;	/* Allocated as needed  */ 
	evFile		**fdTable;	/* Ditto                */ 
	int		maxnfds;	/* # elements in above  */ 
	int		firstfd;	/* First active fd      */ 
	int		fdMax;		/* Last active fd       */ 
	int		fdCount;	/* # fd:s with I/O      */ 
	int		highestFD;	/* max fd allowed by OS */ 
	__evEmulMask	rdLast, rdNext; 
	__evEmulMask	wrLast, wrNext; 
	__evEmulMask	exLast, exNext; 
	__evEmulMask	nonblockBefore; 
#endif /* USE_POLL */
#ifdef EVENTLIB_TIME_CHECKS
	struct timespec	lastSelectTime;
	int		lastFdCount;
#endif
	/* Streams. */
	evStream	*streams;
	evStream	*strDone, *strLast;
	/* Timers. */
	struct timespec	lastEventTime;
	heap_context	timers;
	/* Waits. */
	evWaitList	*waitLists;
	evWaitList	waitDone;
} evContext_p;

/* eventlib.c */
#define evPrintf __evPrintf
void evPrintf(const evContext_p *ctx, int level, const char *fmt, ...)
     ISC_FORMAT_PRINTF(3, 4);

#ifdef USE_POLL
extern int evPollfdRealloc(evContext_p *ctx, int pollfd_chunk_size, int fd);
#endif /* USE_POLL */

/* ev_timers.c */
#define evCreateTimers __evCreateTimers
heap_context evCreateTimers(const evContext_p *);
#define evDestroyTimers __evDestroyTimers
void evDestroyTimers(const evContext_p *);

/* ev_waits.c */
#define evFreeWait __evFreeWait
evWait *evFreeWait(evContext_p *ctx, evWait *old);
#endif

/* Global options */
#ifndef _LIBC
extern int	__evOptMonoTime;
#endif

#endif /*_EVENTLIB_P_H*/
