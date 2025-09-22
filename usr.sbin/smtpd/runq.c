/*	$OpenBSD: runq.c,v 1.5 2023/05/31 16:51:46 op Exp $	*/

/*
 * Copyright (c) 2013,2019 Eric Faurot <eric@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <stdlib.h>
#include <time.h>

#include "smtpd.h"

struct job {
	TAILQ_ENTRY(job)	 entry;
	time_t			 when;
	void			*arg;
};

struct runq {
	TAILQ_HEAD(, job)	 jobs;
	void			(*cb)(struct runq *, void *);
	struct event		 ev;
};

static void runq_timeout(int, short, void *);

static struct runq *active;

static void
runq_reset(struct runq *runq)
{
	struct timeval	 tv;
	struct job	*job;
	time_t		 now;

	job = TAILQ_FIRST(&runq->jobs);
	if (job == NULL)
		return;

	now = time(NULL);
	if (job->when <= now)
		tv.tv_sec = 0;
	else
		tv.tv_sec = job->when - now;
	tv.tv_usec = 0;
	evtimer_add(&runq->ev, &tv);
}

static void
runq_timeout(int fd, short ev, void *arg)
{
	struct runq	*runq = arg;
	struct job	*job;
	time_t		 now;

	active = runq;
	now = time(NULL);

	while((job = TAILQ_FIRST(&runq->jobs))) {
		if (job->when > now)
			break;
		TAILQ_REMOVE(&runq->jobs, job, entry);
		runq->cb(runq, job->arg);
		free(job);
	}

	active = NULL;
	runq_reset(runq);
}

int
runq_init(struct runq **runqp, void (*cb)(struct runq *, void *))
{
	struct runq	*runq;

	runq = malloc(sizeof(*runq));
	if (runq == NULL)
		return (0);

	runq->cb = cb;
	TAILQ_INIT(&runq->jobs);
	evtimer_set(&runq->ev, runq_timeout, runq);

	*runqp = runq;

	return (1);
}

int
runq_schedule(struct runq *runq, time_t delay, void *arg)
{
	time_t t;

	time(&t);
	return runq_schedule_at(runq, t + delay, arg);
}

int
runq_schedule_at(struct runq *runq, time_t when, void *arg)
{
	struct job	*job, *tmpjob;

	job = malloc(sizeof(*job));
	if (job == NULL)
		return (0);

	job->arg = arg;
	job->when = when;

	TAILQ_FOREACH(tmpjob, &runq->jobs, entry) {
		if (tmpjob->when > job->when) {
			TAILQ_INSERT_BEFORE(tmpjob, job, entry);
			goto done;
		}
	}
	TAILQ_INSERT_TAIL(&runq->jobs, job, entry);

    done:
	if (runq != active && job == TAILQ_FIRST(&runq->jobs)) {
		evtimer_del(&runq->ev);
		runq_reset(runq);
	}
	return (1);
}

int
runq_cancel(struct runq *runq, void *arg)
{
	struct job	*job, *first;

	first = TAILQ_FIRST(&runq->jobs);
	TAILQ_FOREACH(job, &runq->jobs, entry) {
		if (job->arg == arg) {
			TAILQ_REMOVE(&runq->jobs, job, entry);
			free(job);
			if (runq != active && job == first) {
				evtimer_del(&runq->ev);
				runq_reset(runq);
			}
			return (1);
		}
	}

	return (0);
}

int
runq_pending(struct runq *runq, void *arg, time_t *when)
{
	struct job	*job;

	TAILQ_FOREACH(job, &runq->jobs, entry) {
		if (job->arg == arg) {
			if (when)
				*when = job->when;
			return (1);
		}
	}

	return (0);
}
