#ifndef _ASM_IA64_BITOPS_H
#define _ASM_IA64_BITOPS_H

/*
 * Copyright (C) 1998-2003 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 *
 * 02/06/02 find_next_bit() and find_first_bit() added from Erich Focht's ia64
 * O(1) scheduler patch
 */

#include <linux/compiler.h>
#include <linux/types.h>
#include <asm/bitops.h>
#include <asm/intrinsics.h>

/**
 * set_bit - Atomically set a bit in memory
 * @nr: the bit to set
 * @addr: the address to start counting from
 *
 * This function is atomic and may not be reordered.  See __set_bit()
 * if you do not require the atomic guarantees.
 * Note that @nr may be almost arbitrarily large; this function is not
 * restricted to acting on a single-word quantity.
 *
 * The address must be (at least) "long" aligned.
 * Note that there are driver (e.g., eepro100) which use these operations to
 * operate on hw-defined data-structures, so we can't easily change these
 * operations to force a bigger alignment.
 *
 * bit 0 is the LSB of addr; bit 32 is the LSB of (addr+1).
 */
static __inline__ void
set_bit (int nr, volatile void *addr)
{
	__u32 bit, old, new;
	volatile __u32 *m;
	CMPXCHG_BUGCHECK_DECL

	m = (volatile __u32 *) addr + (nr >> 5);
	bit = 1 << (nr & 31);
	do {
		CMPXCHG_BUGCHECK(m);
		old = *m;
		new = old | bit;
	} while (cmpxchg_acq(m, old, new) != old);
}

/**
 * __set_bit - Set a bit in memory
 * @nr: the bit to set
 * @addr: the address to start counting from
 *
 * Unlike set_bit(), this function is non-atomic and may be reordered.
 * If it's called on the same region of memory simultaneously, the effect
 * may be that only one operation succeeds.
 */
static __inline__ void
__set_bit (int nr, volatile void *addr)
{
	*((__u32 *) addr + (nr >> 5)) |= (1 << (nr & 31));
}

/*
 * clear_bit() has "acquire" semantics.
 */
#define smp_mb__before_clear_bit()	smp_mb()
#define smp_mb__after_clear_bit()	do { /* skip */; } while (0)

/**
 * clear_bit - Clears a bit in memory
 * @nr: Bit to clear
 * @addr: Address to start counting from
 *
 * clear_bit() is atomic and may not be reordered.  However, it does
 * not contain a memory barrier, so if it is used for locking purposes,
 * you should call smp_mb__before_clear_bit() and/or smp_mb__after_clear_bit()
 * in order to ensure changes are visible on other processors.
 */
static __inline__ void
clear_bit (int nr, volatile void *addr)
{
	__u32 mask, old, new;
	volatile __u32 *m;
	CMPXCHG_BUGCHECK_DECL

	m = (volatile __u32 *) addr + (nr >> 5);
	mask = ~(1 << (nr & 31));
	do {
		CMPXCHG_BUGCHECK(m);
		old = *m;
		new = old & mask;
	} while (cmpxchg_acq(m, old, new) != old);
}

/**
 * __clear_bit - Clears a bit in memory (non-atomic version)
 */
static __inline__ void
__clear_bit (int nr, volatile void *addr)
{
	volatile __u32 *p = (__u32 *) addr + (nr >> 5);
	__u32 m = 1 << (nr & 31);
	*p &= ~m;
}

/**
 * change_bit - Toggle a bit in memory
 * @nr: Bit to clear
 * @addr: Address to start counting from
 *
 * change_bit() is atomic and may not be reordered.
 * Note that @nr may be almost arbitrarily large; this function is not
 * restricted to acting on a single-word quantity.
 */
static __inline__ void
change_bit (int nr, volatile void *addr)
{
	__u32 bit, old, new;
	volatile __u32 *m;
	CMPXCHG_BUGCHECK_DECL

	m = (volatile __u32 *) addr + (nr >> 5);
	bit = (1 << (nr & 31));
	do {
		CMPXCHG_BUGCHECK(m);
		old = *m;
		new = old ^ bit;
	} while (cmpxchg_acq(m, old, new) != old);
}

/**
 * __change_bit - Toggle a bit in memory
 * @nr: the bit to set
 * @addr: the address to start counting from
 *
 * Unlike change_bit(), this function is non-atomic and may be reordered.
 * If it's called on the same region of memory simultaneously, the effect
 * may be that only one operation succeeds.
 */
static __inline__ void
__change_bit (int nr, volatile void *addr)
{
	*((__u32 *) addr + (nr >> 5)) ^= (1 << (nr & 31));
}

/**
 * test_and_set_bit - Set a bit and return its old value
 * @nr: Bit to set
 * @addr: Address to count from
 *
 * This operation is atomic and cannot be reordered.  
 * It also implies a memory barrier.
 */
static __inline__ int
test_and_set_bit (int nr, volatile void *addr)
{
	__u32 bit, old, new;
	volatile __u32 *m;
	CMPXCHG_BUGCHECK_DECL

	m = (volatile __u32 *) addr + (nr >> 5);
	bit = 1 << (nr & 31);
	do {
		CMPXCHG_BUGCHECK(m);
		old = *m;
		new = old | bit;
	} while (cmpxchg_acq(m, old, new) != old);
	return (old & bit) != 0;
}

/**
 * __test_and_set_bit - Set a bit and return its old value
 * @nr: Bit to set
 * @addr: Address to count from
 *
 * This operation is non-atomic and can be reordered.  
 * If two examples of this operation race, one can appear to succeed
 * but actually fail.  You must protect multiple accesses with a lock.
 */
