/*	$OpenBSD: job.c,v 1.15 2020/04/17 02:12:56 millert Exp $	*/

/* Copyright 1988,1990,1993,1994 by Paul Vixie
 * Copyright (c) 2004 by Internet Systems Consortium, Inc. ("ISC")
 * Copyright (c) 1997,2000 by Internet Software Consortium, Inc.
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

#include <sys/types.h>

#include <bitstring.h>		/* for structs.h */
#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>
#include <time.h>		/* for structs.h */

#include "macros.h"
#include "structs.h"
#include "funcs.h"

typedef	struct _job {
	SIMPLEQ_ENTRY(_job) entries;
	entry		*e;
	user		*u;
	pid_t		pid;
} job;


static SIMPLEQ_HEAD(job_queue, _job) jobs = SIMPLEQ_HEAD_INITIALIZER(jobs);

void
job_add(entry *e, user *u)
{
	job *j;

	/* if already on queue, keep going */
	SIMPLEQ_FOREACH(j, &jobs, entries) {
		if (j->e == e && j->u == u) {
			if ((j->e->flags & DONT_LOG) == 0) {
				syslog(LOG_INFO, "(%s) SKIPPING (%s)",
				    j->u->name, j->e->cmd);
			}
			return;
		}
	}

	/* build a job queue element */
	if ((j = malloc(sizeof(job))) == NULL)
		return;
	j->e = e;
	j->u = u;
	j->pid = -1;

	/* add it to the tail */
	SIMPLEQ_INSERT_TAIL(&jobs, j, entries);
}

void
job_remove(entry *e, user *u)
{
	job *j, *prev = NULL;

	SIMPLEQ_FOREACH(j, &jobs, entries) {
		if (j->e == e && j->u == u) {
			if (prev == NULL)
				SIMPLEQ_REMOVE_HEAD(&jobs, entries);
			else
				SIMPLEQ_REMOVE_AFTER(&jobs, prev, entries);
			free(j);
			break;
		}
		prev = j;
	}
}

void
job_exit(pid_t jobpid)
{
	job *j, *prev = NULL;

	/* If a singleton exited, remove and free it. */
	SIMPLEQ_FOREACH(j, &jobs, entries) {
		if (jobpid == j->pid) {
			if (prev == NULL)
				SIMPLEQ_REMOVE_HEAD(&jobs, entries);
			else
				SIMPLEQ_REMOVE_AFTER(&jobs, prev, entries);
			free(j);
			break;
		}
		prev = j;
	}
}

int
job_runqueue(void)
{
	struct job_queue singletons = SIMPLEQ_HEAD_INITIALIZER(singletons);
	job *j;
	int run = 0;

	while ((j = SIMPLEQ_FIRST(&jobs))) {
		SIMPLEQ_REMOVE_HEAD(&jobs, entries);

		/* Only start the job if it is not a running singleton. */
		if (j->pid == -1) {
			j->pid = do_command(j->e, j->u);
			run++;
		}

		/* Singleton jobs persist in the queue until they exit. */
		if (j->pid != -1)
			SIMPLEQ_INSERT_TAIL(&singletons, j, entries);
		else
			free(j);
	}
	SIMPLEQ_CONCAT(&jobs, &singletons);
	return (run);
}
