/*	$OpenBSD: rthread_mutex_prio.c,v 1.2 2014/06/23 00:43:15 guenther Exp $ */
/*
 * Copyright (c) 2011 Philip Guenther <guenther@openbsd.org>
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


#include <errno.h>

#include <pthread.h>
#include "rthread.h"

int
pthread_mutex_getprioceiling(pthread_mutex_t *mutexp, int *prioceiling)
{
	pthread_mutex_t mutex = *mutexp;

	if (mutex->prioceiling == -1)
		return (EINVAL);
	*prioceiling = mutex->prioceiling;

	return (0);
}

int
pthread_mutex_setprioceiling(pthread_mutex_t *mutexp, int prioceiling,
    int *old_ceiling)
{
	pthread_mutex_t mutex = *mutexp;
	int ret;

	if (mutex->prioceiling == -1 ||
	    prioceiling < PTHREAD_MIN_PRIORITY ||
	    prioceiling > PTHREAD_MAX_PRIORITY) {
		ret = EINVAL;
	} else if ((ret = pthread_mutex_lock(mutexp)) == 0) {
		*old_ceiling = mutex->prioceiling;
		mutex->prioceiling = prioceiling;
		pthread_mutex_unlock(mutexp);
	}

	return (ret);
}
