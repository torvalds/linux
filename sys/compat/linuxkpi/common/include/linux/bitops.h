/*-
 * Copyright (c) 2010 Isilon Systems, Inc.
 * Copyright (c) 2010 iX Systems, Inc.
 * Copyright (c) 2010 Panasas, Inc.
 * Copyright (c) 2013-2017 Mellanox Technologies, Ltd.
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
#ifndef	_LINUX_BITOPS_H_
#define	_LINUX_BITOPS_H_

#include <sys/param.h>
#include <sys/types.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/libkern.h>

#define	BIT(nr)			(1UL << (nr))
#define	BIT_ULL(nr)		(1ULL << (nr))
#ifdef __LP64__
#define	BITS_PER_LONG		64
#else
#define	BITS_PER_LONG		32
#endif

#define	BITS_PER_LONG_LONG	64

#define	BITMAP_FIRST_WORD_MASK(start)	(~0UL << ((start) % BITS_PER_LONG))
#define	BITMAP_LAST_WORD_MASK(n)	(~0UL >> (BITS_PER_LONG - (n)))
#define	BITS_TO_LONGS(n)	howmany((n), BITS_PER_LONG)
#define	BIT_MASK(nr)		(1UL << ((nr) & (BITS_PER_LONG - 1)))
#define	BIT_WORD(nr)		((nr) / BITS_PER_LONG)
#define	GENMASK(h, l)		(((~0UL) >> (BITS_PER_LONG - (h) - 1)) & ((~0UL) << (l)))
#define	GENMASK_ULL(h, l)	(((~0ULL) >> (BITS_PER_LONG_LONG - (h) - 1)) & ((~0ULL) << (l)))
#define	BITS_PER_BYTE		8
#define	BITS_PER_TYPE(t)	(sizeof(t) * BITS_PER_BYTE)

#define	hweight8(x)	bitcount((uint8_t)(x))
#define	hweight16(x)	bitcount16(x)
#define	hweight32(x)	bitcount32(x)
#define	hweight64(x)	bitcount64(x)
#define	hweight_long(x)	bitcountl(x)

static inline int
__ffs(int mask)
{
	return (ffs(mask) - 1);
}

static inline int
__fls(int mask)
{
	return (fls(mask) - 1);
}

static inline int
__ffsl(long mask)
{
	return (ffsl(mask) - 1);
}

static inline int
__flsl(long mask)
{
	return (flsl(mask) - 1);
}

static inline int
fls64(uint64_t mask)
{
	return (flsll(mask));
}

static inline uint32_t
ror32(uint32_t word, unsigned int shift)
{
	return ((word >> shift) | (word << (32 - shift)));
}

#define	ffz(mask)	__ffs(~(mask))

static inline int get_count_order(unsigned int count)
{
        int order;

        order = fls(count) - 1;
        if (count & (count - 1))
                order++;
        return order;
}

static inline unsigned long
find_first_bit(const unsigned long *addr, unsigned long size)
{
	long mask;
	int bit;

	for (bit = 0; size >= BITS_PER_LONG;
	    size -= BITS_PER_LONG, bit += BITS_PER_LONG, addr++) {
		if (*addr == 0)
			continue;
		return (bit + __ffsl(*addr));
	}
	if (size) {
		mask = (*addr) & BITMAP_LAST_WORD_MASK(size);
		if (mask)
			bit += __ffsl(mask);
		else
			bit += size;
	}
	return (bit);
}

static inline unsigned long
find_first_zero_bit(const unsigned long *addr, unsigned long size)
{
	long mask;
	int bit;

	for (bit = 0; size >= BITS_PER_LONG;
	    size -= BITS_PER_LONG, bit += BITS_PER_LONG, addr++) {
		if (~(*addr) == 0)
			continue;
		return (bit + __ffsl(~(*addr)));
	}
	if (size) {
		mask = ~(*addr) & BITMAP_LAST_WORD_MASK(size);
		if (mask)
			bit += __ffsl(mask);
		else
			bit += size;
	}
	return (bit);
}

static inline unsigned long
find_last_bit(const unsigned long *addr, unsigned long size)
{
	long mask;
	int offs;
	int bit;
	int pos;

	pos = size / BITS_PER_LONG;
	offs = size % BITS_PER_LONG;
	bit = BITS_PER_LONG * pos;
	addr += pos;
	if (offs) {
		mask = (*addr) & BITMAP_LAST_WORD_MASK(offs);
		if (mask)
			return (bit + __flsl(mask));
	}
	while (pos--) {
		addr--;
		bit -= BITS_PER_LONG;
		if (*addr)
			return (bit + __flsl(*addr));
	}
	return (size);
}

static inline unsigned long
find_next_bit(const unsigned long *addr, unsigned long size, unsigned long offset)
{
	long mask;
	int offs;
	int bit;
	int pos;

	if (offset >= size)
		return (size);
	pos = offset / BITS_PER_LONG;
	offs = offset % BITS_PER_LONG;
	bit = BITS_PER_LONG * pos;
	addr += pos;
	if (offs) {
		mask = (*addr) & ~BITMAP_LAST_WORD_MASK(offs);
		if (mask)
			return (bit + __ffsl(mask));
		if (size - bit <= BITS_PER_LONG)
			return (size);
		bit += BITS_PER_LONG;
		addr++;
	}
	for (size -= bit; size >= BITS_PER_LONG;
	    size -= BITS_PER_LONG, bit += BITS_PER_LONG, addr++) {
		if (*addr == 0)
			continue;
		return (bit + __ffsl(*addr));
	}
	if (size) {
		mask = (*addr) & BITMAP_LAST_WORD_MASK(size);
		if (mask)
			bit += __ffsl(mask);
		else
			bit += size;
	}
	return (bit);
}

static inline unsigned long
find_next_zero_bit(const unsigned long *addr, unsigned long size,
    unsigned long offset)
{
	long mask;
	int offs;
	int bit;
	int pos;

	if (offset >= size)
		return (size);
	pos = offset / BITS_PER_LONG;
	offs = offset % BITS_PER_LONG;
	bit = BITS_PER_LONG * pos;
	addr += pos;
	if (offs) {
		mask = ~(*addr) & ~BITMAP_LAST_WORD_MASK(offs);
		if (mask)
			return (bit + __ffsl(mask));
		if (size - bit <= BITS_PER_LONG)
			return (size);
		bit += BITS_PER_LONG;
		addr++;
	}
	for (size -= bit; size >= BITS_PER_LONG;
	    size -= BITS_PER_LONG, bit += BITS_PER_LONG, addr++) {
		if (~(*addr) == 0)
			continue;
		return (bit + __ffsl(~(*addr)));
	}
	if (size) {
		mask = ~(*addr) & BITMAP_LAST_WORD_MASK(size);
		if (mask)
			bit += __ffsl(mask);
		else
			bit += size;
	}
	return (bit);
}

#define	__set_bit(i, a)							\
    atomic_set_long(&((volatile unsigned long *)(a))[BIT_WORD(i)], BIT_MASK(i))

#define	set_bit(i, a)							\
    atomic_set_long(&((volatile unsigned long *)(a))[BIT_WORD(i)], BIT_MASK(i))

#define	__clear_bit(i, a)						\
    atomic_clear_long(&((volatile unsigned long *)(a))[BIT_WORD(i)], BIT_MASK(i))

#define	clear_bit(i, a)							\
    atomic_clear_long(&((volatile unsigned long *)(a))[BIT_WORD(i)], BIT_MASK(i))

#define	test_bit(i, a)							\
    !!(READ_ONCE(((volatile unsigned long *)(a))[BIT_WORD(i)]) & BIT_MASK(i))

static inline int
test_and_clear_bit(long bit, volatile unsigned long *var)
{
	long val;

	var += BIT_WORD(bit);
	bit %= BITS_PER_LONG;
	bit = (1UL << bit);

	val = *var;
	while (!atomic_fcmpset_long(var, &val, val & ~bit))
		;
	return !!(val & bit);
}

static inline int
__test_and_clear_bit(long bit, volatile unsigned long *var)
{
	long val;

	var += BIT_WORD(bit);
	bit %= BITS_PER_LONG;
	bit = (1UL << bit);

	val = *var;
	*var &= ~bit;

	return !!(val & bit);
}

static inline int
test_and_set_bit(long bit, volatile unsigned long *var)
{
	long val;

	var += BIT_WORD(bit);
	bit %= BITS_PER_LONG;
	bit = (1UL << bit);

	val = *var;
	while (!atomic_fcmpset_long(var, &val, val | bit))
		;
	return !!(val & bit);
}

static inline int
__test_and_set_bit(long bit, volatile unsigned long *var)
{
	long val;

	var += BIT_WORD(bit);
	bit %= BITS_PER_LONG;
	bit = (1UL << bit);

	val = *var;
	*var |= bit;

	return !!(val & bit);
}

enum {
        REG_OP_ISFREE,
        REG_OP_ALLOC,
        REG_OP_RELEASE,
};

static inline int
linux_reg_op(unsigned long *bitmap, int pos, int order, int reg_op)
{
        int nbits_reg;
        int index;
        int offset;
        int nlongs_reg;
        int nbitsinlong;
        unsigned long mask;
        int i;
        int ret = 0;

        nbits_reg = 1 << order;
        index = pos / BITS_PER_LONG;
        offset = pos - (index * BITS_PER_LONG);
        nlongs_reg = BITS_TO_LONGS(nbits_reg);
        nbitsinlong = min(nbits_reg,  BITS_PER_LONG);

        mask = (1UL << (nbitsinlong - 1));
        mask += mask - 1;
        mask <<= offset;

        switch (reg_op) {
        case REG_OP_ISFREE:
                for (i = 0; i < nlongs_reg; i++) {
                        if (bitmap[index + i] & mask)
                                goto done;
                }
                ret = 1;
                break;

        case REG_OP_ALLOC:
                for (i = 0; i < nlongs_reg; i++)
                        bitmap[index + i] |= mask;
                break;

        case REG_OP_RELEASE:
                for (i = 0; i < nlongs_reg; i++)
                        bitmap[index + i] &= ~mask;
                break;
        }
done:
        return ret;
}

#define for_each_set_bit(bit, addr, size) \
	for ((bit) = find_first_bit((addr), (size));		\
	     (bit) < (size);					\
	     (bit) = find_next_bit((addr), (size), (bit) + 1))

#define	for_each_clear_bit(bit, addr, size) \
	for ((bit) = find_first_zero_bit((addr), (size));		\
	     (bit) < (size);						\
	     (bit) = find_next_zero_bit((addr), (size), (bit) + 1))

static inline uint64_t
sign_extend64(uint64_t value, int index)
{
	uint8_t shift = 63 - index;

	return ((int64_t)(value << shift) >> shift);
}

#endif	/* _LINUX_BITOPS_H_ */
