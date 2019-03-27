/*-
 * SPDX-License-Identifier: ISC
 *
 * Copyright (c) 2004 by Internet Systems Consortium, Inc. ("ISC")
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

/* ev_timers.c - implement timers for the eventlib
 * vix 09sep95 [initial]
 */

#if !defined(LINT) && !defined(CODECENTER)
static const char rcsid[] = "$Id: ev_timers.c,v 1.6 2005/04/27 04:56:36 sra Exp $";
#endif
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/* Import. */

#include "port_before.h"
#ifndef _LIBC
#include "fd_setsize.h"
#endif

#include <errno.h>

#ifndef _LIBC
#include <isc/assertions.h>
#endif
#include <isc/eventlib.h>
#include "eventlib_p.h"

#include "port_after.h"

/* Constants. */

#define	MILLION 1000000
#define BILLION 1000000000

/* Forward. */

#ifdef _LIBC
static int	__evOptMonoTime;
#else
static int due_sooner(void *, void *);
static void set_index(void *, int);
static void free_timer(void *, void *);
static void print_timer(void *, void *);
static void idle_timeout(evContext, void *, struct timespec, struct timespec);

/* Private type. */

typedef struct {
	evTimerFunc	func;
	void *		uap;
	struct timespec	lastTouched;
	struct timespec	max_idle;
	evTimer *	timer;
} idle_timer;
#endif

/* Public. */

struct timespec
evConsTime(time_t sec, long nsec) {
	struct timespec x;

	x.tv_sec = sec;
	x.tv_nsec = nsec;
	return (x);
}

struct timespec
evAddTime(struct timespec addend1, struct timespec addend2) {
	struct timespec x;

	x.tv_sec = addend1.tv_sec + addend2.tv_sec;
	x.tv_nsec = addend1.tv_nsec + addend2.tv_nsec;
	if (x.tv_nsec >= BILLION) {
		x.tv_sec++;
		x.tv_nsec -= BILLION;
	}
	return (x);
}

struct timespec
evSubTime(struct timespec minuend, struct timespec subtrahend) {
	struct timespec x;

	x.tv_sec = minuend.tv_sec - subtrahend.tv_sec;
	if (minuend.tv_nsec >= subtrahend.tv_nsec)
		x.tv_nsec = minuend.tv_nsec - subtrahend.tv_nsec;
	else {
		x.tv_nsec = BILLION - subtrahend.tv_nsec + minuend.tv_nsec;
		x.tv_sec--;
	}
	return (x);
}

int
evCmpTime(struct timespec a, struct timespec b) {
	long x = a.tv_sec - b.tv_sec;

	if (x == 0L)
		x = a.tv_nsec - b.tv_nsec;
	return (x < 0L ? (-1) : x > 0L ? (1) : (0));
}

struct timespec
evNowTime(void) {
	struct timeval now;
#ifdef CLOCK_REALTIME
	struct timespec tsnow;
	int m = CLOCK_REALTIME;

#ifdef CLOCK_MONOTONIC
	if (__evOptMonoTime)
		m = CLOCK_MONOTONIC;
#endif
	if (clock_gettime(m, &tsnow) == 0)
		return (tsnow);
#endif
	if (gettimeofday(&now, NULL) < 0)
		return (evConsTime(0, 0));
	return (evTimeSpec(now));
}

struct timespec
evUTCTime(void) {
	struct timeval now;
#ifdef CLOCK_REALTIME
	struct timespec tsnow;
	if (clock_gettime(CLOCK_REALTIME, &tsnow) == 0)
		return (tsnow);
#endif
	if (gettimeofday(&now, NULL) < 0)
		return (evConsTime(0, 0));
	return (evTimeSpec(now));
}

#ifndef _LIBC
struct timespec
evLastEventTime(evContext opaqueCtx) {
	evContext_p *ctx = opaqueCtx.opaque;

	return (ctx->lastEventTime);
}
#endif

struct timespec
evTimeSpec(struct timeval tv) {
	struct timespec ts;

	ts.tv_sec = tv.tv_sec;
	ts.tv_nsec = tv.tv_usec * 1000;
	return (ts);
}

#if !defined(USE_KQUEUE) || !defined(_LIBC)
struct timeval
evTimeVal(struct timespec ts) {
	struct timeval tv;

	tv.tv_sec = ts.tv_sec;
	tv.tv_usec = ts.tv_nsec / 1000;
	return (tv);
}
#endif

