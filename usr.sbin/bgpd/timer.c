/*	$OpenBSD: timer.c,v 1.20 2025/02/20 19:47:31 claudio Exp $ */

/*
 * Copyright (c) 2003-2007 Henning Brauer <henning@openbsd.org>
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

#include <sys/types.h>
#include <stdlib.h>

#include "bgpd.h"
#include "session.h"
#include "log.h"

struct timer *
timer_get(struct timer_head *th, enum Timer timer)
{
	struct timer *t;

	TAILQ_FOREACH(t, th, entry)
		if (t->type == timer)
			break;

	return (t);
}

struct timer *
timer_nextisdue(struct timer_head *th, monotime_t now)
{
	struct timer *t;

	t = TAILQ_FIRST(th);
	if (t != NULL && monotime_valid(t->val) &&
	    monotime_cmp(t->val, now) <= 0)
		return (t);
	return (NULL);
}

monotime_t
timer_nextduein(struct timer_head *th)
{
	struct timer *t;

	if ((t = TAILQ_FIRST(th)) != NULL && monotime_valid(t->val))
		return t->val;
	return monotime_clear();
}

int
timer_running(struct timer_head *th, enum Timer timer, monotime_t *due)
{
	struct timer	*t = timer_get(th, timer);

	if (t != NULL && monotime_valid(t->val)) {
		if (due != NULL)
			*due = t->val;
		return (1);
	}
	return (0);
}

void
timer_set(struct timer_head *th, enum Timer timer, u_int offset)
{
	struct timer	*t = timer_get(th, timer);
	struct timer	*next;
	monotime_t	 ms;

	ms = monotime_from_sec(offset);
	ms = monotime_add(ms, getmonotime());

	if (t == NULL) {	/* have to create */
		if ((t = malloc(sizeof(*t))) == NULL)
			fatal("timer_set");
		t->type = timer;
	} else {
		if (monotime_cmp(t->val, ms) == 0)
			return;
		TAILQ_REMOVE(th, t, entry);
	}

	t->val = ms;

	TAILQ_FOREACH(next, th, entry)
		if (!monotime_valid(next->val) ||
		    monotime_cmp(next->val, t->val) > 0)
			break;
	if (next != NULL)
		TAILQ_INSERT_BEFORE(next, t, entry);
	else
		TAILQ_INSERT_TAIL(th, t, entry);
}

void
timer_stop(struct timer_head *th, enum Timer timer)
{
	struct timer	*t = timer_get(th, timer);

	if (t != NULL) {
		t->val = monotime_clear();
		TAILQ_REMOVE(th, t, entry);
		TAILQ_INSERT_TAIL(th, t, entry);
	}
}

void
timer_remove(struct timer_head *th, enum Timer timer)
{
	struct timer	*t = timer_get(th, timer);

	if (t != NULL) {
		TAILQ_REMOVE(th, t, entry);
		free(t);
	}
}

void
timer_remove_all(struct timer_head *th)
{
	struct timer	*t;

	while ((t = TAILQ_FIRST(th)) != NULL) {
		TAILQ_REMOVE(th, t, entry);
		free(t);
	}
}
