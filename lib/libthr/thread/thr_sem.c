/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (C) 2005 David Xu <davidxu@freebsd.org>.
 * Copyright (C) 2000 Jason Evans <jasone@freebsd.org>.
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice(s), this list of conditions and the following disclaimer as
 *    the first lines of this file unmodified other than the possible
 *    addition of one or more copyright notices.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice(s), this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDER(S) ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT HOLDER(S) BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "namespace.h"
#include <sys/types.h>
#include <sys/queue.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdlib.h>
#include <time.h>
#include <_semaphore.h>
#include "un-namespace.h"

#include "thr_private.h"

FB10_COMPAT(_sem_init_compat, sem_init);
FB10_COMPAT(_sem_destroy_compat, sem_destroy);
FB10_COMPAT(_sem_getvalue_compat, sem_getvalue);
FB10_COMPAT(_sem_trywait_compat, sem_trywait);
FB10_COMPAT(_sem_wait_compat, sem_wait);
FB10_COMPAT(_sem_timedwait_compat, sem_timedwait);
FB10_COMPAT(_sem_post_compat, sem_post);

typedef struct sem *sem_t;

extern int _libc_sem_init_compat(sem_t *sem, int pshared, unsigned int value);
extern int _libc_sem_destroy_compat(sem_t *sem);
extern int _libc_sem_getvalue_compat(sem_t * __restrict sem, int * __restrict sval);
extern int _libc_sem_trywait_compat(sem_t *sem);
extern int _libc_sem_wait_compat(sem_t *sem);
extern int _libc_sem_timedwait_compat(sem_t * __restrict sem,
    const struct timespec * __restrict abstime);
extern int _libc_sem_post_compat(sem_t *sem);

int _sem_init_compat(sem_t *sem, int pshared, unsigned int value);
int _sem_destroy_compat(sem_t *sem);
int _sem_getvalue_compat(sem_t * __restrict sem, int * __restrict sval);
int _sem_trywait_compat(sem_t *sem);
int _sem_wait_compat(sem_t *sem);
int _sem_timedwait_compat(sem_t * __restrict sem,
    const struct timespec * __restrict abstime);
int _sem_post_compat(sem_t *sem);

int
_sem_init_compat(sem_t *sem, int pshared, unsigned int value)
{
	return _libc_sem_init_compat(sem, pshared, value);
}

int
_sem_destroy_compat(sem_t *sem)
{
	return _libc_sem_destroy_compat(sem);
}

int
_sem_getvalue_compat(sem_t * __restrict sem, int * __restrict sval)
{
	return _libc_sem_getvalue_compat(sem, sval);
}

int
_sem_trywait_compat(sem_t *sem)
{
	return _libc_sem_trywait_compat(sem);
}

int
_sem_wait_compat(sem_t *sem)
{
	return _libc_sem_wait_compat(sem);
}

int
_sem_timedwait_compat(sem_t * __restrict sem,
    const struct timespec * __restrict abstime)
{
	return _libc_sem_timedwait_compat(sem, abstime);
}

int
_sem_post_compat(sem_t *sem)
{
	return _libc_sem_post_compat(sem);
}
