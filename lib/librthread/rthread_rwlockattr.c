/*	$OpenBSD: rthread_rwlockattr.c,v 1.2 2012/02/15 04:58:42 guenther Exp $ */
/*
 * Copyright (c) 2004,2005 Ted Unangst <tedu@openbsd.org>
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
 * rwlock attributes
 */


#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

#include <pthread.h>

#include "rthread.h"

int
pthread_rwlockattr_init(pthread_rwlockattr_t *attrp)
{
	pthread_rwlockattr_t attr;

	attr = calloc(1, sizeof(*attr));
	if (!attr)
		return (errno);
	attr->pshared = PTHREAD_PROCESS_PRIVATE;
	*attrp = attr;

	return (0);
}

int
pthread_rwlockattr_destroy(pthread_rwlockattr_t *attrp)
{
	free(*attrp);
	*attrp = NULL;

	return (0);
}

int
pthread_rwlockattr_getpshared(const pthread_rwlockattr_t *attrp, int *pshared)
{
	*pshared = (*attrp)->pshared;

	return (0);  
}

int
pthread_rwlockattr_setpshared(pthread_rwlockattr_t *attrp, int pshared)
{
	/* only PTHREAD_PROCESS_PRIVATE is supported */
	if (pshared != PTHREAD_PROCESS_PRIVATE)
		return (EINVAL); 

	(*attrp)->pshared = pshared;

	return (0);
}
