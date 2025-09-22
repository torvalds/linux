/*
 * Copyright (c) 1989 Jan-Simon Pendry
 * Copyright (c) 1989 Imperial College of Science, Technology & Medicine
 * Copyright (c) 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Jan-Simon Pendry at Imperial College, London.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	from: @(#)clock.c	8.1 (Berkeley) 6/6/93
 *	$Id: clock.c,v 1.7 2014/10/26 03:08:21 guenther Exp $
 */

/*
 * Callouts.
 *
 * Modelled on kernel object of the same name.
 * See usual references.
 *
 * Use of a heap-based mechanism was rejected:
 * 1.  more complex implementation needed.
 * 2.  not obvious that a list is too slow for Amd.
 */

#include "am.h"

typedef struct callout callout;
struct callout {
	callout	*c_next;		/* List of callouts */
	void	(*c_fn)(void *);	/* Function to call */
	void	*c_closure;		/* Closure to pass to call */
	time_t	c_time;			/* Time of call */
	int	c_id;			/* Unique identifier */
};

static callout callouts;		/* List of pending callouts */
static callout *free_callouts;		/* Cache of free callouts */
static int nfree_callouts;		/* Number on free list */
static int callout_id;			/* Next free callout identifier */
time_t next_softclock;			/* Time of next call to softclock() */

/*
 * Number of callout slots we keep on the free list
 */
#define	CALLOUT_FREE_SLOP	10

/*
 * Global assumption: valid id's are non-zero.
 */
#define	CID_ALLOC()	(++callout_id)
#define	CID_UNDEF	(0)

static callout *
alloc_callout(void)
{
	callout *cp = free_callouts;
	if (cp) {
		--nfree_callouts;
		free_callouts = free_callouts->c_next;
		return cp;
	}
	return ALLOC(callout);
}

static void
free_callout(callout *cp)
{
	if (nfree_callouts > CALLOUT_FREE_SLOP) {
		free(cp);
	} else {
		cp->c_next = free_callouts;
		free_callouts = cp;
		nfree_callouts++;
	}
}

/*
 * Schedule a callout.
 *
 * (*fn)(closure) will be called at clocktime() + secs
 */
int
timeout(unsigned int secs, void (*fn)(void *), void *closure)
{
	callout *cp, *cp2;
	time_t t = clocktime() + secs;

	/*
	 * Allocate and fill in a new callout structure
	 */
	callout *cpnew = alloc_callout();
	cpnew->c_closure = closure;
	cpnew->c_fn = fn;
	cpnew->c_time = t;
	cpnew->c_id = CID_ALLOC();

	if (t < next_softclock)
		next_softclock = t;

	/*
	 * Find the correct place in the list
	 */
	for (cp = &callouts; (cp2 = cp->c_next); cp = cp2)
		if (cp2->c_time >= t)
			break;

	/*
	 * And link it in
	 */
	cp->c_next = cpnew;
	cpnew->c_next = cp2;

	/*
	 * Return callout identifier
	 */
	return cpnew->c_id;
}

/*
 * De-schedule a callout
 */
void
untimeout(int id)
{
	callout *cp, *cp2;
	for (cp = &callouts; (cp2 = cp->c_next); cp = cp2) {
		if (cp2->c_id == id) {
			cp->c_next = cp2->c_next;
			free_callout(cp2);
			break;
		}
	}
}

/*
 * Reschedule after clock changed
 */
void
reschedule_timeouts(time_t now, time_t then)
{
	callout *cp;

	for (cp = callouts.c_next; cp; cp = cp->c_next) {
		if (cp->c_time >= now && cp->c_time <= then) {
			plog(XLOG_WARNING, "job %d rescheduled to run immediately", cp->c_id);
#ifdef DEBUG
			dlog("rescheduling job %d back %d seconds",
				cp->c_id, cp->c_time - now);
#endif
			next_softclock = cp->c_time = now;
		}
	}
}

/*
 * Clock handler
 */
int
softclock(void)
{
	time_t now;
	callout *cp;

	do {
		if (task_notify_todo)
			do_task_notify();

		now = clocktime();

		/*
		 * While there are more callouts waiting...
		 */
		while ((cp = callouts.c_next) && cp->c_time <= now) {
			/*
			 * Extract first from list, save fn & closure and
			 * unlink callout from list and free.
			 * Finally call function.
			 *
			 * The free is done first because
			 * it is quite common that the
			 * function will call timeout()
			 * and try to allocate a callout
			 */
			void (*fn)(void *) = cp->c_fn;
			void *closure = cp->c_closure;

			callouts.c_next = cp->c_next;
			free_callout(cp);
#ifdef DEBUG
			/*dlog("Calling %#x(%#x)", fn, closure);*/
#endif /* DEBUG */
			(*fn)(closure);
		}

	} while (task_notify_todo);

	/*
	 * Return number of seconds to next event,
	 * or 0 if there is no event.
	 */
	if ((cp = callouts.c_next))
		return cp->c_time - now;
	return 0;
}
