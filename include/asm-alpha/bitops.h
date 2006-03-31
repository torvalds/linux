#ifndef _ALPHA_BITOPS_H
#define _ALPHA_BITOPS_H

#include <linux/config.h>
#include <asm/compiler.h>

/*
 * Copyright 1994, Linus Torvalds.
 */

/*
 * These have to be done with inline assembly: that way the bit-setting
 * is guaranteed to be atomic. All bit operations return 0 if the bit
 * was cleared before the operation and != 0 if it was not.
 *
 * To get proper branch prediction for the main line, we must branch
 * forward to code at the end of this object's .text section, then
 * branch back to restart the operation.
 *
 * bit 0 is the LSB of addr; bit 64 is the LSB of (addr+1).
 */

static inline void
set_bit(unsigned long nr, volatile void * addr)
{
	unsigned long temp;
	int *m = ((int *) addr) + (nr >> 5);

	__asm__ __volatile__(
	"1:	ldl_l %0,%3\n"
	"	bis %0,%2,%0\n"
	"	stl_c %0,%1\n"
	"	beq %0,2f\n"
	".subsection 2\n"
	"2:	br 1b\n"
	".previous"
	:"=&r" (temp), "=m" (*m)
	:"Ir" (1UL << (nr & 31)), "m" (*m));
}

/*
 * WARNING: non atomic version.
 */
static inline void
__set_bit(unsigned long nr, volatile void * addr)
{
	int *m = ((int *) addr) + (nr >> 5);

	*m |= 1 << (nr & 31);
}

#define smp_mb__before_clear_bit()	smp_mb()
#define smp_mb__after_clear_bit()	smp_mb()

static inline void
clear_bit(unsigned long nr, volatile void * addr)
{
	unsigned long temp;
	int *m = ((int *) addr) + (nr >> 5);

	__asm__ __volatile__(
	"1:	ldl_l %0,%3\n"
	"	bic %0,%2,%0\n"
	"	stl_c %0,%1\n"
	"	beq %0,2f\n"
	".subsection 2\n"
	"2:	br 1b\n"
	".previous"
	:"=&r" (temp), "=m" (*m)
	:"Ir" (1UL << (nr & 31)), "m" (*m));
}

/*
 * WARNING: non atomic version.
 */
static __inline__ void
__clear_bit(unsigned long nr, volatile void * addr)
{
	int *m = ((int *) addr) + (nr >> 5);

	*m &= ~(1 << (nr & 31));
}

static inline void
change_bit(unsigned long nr, volatile void * addr)
{
	unsigned long temp;
	int *m = ((int *) addr) + (nr >> 5);

	__asm__ __volatile__(
	"1:	ldl_l %0,%3\n"
	"	xor %0,%2,%0\n"
	"	stl_c %0,%1\n"
	"	beq %0,2f\n"
	".subsection 2\n"
	"2:	br 1b\n"
	".previous"
	:"=&r" (temp), "=m" (*m)
	:"Ir" (1UL << (nr & 31)), "m" (*m));
}

/*
 * WARNING: non atomic version.
 */
static __inline__ void
__change_bit(unsigned long nr, volatile void * addr)
{
	int *m = ((int *) addr) + (nr >> 5);

	*m ^= 1 << (nr & 31);
}

static inline int
test_and_set_bit(unsigned long nr, volatile void *addr)
{
	unsigned long oldbit;
	unsigned long temp;
	int *m = ((int *) addr) + (nr >> 5);

	__asm__ __volatile__(
	"1:	ldl_l %0,%4\n"
	"	and %0,%3,%2\n"
	"	bne %2,2f\n"
	"	xor %0,%3,%0\n"
	"	stl_c %0,%1\n"
	"	beq %0,3f\n"
	"2:\n"
#ifdef CONFIG_SMP
	"	mb\n"
#endif
	".subsection 2\n"
	"3:	br 1b\n"
	".previous"
	:"=&r" (temp), "=m" (*m), "=&r" (oldbit)
	:"Ir" (1UL << (nr & 31)), "m" (*m) : "memory");

	return oldbit != 0;
}