static __inline__ int
__test_and_set_bit (int nr, volatile void *addr)
{
	__u32 *p = (__u32 *) addr + (nr >> 5);
	__u32 m = 1 << (nr & 31);
	int oldbitset = (*p & m) != 0;

	*p |= m;
	return oldbitset;
}

/**
 * test_and_clear_bit - Clear a bit and return its old value
 * @nr: Bit to set
 * @addr: Address to count from
 *
 * This operation is atomic and cannot be reordered.  
 * It also implies a memory barrier.
 */
static __inline__ int
test_and_clear_bit (int nr, volatile void *addr)
{
	__u32 mask, old, new;
	volatile __u32 *m;
	CMPXCHG_BUGCHECK_DECL

	m = (volatile __u32 *) addr + (nr >> 5);
	mask = ~(1 << (nr & 31));
	do {
		CMPXCHG_BUGCHECK(m);
		old = *m;
		new = old & mask;
	} while (cmpxchg_acq(m, old, new) != old);
	return (old & ~mask) != 0;
}

/**
 * __test_and_clear_bit - Clear a bit and return its old value
 * @nr: Bit to set
 * @addr: Address to count from
 *
 * This operation is non-atomic and can be reordered.  
 * If two examples of this operation race, one can appear to succeed
 * but actually fail.  You must protect multiple accesses with a lock.
 */
static __inline__ int
__test_and_clear_bit(int nr, volatile void * addr)
{
	__u32 *p = (__u32 *) addr + (nr >> 5);
	__u32 m = 1 << (nr & 31);
	int oldbitset = *p & m;

	*p &= ~m;
	return oldbitset;
}

/**
 * test_and_change_bit - Change a bit and return its old value
 * @nr: Bit to set
 * @addr: Address to count from
 *
 * This operation is atomic and cannot be reordered.  
 * It also implies a memory barrier.
 */
static __inline__ int
test_and_change_bit (int nr, volatile void *addr)
{
	__u32 bit, old, new;
	volatile __u32 *m;
	CMPXCHG_BUGCHECK_DECL

	m = (volatile __u32 *) addr + (nr >> 5);
	bit = (1 << (nr & 31));
	do {
		CMPXCHG_BUGCHECK(m);
		old = *m;
		new = old ^ bit;
	} while (cmpxchg_acq(m, old, new) != old);
	return (old & bit) != 0;
}

/*
 * WARNING: non atomic version.
 */
static __inline__ int
__test_and_change_bit (int nr, void *addr)
{
	__u32 old, bit = (1 << (nr & 31));
	__u32 *m = (__u32 *) addr + (nr >> 5);

	old = *m;
	*m = old ^ bit;
	return (old & bit) != 0;
}

static __inline__ int
test_bit (int nr, const volatile void *addr)
{
	return 1 & (((const volatile __u32 *) addr)[nr >> 5] >> (nr & 31));
}

/**
 * ffz - find the first zero bit in a long word
 * @x: The long word to find the bit in
 *
 * Returns the bit-number (0..63) of the first (least significant) zero bit.
 * Undefined if no zero exists, so code should check against ~0UL first...
 */
static inline unsigned long
ffz (unsigned long x)
{
	unsigned long result;

	result = ia64_popcnt(x & (~x - 1));
	return result;
}

/**
 * __ffs - find first bit in word.
 * @x: The word to search
 *
 * Undefined if no bit exists, so code should check against 0 first.
 */
static __inline__ unsigned long
__ffs (unsigned long x)
{
	unsigned long result;

	result = ia64_popcnt((x-1) & ~x);
	return result;
}

#ifdef __KERNEL__

/*
 * Return bit number of last (most-significant) bit set.  Undefined
 * for x==0.  Bits are numbered from 0..63 (e.g., ia64_fls(9) == 3).
 */
static inline unsigned long
ia64_fls (unsigned long x)
{
	long double d = x;
	long exp;

	exp = ia64_getf_exp(d);
	return exp - 0xffff;
}

/*
 * Find the last (most significant) bit set.  Returns 0 for x==0 and
 * bits are numbered from 1..32 (e.g., fls(9) == 4).
 */
static inline int
fls (int t)
{
	unsigned long x = t & 0xffffffffu;

	if (!x)
		return 0;
	x |= x >> 1;
	x |= x >> 2;
	x |= x >> 4;
	x |= x >> 8;
	x |= x >> 16;
	return ia64_popcnt(x);
}

#include <asm-generic/bitops/fls64.h>

/*
 * ffs: find first bit set. This is defined the same way as the libc and
 * compiler builtin ffs routines, therefore differs in spirit from the above
 * ffz (man ffs): it operates on "int" values only and the result value is the
 * bit number + 1.  ffs(0) is defined to return zero.
 */
#define ffs(x)	__builtin_ffs(x)

/*
 * hweightN: returns the hamming weight (i.e. the number
 * of bits set) of a N-bit word
 */
static __inline__ unsigned long
hweight64 (unsigned long x)
{
	unsigned long result;
	result = ia64_popcnt(x);
	return result;
}

#define hweight32(x)	(unsigned int) hweight64((x) & 0xfffffffful)
#define hweight16(x)	(unsigned int) hweight64((x) & 0xfffful)
#define hweight8(x)	(unsigned int) hweight64((x) & 0xfful)

#endif /* __KERNEL__ */

#include <asm-generic/bitops/find.h>

#ifdef __KERNEL__

#include <asm-generic/bitops/ext2-non-atomic.h>

#define ext2_set_bit_atomic(l,n,a)	test_and_set_bit(n,a)
#define ext2_clear_bit_atomic(l,n,a)	test_and_clear_bit(n,a)

#include <asm-generic/bitops/minix.h>
#include <asm-generic/bitops/sched.h>

#endif /* __KERNEL__ */

#endif /* _ASM_IA64_BITOPS_H */
