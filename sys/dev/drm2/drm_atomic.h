/**
 * \file drm_atomic.h
 * Atomic operations used in the DRM which may or may not be provided by the OS.
 * 
 * \author Eric Anholt <anholt@FreeBSD.org>
 */

/*-
 * Copyright 2004 Eric Anholt
 * Copyright 2013 Jung-uk Kim <jkim@FreeBSD.org>
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * VA LINUX SYSTEMS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

typedef u_int		atomic_t;
typedef uint64_t	atomic64_t;

#define	NB_BITS_PER_LONG		(sizeof(long) * NBBY)
#define	BITS_TO_LONGS(x)		howmany(x, NB_BITS_PER_LONG)

#define	atomic_read(p)			atomic_load_acq_int(p)
#define	atomic_set(p, v)		atomic_store_rel_int(p, v)

#define	atomic64_read(p)		atomic_load_acq_64(p)
#define	atomic64_set(p, v)		atomic_store_rel_64(p, v)

#define	atomic_add(v, p)		atomic_add_int(p, v)
#define	atomic_sub(v, p)		atomic_subtract_int(p, v)
#define	atomic_inc(p)			atomic_add(1, p)
#define	atomic_dec(p)			atomic_sub(1, p)

#define	atomic_add_return(v, p)		(atomic_fetchadd_int(p, v) + (v))
#define	atomic_sub_return(v, p)		(atomic_fetchadd_int(p, -(v)) - (v))
#define	atomic_inc_return(p)		atomic_add_return(1, p)
#define	atomic_dec_return(p)		atomic_sub_return(1, p)

#define	atomic_add_and_test(v, p)	(atomic_add_return(v, p) == 0)
#define	atomic_sub_and_test(v, p)	(atomic_sub_return(v, p) == 0)
#define	atomic_inc_and_test(p)		(atomic_inc_return(p) == 0)
#define	atomic_dec_and_test(p)		(atomic_dec_return(p) == 0)

#define	atomic_xchg(p, v)		atomic_swap_int(p, v)
#define	atomic64_xchg(p, v)		atomic_swap_64(p, v)

#define	__bit_word(b)			((b) / NB_BITS_PER_LONG)
#define	__bit_mask(b)			(1UL << (b) % NB_BITS_PER_LONG)
#define	__bit_addr(p, b)		((volatile u_long *)(p) + __bit_word(b))

#define	clear_bit(b, p) \
    atomic_clear_long(__bit_addr(p, b), __bit_mask(b))
#define	set_bit(b, p) \
    atomic_set_long(__bit_addr(p, b), __bit_mask(b))
#define	test_bit(b, p) \
    ((*__bit_addr(p, b) & __bit_mask(b)) != 0)
#define	test_and_set_bit(b, p) \
    (atomic_xchg((p), 1) != b)
#define	cmpxchg(ptr, old, new) \
    (atomic_cmpset_int((volatile u_int *)(ptr),(old),(new)) ? (old) : (0))

#define	atomic_inc_not_zero(p)		atomic_inc(p)
#define	atomic_clear_mask(b, p)		atomic_clear_int((p), (b))

static __inline u_long
find_first_zero_bit(const u_long *p, u_long max)
{
	u_long i, n;

	KASSERT(max % NB_BITS_PER_LONG == 0, ("invalid bitmap size %lu", max));
	for (i = 0; i < max / NB_BITS_PER_LONG; i++) {
		n = ~p[i];
		if (n != 0)
			return (i * NB_BITS_PER_LONG + ffsl(n) - 1);
	}
	return (max);
}