/*
 * WARNING: non atomic version.
 */
static inline int
__test_and_set_bit(unsigned long nr, volatile void * addr)
{
	unsigned long mask = 1 << (nr & 0x1f);
	int *m = ((int *) addr) + (nr >> 5);
	int old = *m;

	*m = old | mask;
	return (old & mask) != 0;
}

static inline int
test_and_clear_bit(unsigned long nr, volatile void * addr)
{
	unsigned long oldbit;
	unsigned long temp;
	int *m = ((int *) addr) + (nr >> 5);

	__asm__ __volatile__(
	"1:	ldl_l %0,%4\n"
	"	and %0,%3,%2\n"
	"	beq %2,2f\n"
	"	xor %0,%3,%0\n"
	"	stl_c %0,%1\n"
	"	beq %0,3f\n"
	"2:\n"
#ifdef CONFIG_SMP
	"	mb\n"
#endif
	".subsection 2\n"
	"3:	br 1b\n"
	".previous"
	:"=&r" (temp), "=m" (*m), "=&r" (oldbit)
	:"Ir" (1UL << (nr & 31)), "m" (*m) : "memory");

	return oldbit != 0;
}

/*
 * WARNING: non atomic version.
 */
static inline int
__test_and_clear_bit(unsigned long nr, volatile void * addr)
{
	unsigned long mask = 1 << (nr & 0x1f);
	int *m = ((int *) addr) + (nr >> 5);
	int old = *m;

	*m = old & ~mask;
	return (old & mask) != 0;
}

static inline int
test_and_change_bit(unsigned long nr, volatile void * addr)
{
	unsigned long oldbit;
	unsigned long temp;
	int *m = ((int *) addr) + (nr >> 5);

	__asm__ __volatile__(
	"1:	ldl_l %0,%4\n"
	"	and %0,%3,%2\n"
	"	xor %0,%3,%0\n"
	"	stl_c %0,%1\n"
	"	beq %0,3f\n"
#ifdef CONFIG_SMP
	"	mb\n"
#endif
	".subsection 2\n"
	"3:	br 1b\n"
	".previous"
	:"=&r" (temp), "=m" (*m), "=&r" (oldbit)
	:"Ir" (1UL << (nr & 31)), "m" (*m) : "memory");

	return oldbit != 0;
}

/*
 * WARNING: non atomic version.
 */
static __inline__ int
__test_and_change_bit(unsigned long nr, volatile void * addr)
{
	unsigned long mask = 1 << (nr & 0x1f);
	int *m = ((int *) addr) + (nr >> 5);
	int old = *m;

	*m = old ^ mask;
	return (old & mask) != 0;
}

static inline int
test_bit(int nr, const volatile void * addr)
{
	return (1UL & (((const int *) addr)[nr >> 5] >> (nr & 31))) != 0UL;
}

/*
 * ffz = Find First Zero in word. Undefined if no zero exists,
 * so code should check against ~0UL first..
 *
 * Do a binary search on the bits.  Due to the nature of large
 * constants on the alpha, it is worthwhile to split the search.
 */
static inline unsigned long ffz_b(unsigned long x)
{
	unsigned long sum, x1, x2, x4;

	x = ~x & -~x;		/* set first 0 bit, clear others */
	x1 = x & 0xAA;
	x2 = x & 0xCC;
	x4 = x & 0xF0;
	sum = x2 ? 2 : 0;
	sum += (x4 != 0) * 4;
	sum += (x1 != 0);

	return sum;
}

