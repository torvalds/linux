/*-
 * Copyright (c) 2016-2017 Mellanox Technologies, Ltd.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */
#ifndef	_ASM_ATOMIC64_H_
#define	_ASM_ATOMIC64_H_

#include <linux/compiler.h>
#include <sys/types.h>
#include <machine/atomic.h>

typedef struct {
	volatile int64_t counter;
} atomic64_t;

#define	ATOMIC64_INIT(x)	{ .counter = (x) }

/*------------------------------------------------------------------------*
 *	64-bit atomic operations
 *------------------------------------------------------------------------*/

#define	atomic64_add(i, v)		atomic64_add_return((i), (v))
#define	atomic64_sub(i, v)		atomic64_sub_return((i), (v))
#define	atomic64_inc_return(v)		atomic64_add_return(1, (v))
#define	atomic64_add_negative(i, v)	(atomic64_add_return((i), (v)) < 0)
#define	atomic64_add_and_test(i, v)	(atomic64_add_return((i), (v)) == 0)
#define	atomic64_sub_and_test(i, v)	(atomic64_sub_return((i), (v)) == 0)
#define	atomic64_dec_and_test(v)	(atomic64_sub_return(1, (v)) == 0)
#define	atomic64_inc_and_test(v)	(atomic64_add_return(1, (v)) == 0)
#define	atomic64_dec_return(v)		atomic64_sub_return(1, (v))
#define	atomic64_inc_not_zero(v)	atomic64_add_unless((v), 1, 0)

static inline int64_t
atomic64_add_return(int64_t i, atomic64_t *v)
{
	return i + atomic_fetchadd_64(&v->counter, i);
}

static inline int64_t
atomic64_sub_return(int64_t i, atomic64_t *v)
{
	return atomic_fetchadd_64(&v->counter, -i) - i;
}

static inline void
atomic64_set(atomic64_t *v, int64_t i)
{
	atomic_store_rel_64(&v->counter, i);
}

static inline int64_t
atomic64_read(atomic64_t *v)
{
	return READ_ONCE(v->counter);
}

static inline int64_t
atomic64_inc(atomic64_t *v)
{
	return atomic_fetchadd_64(&v->counter, 1) + 1;
}

static inline int64_t
atomic64_dec(atomic64_t *v)
{
	return atomic_fetchadd_64(&v->counter, -1) - 1;
}

static inline int64_t
atomic64_add_unless(atomic64_t *v, int64_t a, int64_t u)
{
	int64_t c = atomic64_read(v);

	for (;;) {
		if (unlikely(c == u))
			break;
		if (likely(atomic_fcmpset_64(&v->counter, &c, c + a)))
			break;
	}
	return (c != u);
}

static inline int64_t
atomic64_xchg(atomic64_t *v, int64_t i)
{
#if !((defined(__mips__) && !(defined(__mips_n32) || defined(__mips_n64))) || \
    (defined(__powerpc__) && !defined(__powerpc64__)))
	return (atomic_swap_64(&v->counter, i));
#else
	int64_t ret = atomic64_read(v);

	while (!atomic_fcmpset_64(&v->counter, &ret, i))
		;
	return (ret);
#endif
}

static inline int64_t
atomic64_cmpxchg(atomic64_t *v, int64_t old, int64_t new)
{
	int64_t ret = old;

	for (;;) {
		if (atomic_fcmpset_64(&v->counter, &ret, new))
			break;
		if (ret != old)
			break;
	}
	return (ret);
}

#endif					/* _ASM_ATOMIC64_H_ */
