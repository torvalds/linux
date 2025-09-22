/*	$OpenBSD: timer.c,v 1.7 2018/04/10 15:58:21 cheloha Exp $	*/

/*
 * Copyright (c) 2005 Håkan Olsson.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * This code was written under funding by Multicom Security AB.
 */


#include <sys/types.h>
#include <sys/queue.h>
#include <sys/time.h>

#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "sasyncd.h"

/*
 * All events have a name, an expiration time and a function to be called
 * at this time. The queue is always sorted so the next event to happen is
 * first in the queue.
 */
struct event {
	TAILQ_ENTRY (event) next;
	struct timespec	 expire;
	char		*name;
	void		(*fun) (void *);
	void		*arg;
};

static TAILQ_HEAD (event_head, event) events;

/* Initialize timer event queue. */
void
timer_init(void)
{
	TAILQ_INIT(&events);
}

/*
 * Return the number of seconds until the next event happens. Used for
 * the select() call in the main loop.
 */
void
timer_next_event(struct timespec *ts)
{
	struct timespec	 now;
	struct event	*e = TAILQ_FIRST(&events);

	if (e) {
		clock_gettime(CLOCK_MONOTONIC, &now);
		if (timespeccmp(&now, &e->expire, >=))
			timespecclear(ts);
		else
			timespecsub(&e->expire, &now, ts);
	} else {
		ts->tv_sec = 60;	/* "Best guess". */
		ts->tv_nsec = 0;
	}
}

/*
 * Whenever select() times out, we have an event that should happen and this
 * routine gets called. Handle and remove all pending events.
 */
void
timer_run(void)
{
	struct timespec	 now;
	struct event	*e;

	clock_gettime(CLOCK_MONOTONIC, &now);
	for (e = TAILQ_FIRST(&events); e && timespeccmp(&now, &e->expire, >=);
	     e = TAILQ_FIRST(&events)) {
		TAILQ_REMOVE(&events, e, next);
		log_msg(2, "timer_run: event \"%s\"",
		    e->name ? e->name : "<unknown>");
		(*e->fun)(e->arg);
		if (e->name)
			free(e->name);
		free(e);
	}
}

/* Add a new event. */
int
timer_add(char *name, u_int32_t when, void (*function)(void *), void *arg)
{
	struct timespec	 now, tmp;
	struct event	*e, *new;

	new = calloc(1, sizeof *new);
	if (!new) {
		log_err("timer_add: calloc (1, %zu) failed", sizeof *new);
		return -1;
	}

	new->name = strdup(name); /* We handle failures here. */
	new->fun = function;
	new->arg = arg;

	tmp.tv_sec = when;
	tmp.tv_nsec = 0;
	clock_gettime(CLOCK_MONOTONIC, &now);
	timespecadd(&now, &tmp, &new->expire);

	log_msg(2, "timer_add: new event \"%s\" (expiring in %us)",
	    name ? name : "<unknown>", when);

	/* Insert the new event in the queue so it's always sorted. */
	for (e = TAILQ_FIRST(&events); e; e = TAILQ_NEXT(e, next)) {
		if (timespeccmp(&new->expire, &e->expire, >=))
			continue;
		TAILQ_INSERT_BEFORE(e, new, next);
		return 0;
	}
	TAILQ_INSERT_TAIL(&events, new, next);
	return 0;
}
