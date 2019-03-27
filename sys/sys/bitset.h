/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008, Jeffrey Roberson <jeff@freebsd.org>
 * All rights reserved.
 *
 * Copyright (c) 2008 Nokia Corporation
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

#ifndef _SYS_BITSET_H_
#define	_SYS_BITSET_H_

#define	__bitset_mask(_s, n)						\
	(1L << ((__bitset_words((_s)) == 1) ?				\
	    (__size_t)(n) : ((n) % _BITSET_BITS)))

#define	__bitset_word(_s, n)						\
	((__bitset_words((_s)) == 1) ? 0 : ((n) / _BITSET_BITS))

#define	BIT_CLR(_s, n, p)						\
	((p)->__bits[__bitset_word(_s, n)] &= ~__bitset_mask((_s), (n)))

#define	BIT_COPY(_s, f, t)	(void)(*(t) = *(f))

#define	BIT_ISSET(_s, n, p)						\
	((((p)->__bits[__bitset_word(_s, n)] & __bitset_mask((_s), (n))) != 0))

#define	BIT_SET(_s, n, p)						\
	((p)->__bits[__bitset_word(_s, n)] |= __bitset_mask((_s), (n)))

#define	BIT_ZERO(_s, p) do {						\
	__size_t __i;							\
	for (__i = 0; __i < __bitset_words((_s)); __i++)		\
		(p)->__bits[__i] = 0L;					\
} while (0)

#define	BIT_FILL(_s, p) do {						\
	__size_t __i;							\
	for (__i = 0; __i < __bitset_words((_s)); __i++)		\
		(p)->__bits[__i] = -1L;					\
} while (0)

#define	BIT_SETOF(_s, n, p) do {					\
	BIT_ZERO(_s, p);						\
	(p)->__bits[__bitset_word(_s, n)] = __bitset_mask((_s), (n));	\
} while (0)

/* Is p empty. */
#define	BIT_EMPTY(_s, p) __extension__ ({				\
	__size_t __i;							\
	for (__i = 0; __i < __bitset_words((_s)); __i++)		\
		if ((p)->__bits[__i])					\
			break;						\
	__i == __bitset_words((_s));					\
})

/* Is p full set. */
#define	BIT_ISFULLSET(_s, p) __extension__ ({				\
	__size_t __i;							\
	for (__i = 0; __i < __bitset_words((_s)); __i++)		\
		if ((p)->__bits[__i] != (long)-1)			\
			break;						\
	__i == __bitset_words((_s));					\
})

/* Is c a subset of p. */
#define	BIT_SUBSET(_s, p, c) __extension__ ({				\
	__size_t __i;							\
	for (__i = 0; __i < __bitset_words((_s)); __i++)		\
		if (((c)->__bits[__i] &					\
		    (p)->__bits[__i]) !=				\
		    (c)->__bits[__i])					\
			break;						\
	__i == __bitset_words((_s));					\
})

/* Are there any common bits between b & c? */
#define	BIT_OVERLAP(_s, p, c) __extension__ ({				\
	__size_t __i;							\
	for (__i = 0; __i < __bitset_words((_s)); __i++)		\
		if (((c)->__bits[__i] &					\
		    (p)->__bits[__i]) != 0)				\
			break;						\
	__i != __bitset_words((_s));					\
})

/* Compare two sets, returns 0 if equal 1 otherwise. */
#define	BIT_CMP(_s, p, c) __extension__ ({				\
	__size_t __i;							\
	for (__i = 0; __i < __bitset_words((_s)); __i++)		\
		if (((c)->__bits[__i] !=				\
		    (p)->__bits[__i]))					\
			break;						\
	__i != __bitset_words((_s));					\
})

#define	BIT_OR(_s, d, s) do {						\
	__size_t __i;							\
	for (__i = 0; __i < __bitset_words((_s)); __i++)		\
		(d)->__bits[__i] |= (s)->__bits[__i];			\
} while (0)

#define	BIT_OR2(_s, d, s1, s2) do {					\
	__size_t __i;							\
	for (__i = 0; __i < __bitset_words((_s)); __i++)		\
		(d)->__bits[__i] = (s1)->__bits[__i] | (s2)->__bits[__i];\
} while (0)

#define	BIT_AND(_s, d, s) do {						\
	__size_t __i;							\
	for (__i = 0; __i < __bitset_words((_s)); __i++)		\
		(d)->__bits[__i] &= (s)->__bits[__i];			\
} while (0)

