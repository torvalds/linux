/*	$OpenBSD: rthread_barrier.c,v 1.5 2020/04/06 00:01:08 pirofti Exp $	*/
/*
 * Copyright (c) 2012 Paul Irofti <paul@irofti.net>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
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

#include <errno.h>
#include <stdlib.h>

#include <pthread.h>

#include "rthread.h"

int
pthread_barrier_init(pthread_barrier_t *barrier, pthread_barrierattr_t *attr,
    unsigned int count) {
	int rc = 0;
	pthread_barrier_t b = NULL;

	if (barrier == NULL)
		return (EINVAL);

	if (count == 0)
		return (EINVAL);

	if (attr != NULL) {
		if (*attr == NULL)
			return (EINVAL);

		if ((*attr)->pshared != PTHREAD_PROCESS_PRIVATE)
			return (ENOTSUP);
	}

	b = calloc(1, sizeof *b);
	if (b == NULL)
		return (ENOMEM);

	if ((rc = pthread_mutex_init(&b->mutex, NULL)))
		goto err;
	if ((rc = pthread_cond_init(&b->cond, NULL)))
		goto err;

	b->threshold = count;

	*barrier = b;

	return (0);

err:
	if (b) {
		if (b->mutex)
			pthread_mutex_destroy(&b->mutex);
		if (b->cond)
			pthread_cond_destroy(&b->cond);
		free(b);
	}

	return (rc);
}

int
pthread_barrier_destroy(pthread_barrier_t *barrier)
{
	int rc;
	pthread_barrier_t b;

	if (barrier == NULL || *barrier == NULL)
		return (EINVAL);

	if ((rc = pthread_mutex_lock(&(*barrier)->mutex)))
		return (rc);

	b = *barrier;

	if (b->out > 0 || b->in > 0) {
		pthread_mutex_unlock(&b->mutex);
		return (EBUSY);
	}

	*barrier = NULL;
	pthread_mutex_unlock(&b->mutex);
	pthread_mutex_destroy(&b->mutex);
	pthread_cond_destroy(&b->cond);
	free(b);
	return (0);
}

int
pthread_barrier_wait(pthread_barrier_t *barrier)
{
	pthread_barrier_t b;
	int rc, old_state, gen;
	int done = 0;

	if (barrier == NULL || *barrier == NULL)
		return (EINVAL);

	if ((rc = pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &old_state)))
		return (rc);

	b = *barrier;
	if ((rc = pthread_mutex_lock(&b->mutex)))
		goto cancel;

	_rthread_debug(6, "in: %d, threshold: %d\n", b->in, b->threshold);
	if (++b->in == b->threshold) {
		b->out = b->in - 1;
		b->in = 0;
		b->generation++;
		if ((rc = pthread_cond_signal(&b->cond)))
			goto err;
		done = 1;
		_rthread_debug(6, "threshold reached\n");
	} else {
		gen = b->generation;
		_rthread_debug(6, "waiting on condition\n");
		do {
			if ((rc = pthread_cond_wait(&b->cond, &b->mutex)))
				goto err;
		} while (gen == b->generation);
		b->out--; /* mark thread exit */
		if ((rc = pthread_cond_signal(&b->cond)))
			goto err;
	}

err:
	if ((rc = pthread_mutex_unlock(&b->mutex)))
		return (rc);
cancel:
	rc = pthread_setcancelstate(old_state, NULL);
	if (rc == 0 && done)
		rc = PTHREAD_BARRIER_SERIAL_THREAD;

	return (rc);
}
