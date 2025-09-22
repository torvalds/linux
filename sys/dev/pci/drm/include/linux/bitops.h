/*	$OpenBSD: bitops.h,v 1.7 2025/03/06 02:50:20 jsg Exp $	*/
/*
 * Copyright (c) 2013, 2014, 2015 Mark Kettenis
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

#ifndef _LINUX_BITOPS_H
#define _LINUX_BITOPS_H

#include <sys/types.h>
#include <sys/param.h>
#include <lib/libkern/libkern.h>

#include <asm/bitsperlong.h>
#include <linux/atomic.h>

#define BIT(x)		(1UL << (x))
#define BIT_ULL(x)	(1ULL << (x))
#define BIT_MASK(x)	(1UL << ((x) % BITS_PER_LONG))
#define BITS_PER_BYTE	8

#define GENMASK(h, l)		(((~0UL) >> (BITS_PER_LONG - (h) - 1)) & ((~0UL) << (l)))
#define GENMASK_ULL(h, l)	(((~0ULL) >> (BITS_PER_LONG_LONG - (h) - 1)) & ((~0ULL) << (l)))

#define BITS_PER_TYPE(x)	(8 * sizeof(x))
#define BITS_TO_LONGS(x)	howmany((x), 8 * sizeof(long))

/* despite the name these are really ctz */
#define __ffs(x)		__builtin_ctzl(x)
#define __ffs64(x)		__builtin_ctzll(x)
#define ffz(x)			__ffs(~(x))

static inline unsigned int
hweight8(unsigned int x)
{
	x = (x & 0x55) + ((x & 0xaa) >> 1);
	x = (x & 0x33) + ((x & 0xcc) >> 2);
	x = (x + (x >> 4)) & 0x0f;
	return (x);
}

static inline unsigned int
hweight16(unsigned int x)
{
	x = (x & 0x5555) + ((x & 0xaaaa) >> 1);
	x = (x & 0x3333) + ((x & 0xcccc) >> 2);
	x = (x + (x >> 4)) & 0x0f0f;
	x = (x + (x >> 8)) & 0x00ff;
	return (x);
}

static inline unsigned int
hweight32(unsigned int x)
{
	x = (x & 0x55555555) + ((x & 0xaaaaaaaa) >> 1);
	x = (x & 0x33333333) + ((x & 0xcccccccc) >> 2);
	x = (x + (x >> 4)) & 0x0f0f0f0f;
	x = (x + (x >> 8));
	x = (x + (x >> 16)) & 0x000000ff;
	return x;
}

static inline unsigned long
hweight64(uint64_t x)
{
	x = (x & 0x5555555555555555ULL) + ((x & 0xaaaaaaaaaaaaaaaaULL) >> 1);
	x = (x & 0x3333333333333333ULL) + ((x & 0xccccccccccccccccULL) >> 2);
	x = (x + (x >> 4)) & 0x0f0f0f0f0f0f0f0fULL;
	x = (x + (x >> 8));
	x = (x + (x >> 16));
	x = (x + (x >> 32)) & 0x000000ff;
	return x;
}

static inline unsigned long
hweight_long(unsigned long x)
{
#ifdef __LP64__
	return hweight64(x);
#else
	return hweight32(x);
#endif
}

static inline int64_t
sign_extend64(uint64_t value, int index)
{
	uint8_t shift = 63 - index;
	return (int64_t)(value << shift) >> shift;
}

static inline int
fls64(long long mask)
{
	int bit;

	if (mask == 0)
		return (0);
	for (bit = 1; mask != 1; bit++)
		mask = (unsigned long long)mask >> 1;
	return (bit);
}

static inline int
__fls(long mask)
{
	return (flsl(mask) - 1);
}

static inline uint32_t
ror32(uint32_t word, unsigned int shift)
{
	return (word >> shift) | (word << (32 - shift));
}

static inline void
__assign_bit(u_int b, volatile void *p, bool set)
{
	if (set)
		__set_bit(b, p);
	else
		__clear_bit(b, p);
}
#endif