#define	BIT_AND2(_s, d, s1, s2) do {					\
	__size_t __i;							\
	for (__i = 0; __i < __bitset_words((_s)); __i++)		\
		(d)->__bits[__i] = (s1)->__bits[__i] & (s2)->__bits[__i];\
} while (0)

#define	BIT_NAND(_s, d, s) do {						\
	__size_t __i;							\
	for (__i = 0; __i < __bitset_words((_s)); __i++)		\
		(d)->__bits[__i] &= ~(s)->__bits[__i];			\
} while (0)

#define	BIT_NAND2(_s, d, s1, s2) do {					\
	__size_t __i;							\
	for (__i = 0; __i < __bitset_words((_s)); __i++)		\
		(d)->__bits[__i] = (s1)->__bits[__i] & ~(s2)->__bits[__i];\
} while (0)

#define	BIT_XOR(_s, d, s) do {						\
	__size_t __i;							\
	for (__i = 0; __i < __bitset_words((_s)); __i++)		\
		(d)->__bits[__i] ^= (s)->__bits[__i];			\
} while (0)

#define	BIT_XOR2(_s, d, s1, s2) do {					\
	__size_t __i;							\
	for (__i = 0; __i < __bitset_words((_s)); __i++)		\
		(d)->__bits[__i] = (s1)->__bits[__i] ^ (s2)->__bits[__i];\
} while (0)

#define	BIT_CLR_ATOMIC(_s, n, p)					\
	atomic_clear_long(&(p)->__bits[__bitset_word(_s, n)],		\
	    __bitset_mask((_s), n))

#define	BIT_SET_ATOMIC(_s, n, p)					\
	atomic_set_long(&(p)->__bits[__bitset_word(_s, n)],		\
	    __bitset_mask((_s), n))

#define	BIT_SET_ATOMIC_ACQ(_s, n, p)					\
	atomic_set_acq_long(&(p)->__bits[__bitset_word(_s, n)],		\
	    __bitset_mask((_s), n))

/* Convenience functions catering special cases. */
#define	BIT_AND_ATOMIC(_s, d, s) do {					\
	__size_t __i;							\
	for (__i = 0; __i < __bitset_words((_s)); __i++)		\
		atomic_clear_long(&(d)->__bits[__i],			\
		    ~(s)->__bits[__i]);					\
} while (0)

#define	BIT_OR_ATOMIC(_s, d, s) do {					\
	__size_t __i;							\
	for (__i = 0; __i < __bitset_words((_s)); __i++)		\
		atomic_set_long(&(d)->__bits[__i],			\
		    (s)->__bits[__i]);					\
} while (0)

#define	BIT_COPY_STORE_REL(_s, f, t) do {				\
	__size_t __i;							\
	for (__i = 0; __i < __bitset_words((_s)); __i++)		\
		atomic_store_rel_long(&(t)->__bits[__i],		\
		    (f)->__bits[__i]);					\
} while (0)

#define	BIT_FFS(_s, p) __extension__ ({					\
	__size_t __i;							\
	int __bit;							\
									\
	__bit = 0;							\
	for (__i = 0; __i < __bitset_words((_s)); __i++) {		\
		if ((p)->__bits[__i] != 0) {				\
			__bit = ffsl((p)->__bits[__i]);			\
			__bit += __i * _BITSET_BITS;			\
			break;						\
		}							\
	}								\
	__bit;								\
})

#define	BIT_FLS(_s, p) __extension__ ({					\
	__size_t __i;							\
	int __bit;							\
									\
	__bit = 0;							\
	for (__i = __bitset_words((_s)); __i > 0; __i--) {		\
		if ((p)->__bits[__i - 1] != 0) {			\
			__bit = flsl((p)->__bits[__i - 1]);		\
			__bit += (__i - 1) * _BITSET_BITS;		\
			break;						\
		}							\
	}								\
	__bit;								\
})

#define	BIT_COUNT(_s, p) __extension__ ({				\
	__size_t __i;							\
	int __count;							\
									\
	__count = 0;							\
	for (__i = 0; __i < __bitset_words((_s)); __i++)		\
		__count += __bitcountl((p)->__bits[__i]);		\
	__count;							\
})

#define	BITSET_T_INITIALIZER(x)						\
	{ .__bits = { x } }

#define	BITSET_FSET(n)							\
	[ 0 ... ((n) - 1) ] = (-1L)

/*
 * Dynamically allocate a bitset.
 */
#define BITSET_ALLOC(_s, mt, mf)					\
	malloc(__bitset_words(_s) * sizeof(long), mt, (mf))

#endif /* !_SYS_BITSET_H_ */
