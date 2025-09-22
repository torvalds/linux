/*	$OpenBSD: waitq.c,v 1.7 2021/06/14 17:58:16 eric Exp $	*/

/*
 * Copyright (c) 2012 Eric Faurot <eric@openbsd.org>
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

#include "smtpd.h"

struct waiter {
	TAILQ_ENTRY(waiter)	 entry;
	void			(*cb)(void *, void *, void *);
	void			*arg;
};

struct waitq {
	SPLAY_ENTRY(waitq)	 entry;
	void			*tag;
	TAILQ_HEAD(, waiter)	 waiters;
};

static int waitq_cmp(struct waitq *, struct waitq *);

SPLAY_HEAD(waitqtree, waitq);
SPLAY_PROTOTYPE(waitqtree, waitq, entry, waitq_cmp);

static struct waitqtree waitqs = SPLAY_INITIALIZER(&waitqs);

static int
waitq_cmp(struct waitq *a, struct waitq *b)
{
	if (a->tag < b->tag)
		return (-1);
	if (a->tag > b->tag)
		return (1);
	return (0);
}

SPLAY_GENERATE(waitqtree, waitq, entry, waitq_cmp);

int
waitq_wait(void *tag, void (*cb)(void *, void *, void *), void *arg)
{
	struct waitq	*wq, key;
	struct waiter	*w;

	key.tag = tag;
	wq = SPLAY_FIND(waitqtree, &waitqs, &key);
	if (wq == NULL) {
		wq = xmalloc(sizeof *wq);
		wq->tag = tag;
		TAILQ_INIT(&wq->waiters);
		SPLAY_INSERT(waitqtree, &waitqs, wq);
	}

	w = xmalloc(sizeof *w);
	w->cb = cb;
	w->arg = arg;
	TAILQ_INSERT_TAIL(&wq->waiters, w, entry);

	return (w == TAILQ_FIRST(&wq->waiters));
}

void
waitq_run(void *tag, void *result)
{
	struct waitq	*wq, key;
	struct waiter	*w;

	key.tag = tag;
	wq = SPLAY_FIND(waitqtree, &waitqs, &key);
	SPLAY_REMOVE(waitqtree, &waitqs, wq);

	while ((w = TAILQ_FIRST(&wq->waiters))) {
		TAILQ_REMOVE(&wq->waiters, w, entry);
		w->cb(tag, w->arg, result);
		free(w);
	}
	free(wq);
}
