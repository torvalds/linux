/* bitops.h: bit operations for the Fujitsu FR-V CPUs
 *
 * For an explanation of how atomic ops work in this arch, see:
 *   Documentation/frv/atomic-ops.txt
 *
 * Copyright (C) 2004 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#ifndef _ASM_BITOPS_H
#define _ASM_BITOPS_H

#include <linux/compiler.h>
#include <asm/byteorder.h>

#ifdef __KERNEL__

#ifndef _LINUX_BITOPS_H
#error only <linux/bitops.h> can be included directly
#endif

#include <asm-generic/bitops/ffz.h>

/*
 * clear_bit() doesn't provide any barrier for the compiler.
 */
#define smp_mb__before_clear_bit()	barrier()
#define smp_mb__after_clear_bit()	barrier()

#ifndef CONFIG_FRV_OUTOFLINE_ATOMIC_OPS
static inline
unsigned long atomic_test_and_ANDNOT_mask(unsigned long mask, volatile unsigned long *v)
{
	unsigned long old, tmp;

	asm volatile(
		"0:						\n"
		"	orcc		gr0,gr0,gr0,icc3	\n"	/* set ICC3.Z */
		"	ckeq		icc3,cc7		\n"
		"	ld.p		%M0,%1			\n"	/* LD.P/ORCR are atomic */
		"	orcr		cc7,cc7,cc3		\n"	/* set CC3 to true */
		"	and%I3		%1,%3,%2		\n"
		"	cst.p		%2,%M0		,cc3,#1	\n"	/* if store happens... */
		"	corcc		gr29,gr29,gr0	,cc3,#1	\n"	/* ... clear ICC3.Z */
		"	beq		icc3,#0,0b		\n"
		: "+U"(*v), "=&r"(old), "=r"(tmp)
		: "NPr"(~mask)
		: "memory", "cc7", "cc3", "icc3"
		);

	return old;
}

static inline
unsigned long atomic_test_and_OR_mask(unsigned long mask, volatile unsigned long *v)
{
	unsigned long old, tmp;

	asm volatile(
		"0:						\n"
		"	orcc		gr0,gr0,gr0,icc3	\n"	/* set ICC3.Z */
		"	ckeq		icc3,cc7		\n"
		"	ld.p		%M0,%1			\n"	/* LD.P/ORCR are atomic */
		"	orcr		cc7,cc7,cc3		\n"	/* set CC3 to true */
		"	or%I3		%1,%3,%2		\n"
		"	cst.p		%2,%M0		,cc3,#1	\n"	/* if store happens... */
		"	corcc		gr29,gr29,gr0	,cc3,#1	\n"	/* ... clear ICC3.Z */
		"	beq		icc3,#0,0b		\n"
		: "+U"(*v), "=&r"(old), "=r"(tmp)
		: "NPr"(mask)
		: "memory", "cc7", "cc3", "icc3"
		);

	return old;
}

static inline
unsigned long atomic_test_and_XOR_mask(unsigned long mask, volatile unsigned long *v)
{
	unsigned long old, tmp;

	asm volatile(
		"0:						\n"
		"	orcc		gr0,gr0,gr0,icc3	\n"	/* set ICC3.Z */
		"	ckeq		icc3,cc7		\n"
		"	ld.p		%M0,%1			\n"	/* LD.P/ORCR are atomic */
		"	orcr		cc7,cc7,cc3		\n"	/* set CC3 to true */
		"	xor%I3		%1,%3,%2		\n"
		"	cst.p		%2,%M0		,cc3,#1	\n"	/* if store happens... */
		"	corcc		gr29,gr29,gr0	,cc3,#1	\n"	/* ... clear ICC3.Z */
		"	beq		icc3,#0,0b		\n"
		: "+U"(*v), "=&r"(old), "=r"(tmp)
		: "NPr"(mask)
		: "memory", "cc7", "cc3", "icc3"
		);

	return old;
}

#else

extern unsigned long atomic_test_and_ANDNOT_mask(unsigned long mask, volatile unsigned long *v);
extern unsigned long atomic_test_and_OR_mask(unsigned long mask, volatile unsigned long *v);
extern unsigned long atomic_test_and_XOR_mask(unsigned long mask, volatile unsigned long *v);

#endif

#define atomic_clear_mask(mask, v)	atomic_test_and_ANDNOT_mask((mask), (v))
#define atomic_set_mask(mask, v)	atomic_test_and_OR_mask((mask), (v))

