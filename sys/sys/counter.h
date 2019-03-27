/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012 Gleb Smirnoff <glebius@FreeBSD.org>
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
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
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
 *
 * $FreeBSD$
 */

#ifndef __SYS_COUNTER_H__
#define __SYS_COUNTER_H__

typedef uint64_t *counter_u64_t;

#ifdef _KERNEL
#include <machine/counter.h>

counter_u64_t	counter_u64_alloc(int);
void		counter_u64_free(counter_u64_t);

void		counter_u64_zero(counter_u64_t);
uint64_t	counter_u64_fetch(counter_u64_t);

#define	COUNTER_ARRAY_ALLOC(a, n, wait)	do {			\
	for (int i = 0; i < (n); i++)				\
		(a)[i] = counter_u64_alloc(wait);		\
} while (0)

#define	COUNTER_ARRAY_FREE(a, n)	do {			\
	for (int i = 0; i < (n); i++)				\
		counter_u64_free((a)[i]);			\
} while (0)

#define	COUNTER_ARRAY_COPY(a, dstp, n)	do {			\
	for (int i = 0; i < (n); i++)				\
		((uint64_t *)(dstp))[i] = counter_u64_fetch((a)[i]);\
} while (0)

#define	COUNTER_ARRAY_ZERO(a, n)	do {			\
	for (int i = 0; i < (n); i++)				\
		counter_u64_zero((a)[i]);			\
} while (0)

/*
 * counter(9) based rate checking.
 */
struct counter_rate {
	counter_u64_t	cr_rate;	/* Events since last second */
	volatile int	cr_lock;	/* Lock to clean the struct */
	int		cr_ticks;	/* Ticks on last clean */
	int		cr_over;	/* Over limit since cr_ticks? */
};

int64_t	counter_ratecheck(struct counter_rate *, int64_t);

#endif	/* _KERNEL */
#endif	/* ! __SYS_COUNTER_H__ */
