/* bitops.h: bit operations for the Fujitsu FR-V CPUs
 *
 * For an explanation of how atomic ops work in this arch, see:
 *   Documentation/fujitsu/frv/atomic-ops.txt
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

#include <linux/config.h>
#include <linux/compiler.h>
#include <asm/byteorder.h>
#include <asm/system.h>
#include <asm/atomic.h>

#ifdef __KERNEL__

#include <asm-generic/bitops/ffz.h>

/*
 * clear_bit() doesn't provide any barrier for the compiler.
 */
#define smp_mb__before_clear_bit()	barrier()
#define smp_mb__after_clear_bit()	barrier()

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

#include <asm-generic/bitops/ffs.h>
#include <asm-generic/bitops/__ffs.h>
#include <asm-generic/bitops/find.h>

/*
 * fls: find last bit set.
 */
#define fls(x)						\
({							\
	int bit;					\
							\
	asm("scan %1,gr0,%0" : "=r"(bit) : "r"(x));	\
							\
	bit ? 33 - bit : bit;				\
})

#include <asm-generic/bitops/fls64.h>
#include <asm-generic/bitops/sched.h>
#include <asm-generic/bitops/hweight.h>

#include <asm-generic/bitops/ext2-non-atomic.h>

#define ext2_set_bit_atomic(lock,nr,addr)	test_and_set_bit  ((nr) ^ 0x18, (addr))
#define ext2_clear_bit_atomic(lock,nr,addr)	test_and_clear_bit((nr) ^ 0x18, (addr))

#include <asm-generic/bitops/minix-le.h>

#endif /* __KERNEL__ */

#endif /* _ASM_BITOPS_H */