#ifndef _LIBC
int
evSetTimer(evContext opaqueCtx,
	   evTimerFunc func,
	   void *uap,
	   struct timespec due,
	   struct timespec inter,
	   evTimerID *opaqueID
) {
	evContext_p *ctx = opaqueCtx.opaque;
	evTimer *id;

	evPrintf(ctx, 1,
"evSetTimer(ctx %p, func %p, uap %p, due %ld.%09ld, inter %ld.%09ld)\n",
		 ctx, func, uap,
		 (long)due.tv_sec, due.tv_nsec,
		 (long)inter.tv_sec, inter.tv_nsec);

#ifdef __hpux
	/*
	 * tv_sec and tv_nsec are unsigned.
	 */
	if (due.tv_nsec >= BILLION)
		EV_ERR(EINVAL);

	if (inter.tv_nsec >= BILLION)
		EV_ERR(EINVAL);
#else
	if (due.tv_sec < 0 || due.tv_nsec < 0 || due.tv_nsec >= BILLION)
		EV_ERR(EINVAL);

	if (inter.tv_sec < 0 || inter.tv_nsec < 0 || inter.tv_nsec >= BILLION)
		EV_ERR(EINVAL);
#endif

	/* due={0,0} is a magic cookie meaning "now." */
	if (due.tv_sec == (time_t)0 && due.tv_nsec == 0L)
		due = evNowTime();

	/* Allocate and fill. */
	OKNEW(id);
	id->func = func;
	id->uap = uap;
	id->due = due;
	id->inter = inter;

	if (heap_insert(ctx->timers, id) < 0)
		return (-1);

	/* Remember the ID if the caller provided us a place for it. */
	if (opaqueID)
		opaqueID->opaque = id;

	if (ctx->debug > 7) {
		evPrintf(ctx, 7, "timers after evSetTimer:\n");
		(void) heap_for_each(ctx->timers, print_timer, (void *)ctx);
	}

	return (0);
}

int
evClearTimer(evContext opaqueCtx, evTimerID id) {
	evContext_p *ctx = opaqueCtx.opaque;
	evTimer *del = id.opaque;

	if (ctx->cur != NULL &&
	    ctx->cur->type == Timer &&
	    ctx->cur->u.timer.this == del) {
		evPrintf(ctx, 8, "deferring delete of timer (executing)\n");
		/*
		 * Setting the interval to zero ensures that evDrop() will
		 * clean up the timer.
		 */
		del->inter = evConsTime(0, 0);
		return (0);
	}

	if (heap_element(ctx->timers, del->index) != del)
		EV_ERR(ENOENT);

	if (heap_delete(ctx->timers, del->index) < 0)
		return (-1);
	FREE(del);

	if (ctx->debug > 7) {
		evPrintf(ctx, 7, "timers after evClearTimer:\n");
		(void) heap_for_each(ctx->timers, print_timer, (void *)ctx);
	}

	return (0);
}

int
evConfigTimer(evContext opaqueCtx,
	     evTimerID id,
	     const char *param,
	     int value
) {
	evContext_p *ctx = opaqueCtx.opaque;
	evTimer *timer = id.opaque;
	int result=0;

	UNUSED(value);

	if (heap_element(ctx->timers, timer->index) != timer)
		EV_ERR(ENOENT);

	if (strcmp(param, "rate") == 0)
		timer->mode |= EV_TMR_RATE;
	else if (strcmp(param, "interval") == 0)
		timer->mode &= ~EV_TMR_RATE;
	else
		EV_ERR(EINVAL);

	return (result);
}

int
evResetTimer(evContext opaqueCtx,
	     evTimerID id,
	     evTimerFunc func,
	     void *uap,
	     struct timespec due,
	     struct timespec inter
) {
	evContext_p *ctx = opaqueCtx.opaque;
	evTimer *timer = id.opaque;
	struct timespec old_due;
	int result=0;

	if (heap_element(ctx->timers, timer->index) != timer)
		EV_ERR(ENOENT);

#ifdef __hpux
	/*
	 * tv_sec and tv_nsec are unsigned.
	 */
	if (due.tv_nsec >= BILLION)
		EV_ERR(EINVAL);

	if (inter.tv_nsec >= BILLION)
		EV_ERR(EINVAL);
#else
	if (due.tv_sec < 0 || due.tv_nsec < 0 || due.tv_nsec >= BILLION)
		EV_ERR(EINVAL);

	if (inter.tv_sec < 0 || inter.tv_nsec < 0 || inter.tv_nsec >= BILLION)
		EV_ERR(EINVAL);
#endif

	old_due = timer->due;

	timer->func = func;
	timer->uap = uap;
	timer->due = due;
	timer->inter = inter;

	switch (evCmpTime(due, old_due)) {
	case -1:
		result = heap_increased(ctx->timers, timer->index);
		break;
	case 0:
		result = 0;
		break;
	case 1:
		result = heap_decreased(ctx->timers, timer->index);
		break;
	}

	if (ctx->debug > 7) {
		evPrintf(ctx, 7, "timers after evResetTimer:\n");
		(void) heap_for_each(ctx->timers, print_timer, (void *)ctx);
	}

	return (result);
}