static inline int test_and_clear_bit(int nr, volatile void *addr)
{
	volatile unsigned long *ptr = addr;
	unsigned long mask = 1UL << (nr & 31);
	ptr += nr >> 5;
	return (atomic_test_and_ANDNOT_mask(mask, ptr) & mask) != 0;
}

static inline int test_and_set_bit(int nr, volatile void *addr)
{
	volatile unsigned long *ptr = addr;
	unsigned long mask = 1UL << (nr & 31);
	ptr += nr >> 5;
	return (atomic_test_and_OR_mask(mask, ptr) & mask) != 0;
}

static inline int test_and_change_bit(int nr, volatile void *addr)
{
	volatile unsigned long *ptr = addr;
	unsigned long mask = 1UL << (nr & 31);
	ptr += nr >> 5;
	return (atomic_test_and_XOR_mask(mask, ptr) & mask) != 0;
}

static inline void clear_bit(int nr, volatile void *addr)
{
	test_and_clear_bit(nr, addr);
}

static inline void set_bit(int nr, volatile void *addr)
{
	test_and_set_bit(nr, addr);
}

static inline void change_bit(int nr, volatile void * addr)
{
	test_and_change_bit(nr, addr);
}

static inline void __clear_bit(int nr, volatile void * addr)
{
	volatile unsigned long *a = addr;
	int mask;

	a += nr >> 5;
	mask = 1 << (nr & 31);
	*a &= ~mask;
}

static inline void __set_bit(int nr, volatile void * addr)
{
	volatile unsigned long *a = addr;
	int mask;

	a += nr >> 5;
	mask = 1 << (nr & 31);
	*a |= mask;
}

static inline void __change_bit(int nr, volatile void *addr)
{
	volatile unsigned long *a = addr;
	int mask;

	a += nr >> 5;
	mask = 1 << (nr & 31);
	*a ^= mask;
}

static inline int __test_and_clear_bit(int nr, volatile void * addr)
{
	volatile unsigned long *a = addr;
	int mask, retval;

	a += nr >> 5;
	mask = 1 << (nr & 31);
	retval = (mask & *a) != 0;
	*a &= ~mask;
	return retval;
}

static inline int __test_and_set_bit(int nr, volatile void * addr)
{
	volatile unsigned long *a = addr;
	int mask, retval;

	a += nr >> 5;
	mask = 1 << (nr & 31);
	retval = (mask & *a) != 0;
	*a |= mask;
	return retval;
}

static inline int __test_and_change_bit(int nr, volatile void * addr)
{
	volatile unsigned long *a = addr;
	int mask, retval;

	a += nr >> 5;
	mask = 1 << (nr & 31);
	retval = (mask & *a) != 0;
	*a ^= mask;
	return retval;
}

/*
 * This routine doesn't need to be atomic.
 */
static inline int __constant_test_bit(int nr, const volatile void * addr)
{
	return ((1UL << (nr & 31)) & (((const volatile unsigned int *) addr)[nr >> 5])) != 0;
}

static inline int __test_bit(int nr, const volatile void * addr)
{
	int 	* a = (int *) addr;
	int	mask;

	a += nr >> 5;
	mask = 1 << (nr & 0x1f);
	return ((mask & *a) != 0);
}

#define test_bit(nr,addr) \
(__builtin_constant_p(nr) ? \
 __constant_test_bit((nr),(addr)) : \
 __test_bit((nr),(addr)))

#include <asm-generic/bitops/find.h>

/**
 * fls - find last bit set
 * @x: the word to search
 *
 * This is defined the same way as ffs:
 * - return 32..1 to indicate bit 31..0 most significant bit set
 * - return 0 to indicate no bits set
 */
#define fls(x)						\
({							\
	int bit;					\
							\
	asm("	subcc	%1,gr0,gr0,icc0		\n"	\
	    "	ckne	icc0,cc4		\n"	\
	    "	cscan.p	%1,gr0,%0	,cc4,#1	\n"	\
	    "	csub	%0,%0,%0	,cc4,#0	\n"	\
	    "   csub    %2,%0,%0	,cc4,#1	\n"	\
	    : "=&r"(bit)				\
	    : "r"(x), "r"(32)				\
	    : "icc0", "cc4"				\
	    );						\
							\
	bit;						\
})

/**
 * fls64 - find last bit set in a 64-bit value
 * @n: the value to search
 *
 * This is defined the same way as ffs:
 * - return 64..1 to indicate bit 63..0 most significant bit set
 * - return 0 to indicate no bits set
 */
