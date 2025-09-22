/*	$OpenBSD: rthread_barrier_attr.c,v 1.3 2020/04/06 00:01:08 pirofti Exp $	*/
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
pthread_barrierattr_init(pthread_barrierattr_t *attr)
{
	if (attr == NULL)
		return (EINVAL);

	*attr = calloc(1, sizeof **attr);
	if (*attr == NULL)
		return (ENOMEM);

	(*attr)->pshared = PTHREAD_PROCESS_PRIVATE;

	return (0);
}

int
pthread_barrierattr_destroy(pthread_barrierattr_t *attr)
{
	if (attr == NULL || *attr == NULL)
		return (EINVAL);

	free(*attr);
	return (0);
}

int
pthread_barrierattr_getpshared(pthread_barrierattr_t *attr, int *pshared)
{
	if (attr == NULL || *attr == NULL)
		return (EINVAL);

	*pshared = (*attr)->pshared;

	return (0);
}

int
pthread_barrierattr_setpshared(pthread_barrierattr_t *attr, int pshared)
{
	if (attr == NULL || *attr == NULL)
		return (EINVAL);

	if (pshared != PTHREAD_PROCESS_PRIVATE)
		return (ENOTSUP);

	(*attr)->pshared = pshared;

	return (0);
}