int
evSetIdleTimer(evContext opaqueCtx,
		evTimerFunc func,
		void *uap,
		struct timespec max_idle,
		evTimerID *opaqueID
) {
	evContext_p *ctx = opaqueCtx.opaque;
	idle_timer *tt;

	/* Allocate and fill. */
	OKNEW(tt);
	tt->func = func;
	tt->uap = uap;
	tt->lastTouched = ctx->lastEventTime;
	tt->max_idle = max_idle;

	if (evSetTimer(opaqueCtx, idle_timeout, tt,
		       evAddTime(ctx->lastEventTime, max_idle),
		       max_idle, opaqueID) < 0) {
		FREE(tt);
		return (-1);
	}

	tt->timer = opaqueID->opaque;

	return (0);
}

int
evClearIdleTimer(evContext opaqueCtx, evTimerID id) {
	evTimer *del = id.opaque;
	idle_timer *tt = del->uap;

	FREE(tt);
	return (evClearTimer(opaqueCtx, id));
}

int
evResetIdleTimer(evContext opaqueCtx,
		 evTimerID opaqueID,
		 evTimerFunc func,
		 void *uap,
		 struct timespec max_idle
) {
	evContext_p *ctx = opaqueCtx.opaque;
	evTimer *timer = opaqueID.opaque;
	idle_timer *tt = timer->uap;

	tt->func = func;
	tt->uap = uap;
	tt->lastTouched = ctx->lastEventTime;
	tt->max_idle = max_idle;

	return (evResetTimer(opaqueCtx, opaqueID, idle_timeout, tt,
			     evAddTime(ctx->lastEventTime, max_idle),
			     max_idle));
}

int
evTouchIdleTimer(evContext opaqueCtx, evTimerID id) {
	evContext_p *ctx = opaqueCtx.opaque;
	evTimer *t = id.opaque;
	idle_timer *tt = t->uap;

	tt->lastTouched = ctx->lastEventTime;

	return (0);
}

/* Public to the rest of eventlib. */

heap_context
evCreateTimers(const evContext_p *ctx) {

	UNUSED(ctx);

	return (heap_new(due_sooner, set_index, 2048));
}

void
evDestroyTimers(const evContext_p *ctx) {
	(void) heap_for_each(ctx->timers, free_timer, NULL);
	(void) heap_free(ctx->timers);
}

/* Private. */

static int
due_sooner(void *a, void *b) {
	evTimer *a_timer, *b_timer;

	a_timer = a;
	b_timer = b;
	return (evCmpTime(a_timer->due, b_timer->due) < 0);
}

static void
set_index(void *what, int index) {
	evTimer *timer;

	timer = what;
	timer->index = index;
}

static void
free_timer(void *what, void *uap) {
	evTimer *t = what;

	UNUSED(uap);

	FREE(t);
}

static void
print_timer(void *what, void *uap) {
	evTimer *cur = what;
	evContext_p *ctx = uap;

	cur = what;
	evPrintf(ctx, 7,
	    "  func %p, uap %p, due %ld.%09ld, inter %ld.%09ld\n",
		 cur->func, cur->uap,
		 (long)cur->due.tv_sec, cur->due.tv_nsec,
		 (long)cur->inter.tv_sec, cur->inter.tv_nsec);
}

static void
idle_timeout(evContext opaqueCtx,
	     void *uap,
	     struct timespec due,
	     struct timespec inter
) {
	evContext_p *ctx = opaqueCtx.opaque;
	idle_timer *this = uap;
	struct timespec idle;

	UNUSED(due);
	UNUSED(inter);
	
	idle = evSubTime(ctx->lastEventTime, this->lastTouched);
	if (evCmpTime(idle, this->max_idle) >= 0) {
		(this->func)(opaqueCtx, this->uap, this->timer->due,
			     this->max_idle);
		/*
		 * Setting the interval to zero will cause the timer to
		 * be cleaned up in evDrop().
		 */
		this->timer->inter = evConsTime(0, 0);
		FREE(this);
	} else {
		/* evDrop() will reschedule the timer. */
		this->timer->inter = evSubTime(this->max_idle, idle);
	}
}
#endif

/*! \file */
