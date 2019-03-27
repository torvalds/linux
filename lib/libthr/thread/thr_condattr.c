/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1997 John Birrell <jb@cimlogic.com.au>.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY JOHN BIRRELL AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "namespace.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include "un-namespace.h"

#include "thr_private.h"

__weak_reference(_pthread_condattr_init, pthread_condattr_init);
__weak_reference(_pthread_condattr_destroy, pthread_condattr_destroy);
__weak_reference(_pthread_condattr_getclock, pthread_condattr_getclock);
__weak_reference(_pthread_condattr_setclock, pthread_condattr_setclock);
__weak_reference(_pthread_condattr_getpshared, pthread_condattr_getpshared);
__weak_reference(_pthread_condattr_setpshared, pthread_condattr_setpshared);

int
_pthread_condattr_init(pthread_condattr_t *attr)
{
	pthread_condattr_t pattr;
	int ret;

	if ((pattr = (pthread_condattr_t)
	    malloc(sizeof(struct pthread_cond_attr))) == NULL) {
		ret = ENOMEM;
	} else {
		memcpy(pattr, &_pthread_condattr_default,
		    sizeof(struct pthread_cond_attr));
		*attr = pattr;
		ret = 0;
	}
	return (ret);
}

int
_pthread_condattr_destroy(pthread_condattr_t *attr)
{
	int	ret;

	if (attr == NULL || *attr == NULL) {
		ret = EINVAL;
	} else {
		free(*attr);
		*attr = NULL;
		ret = 0;
	}
	return(ret);
}

int
_pthread_condattr_getclock(const pthread_condattr_t * __restrict attr,
    clockid_t * __restrict clock_id)
{
	if (attr == NULL || *attr == NULL)
		return (EINVAL);
	*clock_id = (*attr)->c_clockid;
	return (0);
}

int
_pthread_condattr_setclock(pthread_condattr_t *attr, clockid_t clock_id)
{
	if (attr == NULL || *attr == NULL)
		return (EINVAL);
	if (clock_id != CLOCK_REALTIME &&
	    clock_id != CLOCK_VIRTUAL &&
	    clock_id != CLOCK_PROF &&
	    clock_id != CLOCK_MONOTONIC) {
		return  (EINVAL);
	}
	(*attr)->c_clockid = clock_id;
	return (0);
}

int
_pthread_condattr_getpshared(const pthread_condattr_t * __restrict attr,
    int * __restrict pshared)
{

	if (attr == NULL || *attr == NULL)
		return (EINVAL);
	*pshared = (*attr)->c_pshared;
	return (0);
}

int
_pthread_condattr_setpshared(pthread_condattr_t *attr, int pshared)
{

	if (attr == NULL || *attr == NULL ||
	    (pshared != PTHREAD_PROCESS_PRIVATE &&
	    pshared != PTHREAD_PROCESS_SHARED))
		return (EINVAL);
	(*attr)->c_pshared = pshared;
	return (0);
}