static inline __attribute__((const))
int fls64(u64 n)
{
	union {
		u64 ll;
		struct { u32 h, l; };
	} _;
	int bit, x, y;

	_.ll = n;

	asm("	subcc.p		%3,gr0,gr0,icc0		\n"
	    "	subcc		%4,gr0,gr0,icc1		\n"
	    "	ckne		icc0,cc4		\n"
	    "	ckne		icc1,cc5		\n"
	    "	norcr		cc4,cc5,cc6		\n"
	    "	csub.p		%0,%0,%0	,cc6,1	\n"
	    "	orcr		cc5,cc4,cc4		\n"
	    "	andcr		cc4,cc5,cc4		\n"
	    "	cscan.p		%3,gr0,%0	,cc4,0	\n"
	    "   setlos		#64,%1			\n"
	    "	cscan.p		%4,gr0,%0	,cc4,1	\n"
	    "   setlos		#32,%2			\n"
	    "	csub.p		%1,%0,%0	,cc4,0	\n"
	    "	csub		%2,%0,%0	,cc4,1	\n"
	    : "=&r"(bit), "=r"(x), "=r"(y)
	    : "0r"(_.h), "r"(_.l)
	    : "icc0", "icc1", "cc4", "cc5", "cc6"
	    );
	return bit;

}

/**
 * ffs - find first bit set
 * @x: the word to search
 *
 * - return 32..1 to indicate bit 31..0 most least significant bit set
 * - return 0 to indicate no bits set
 */
static inline __attribute__((const))
int ffs(int x)
{
	/* Note: (x & -x) gives us a mask that is the least significant
	 * (rightmost) 1-bit of the value in x.
	 */
	return fls(x & -x);
}

/**
 * __ffs - find first bit set
 * @x: the word to search
 *
 * - return 31..0 to indicate bit 31..0 most least significant bit set
 * - if no bits are set in x, the result is undefined
 */
static inline __attribute__((const))
int __ffs(unsigned long x)
{
	int bit;
	asm("scan %1,gr0,%0" : "=r"(bit) : "r"(x & -x));
	return 31 - bit;
}

/**
 * __fls - find last (most-significant) set bit in a long word
 * @word: the word to search
 *
 * Undefined if no set bit exists, so code should check against 0 first.
 */
static inline unsigned long __fls(unsigned long word)
{
	unsigned long bit;
	asm("scan %1,gr0,%0" : "=r"(bit) : "r"(word));
	return bit;
}

/*
 * special slimline version of fls() for calculating ilog2_u32()
 * - note: no protection against n == 0
 */
#define ARCH_HAS_ILOG2_U32
static inline __attribute__((const))
int __ilog2_u32(u32 n)
{
	int bit;
	asm("scan %1,gr0,%0" : "=r"(bit) : "r"(n));
	return 31 - bit;
}

/*
 * special slimline version of fls64() for calculating ilog2_u64()
 * - note: no protection against n == 0
 */
#define ARCH_HAS_ILOG2_U64
static inline __attribute__((const))
int __ilog2_u64(u64 n)
{
	union {
		u64 ll;
		struct { u32 h, l; };
	} _;
	int bit, x, y;

	_.ll = n;

	asm("	subcc		%3,gr0,gr0,icc0		\n"
	    "	ckeq		icc0,cc4		\n"
	    "	cscan.p		%3,gr0,%0	,cc4,0	\n"
	    "   setlos		#63,%1			\n"
	    "	cscan.p		%4,gr0,%0	,cc4,1	\n"
	    "   setlos		#31,%2			\n"
	    "	csub.p		%1,%0,%0	,cc4,0	\n"
	    "	csub		%2,%0,%0	,cc4,1	\n"
	    : "=&r"(bit), "=r"(x), "=r"(y)
	    : "0r"(_.h), "r"(_.l)
	    : "icc0", "cc4"
	    );
	return bit;
}

#include <asm-generic/bitops/sched.h>
#include <asm-generic/bitops/hweight.h>
#include <asm-generic/bitops/lock.h>

#include <asm-generic/bitops/ext2-non-atomic.h>

#define ext2_set_bit_atomic(lock,nr,addr)	test_and_set_bit  ((nr) ^ 0x18, (addr))
#define ext2_clear_bit_atomic(lock,nr,addr)	test_and_clear_bit((nr) ^ 0x18, (addr))

#include <asm-generic/bitops/minix-le.h>

#endif /* __KERNEL__ */

#endif /* _ASM_BITOPS_H */