static inline unsigned long ffz(unsigned long word)
{
#if defined(CONFIG_ALPHA_EV6) && defined(CONFIG_ALPHA_EV67)
	/* Whee.  EV67 can calculate it directly.  */
	return __kernel_cttz(~word);
#else
	unsigned long bits, qofs, bofs;

	bits = __kernel_cmpbge(word, ~0UL);
	qofs = ffz_b(bits);
	bits = __kernel_extbl(word, qofs);
	bofs = ffz_b(bits);

	return qofs*8 + bofs;
#endif
}

/*
 * __ffs = Find First set bit in word.  Undefined if no set bit exists.
 */
static inline unsigned long __ffs(unsigned long word)
{
#if defined(CONFIG_ALPHA_EV6) && defined(CONFIG_ALPHA_EV67)
	/* Whee.  EV67 can calculate it directly.  */
	return __kernel_cttz(word);
#else
	unsigned long bits, qofs, bofs;

	bits = __kernel_cmpbge(0, word);
	qofs = ffz_b(bits);
	bits = __kernel_extbl(word, qofs);
	bofs = ffz_b(~bits);

	return qofs*8 + bofs;
#endif
}

#ifdef __KERNEL__

/*
 * ffs: find first bit set. This is defined the same way as
 * the libc and compiler builtin ffs routines, therefore
 * differs in spirit from the above __ffs.
 */

static inline int ffs(int word)
{
	int result = __ffs(word) + 1;
	return word ? result : 0;
}

/*
 * fls: find last bit set.
 */
#if defined(CONFIG_ALPHA_EV6) && defined(CONFIG_ALPHA_EV67)
static inline int fls(int word)
{
	return 64 - __kernel_ctlz(word & 0xffffffff);
}
#else
#include <asm-generic/bitops/fls.h>
#endif
#include <asm-generic/bitops/fls64.h>

/* Compute powers of two for the given integer.  */
static inline long floor_log2(unsigned long word)
{
#if defined(CONFIG_ALPHA_EV6) && defined(CONFIG_ALPHA_EV67)
	return 63 - __kernel_ctlz(word);
#else
	long bit;
	for (bit = -1; word ; bit++)
		word >>= 1;
	return bit;
#endif
}

static inline long ceil_log2(unsigned long word)
{
	long bit = floor_log2(word);
	return bit + (word > (1UL << bit));
}

/*
 * hweightN: returns the hamming weight (i.e. the number
 * of bits set) of a N-bit word
 */

#if defined(CONFIG_ALPHA_EV6) && defined(CONFIG_ALPHA_EV67)
/* Whee.  EV67 can calculate it directly.  */
static inline unsigned long hweight64(unsigned long w)
{
	return __kernel_ctpop(w);
}

#define hweight32(x)	(unsigned int) hweight64((x) & 0xfffffffful)
#define hweight16(x)	(unsigned int) hweight64((x) & 0xfffful)
#define hweight8(x)	(unsigned int) hweight64((x) & 0xfful)
#else
#include <asm-generic/bitops/hweight.h>
#endif

#endif /* __KERNEL__ */

#include <asm-generic/bitops/find.h>

#ifdef __KERNEL__

/*
 * Every architecture must define this function. It's the fastest
 * way of searching a 140-bit bitmap where the first 100 bits are
 * unlikely to be set. It's guaranteed that at least one of the 140
 * bits is set.
 */
static inline unsigned long
sched_find_first_bit(unsigned long b[3])
{
	unsigned long b0 = b[0], b1 = b[1], b2 = b[2];
	unsigned long ofs;

	ofs = (b1 ? 64 : 128);
	b1 = (b1 ? b1 : b2);
	ofs = (b0 ? 0 : ofs);
	b0 = (b0 ? b0 : b1);

	return __ffs(b0) + ofs;
}

#include <asm-generic/bitops/ext2-non-atomic.h>

#define ext2_set_bit_atomic(l,n,a)   test_and_set_bit(n,a)
#define ext2_clear_bit_atomic(l,n,a) test_and_clear_bit(n,a)

#include <asm-generic/bitops/minix.h>

#endif /* __KERNEL__ */

#endif /* _ALPHA_BITOPS_H */
