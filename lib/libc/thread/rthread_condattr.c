/*	$OpenBSD: rthread_condattr.c,v 1.3 2017/09/05 02:40:54 guenther Exp $ */
/*
 * Copyright (c) 2004,2005 Ted Unangst <tedu@openbsd.org>
 * Copyright (c) 2012 Philip Guenther <guenther@openbsd.org>
 * All Rights Reserved.
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
/*
 * Condition Variable Attributes
 */

#include <errno.h>
#include <pthread.h>
#include <stdlib.h>

#include "rthread.h"

int
pthread_condattr_init(pthread_condattr_t *attrp)
{
	pthread_condattr_t attr;

	attr = calloc(1, sizeof(*attr));
	if (!attr)
		return (errno);
	attr->ca_clock = CLOCK_REALTIME;
	*attrp = attr;

	return (0);
}

int
pthread_condattr_destroy(pthread_condattr_t *attrp)
{
	free(*attrp);
	*attrp = NULL;

	return (0);
}

int
pthread_condattr_getclock(const pthread_condattr_t *attr, clockid_t *clock_id)
{
	*clock_id = (*attr)->ca_clock;
	return (0);
}

int
pthread_condattr_setclock(pthread_condattr_t *attr, clockid_t clock_id)
{
	if (clock_id != CLOCK_REALTIME && clock_id != CLOCK_MONOTONIC)
		return (EINVAL);
	(*attr)->ca_clock = clock_id;
	return (0);
}

