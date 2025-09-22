/* $OpenBSD: timer.c,v 1.18 2017/12/05 20:31:45 jca Exp $	 */
/* $EOM: timer.c,v 1.13 2000/02/20 19:58:42 niklas Exp $	 */

/*
 * Copyright (c) 1998, 1999 Niklas Hallqvist.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
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
 * This code was written under funding by Ericsson Radio Systems.
 */

#include <sys/queue.h>
#include <sys/time.h>

#include <stdlib.h>
#include <string.h>

#include "log.h"
#include "timer.h"

static	TAILQ_HEAD(event_list, event) events;

void
timer_init(void)
{
	TAILQ_INIT(&events);
}

void
timer_next_event(struct timespec **timeout)
{
	struct timespec  now;

	if (TAILQ_FIRST(&events)) {
		clock_gettime(CLOCK_MONOTONIC, &now);
		if (timespeccmp(&now, &TAILQ_FIRST(&events)->expiration, >=))
			timespecclear(*timeout);
		else
			timespecsub(&TAILQ_FIRST(&events)->expiration, &now,
			    *timeout);
	} else
		*timeout = 0;
}

void
timer_handle_expirations(void)
{
	struct timespec now;
	struct event   *n;

	clock_gettime(CLOCK_MONOTONIC, &now);
	for (n = TAILQ_FIRST(&events);
	    n && timespeccmp(&now, &n->expiration, >=);
	    n = TAILQ_FIRST(&events)) {
		LOG_DBG((LOG_TIMER, 10,
		    "timer_handle_expirations: event %s(%p)", n->name,
		    n->arg));
		TAILQ_REMOVE(&events, n, link);
		(*n->func)(n->arg);
		free(n);
	}
}

struct event *
timer_add_event(char *name, void (*func)(void *), void *arg,
    struct timespec *expiration)
{
	struct event   *ev = malloc(sizeof *ev);
	struct event   *n;
	struct timespec now;

	if (!ev)
		return 0;
	ev->name = name;
	ev->func = func;
	ev->arg = arg;
	clock_gettime(CLOCK_MONOTONIC, &now);
	memcpy(&ev->expiration, expiration, sizeof *expiration);
	for (n = TAILQ_FIRST(&events);
	    n && timespeccmp(expiration, &n->expiration, >=);
	    n = TAILQ_NEXT(n, link))
		;
	if (n) {
		LOG_DBG((LOG_TIMER, 10,
		    "timer_add_event: event %s(%p) added before %s(%p), "
		    "expiration in %llds", name, arg, n->name, n->arg,
		    (long long)(expiration->tv_sec - now.tv_sec)));
		TAILQ_INSERT_BEFORE(n, ev, link);
	} else {
		LOG_DBG((LOG_TIMER, 10, "timer_add_event: event %s(%p) added "
		    "last, expiration in %ds", name, arg,
		    (int)(expiration->tv_sec - now.tv_sec)));
		TAILQ_INSERT_TAIL(&events, ev, link);
	}
	return ev;
}

void
timer_remove_event(struct event *ev)
{
	LOG_DBG((LOG_TIMER, 10, "timer_remove_event: removing event %s(%p)",
	    ev->name, ev->arg));
	TAILQ_REMOVE(&events, ev, link);
	free(ev);
}

void
timer_report(void)
{
	struct event   *ev;
	struct timespec now;

	clock_gettime(CLOCK_MONOTONIC, &now);

	for (ev = TAILQ_FIRST(&events); ev; ev = TAILQ_NEXT(ev, link))
		LOG_DBG((LOG_REPORT, 0,
		    "timer_report: event %s(%p) scheduled in %d seconds",
		    (ev->name ? ev->name : "<unknown>"), ev,
		    (int) (ev->expiration.tv_sec - now.tv_sec)));
}
