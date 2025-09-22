/*	$OpenBSD: rthread_mutexattr.c,v 1.3 2012/04/13 13:50:37 kurt Exp $ */
/*
 * Copyright (c) 2004,2005 Ted Unangst <tedu@openbsd.org>
 * Copyright (c) 2011 Philip Guenther <guenther@openbsd.org>
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
 * Mutex attributes
 */


#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

#include <pthread.h>
#include <pthread_np.h>

#include "rthread.h"

int
pthread_mutexattr_init(pthread_mutexattr_t *attrp)
{
	pthread_mutexattr_t attr;

	attr = calloc(1, sizeof(*attr));
	if (!attr)
		return (errno);
	attr->ma_type = PTHREAD_MUTEX_DEFAULT;
	*attrp = attr;

	return (0);
}

int
pthread_mutexattr_destroy(pthread_mutexattr_t *attrp)
{
	free(*attrp);
	*attrp = NULL;

	return (0);
}

int
pthread_mutexattr_settype(pthread_mutexattr_t *attrp, int type)
{
	if (type < PTHREAD_MUTEX_ERRORCHECK || type >= PTHREAD_MUTEX_TYPE_MAX)
		return (EINVAL);
	(*attrp)->ma_type = type;
	return (0);
}

int
pthread_mutexattr_gettype(pthread_mutexattr_t *attrp, int *type)
{
	*type = (*attrp)->ma_type;
	return (0);
}

int
pthread_mutexattr_setprotocol(pthread_mutexattr_t *attrp, int protocol)
{
	if (protocol < PTHREAD_PRIO_NONE || protocol > PTHREAD_PRIO_PROTECT)
		return (EINVAL);
	(*attrp)->ma_protocol = protocol;
	return (0);
}

int
pthread_mutexattr_getprotocol(pthread_mutexattr_t *attrp, int *protocol)
{
	*protocol = (*attrp)->ma_protocol;
	return (0);
}

int
pthread_mutexattr_setprioceiling(pthread_mutexattr_t *attrp, int prioceiling)
{
	if (prioceiling < PTHREAD_MIN_PRIORITY || 
	    prioceiling > PTHREAD_MAX_PRIORITY)
		return (EINVAL);
	(*attrp)->ma_prioceiling = prioceiling;
	return (0);
}

int
pthread_mutexattr_getprioceiling(pthread_mutexattr_t *attrp, int *prioceiling)
{
	*prioceiling = (*attrp)->ma_prioceiling;
	return (0);
}

int
pthread_mutexattr_getkind_np(pthread_mutexattr_t attrp)
{
	int	ret;

	if (attrp == NULL)
		ret = EINVAL;
	else
		ret = attrp->ma_type;

        return(ret);
}

int
pthread_mutexattr_setkind_np(pthread_mutexattr_t *attrp, int kind)
{
	int	ret;

	if (attrp == NULL || *attrp == NULL ||
	    kind < PTHREAD_MUTEX_ERRORCHECK || kind >= PTHREAD_MUTEX_TYPE_MAX)
		ret = EINVAL;
	else {
		(*attrp)->ma_type = kind;
		ret = 0;
	}

	return (ret);
}
