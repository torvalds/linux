/*
 * Copyright 1995, Russell King.
 * Various bits and pieces copyrights include:
 *  Linus Torvalds (test_bit).
 * Big endian support: Copyright 2001, Nicolas Pitre
 *  reworked by rmk.
 *
 * bit 0 is the LSB of an "unsigned long" quantity.
 *
 * Please note that the code in this file should never be included
 * from user space.  Many of these are not implemented in assembler
 * since they would be too costly.  Also, they require privileged
 * instructions (which are not available from user mode) to ensure
 * that they are atomic.
 */

#ifndef __ASM_ARM_BITOPS_H
#define __ASM_ARM_BITOPS_H

#ifdef __KERNEL__

#include <linux/compiler.h>
#include <asm/system.h>

#define smp_mb__before_clear_bit()	mb()
#define smp_mb__after_clear_bit()	mb()

/*
 * These functions are the basis of our bit ops.
 *
 * First, the atomic bitops. These use native endian.
 */
static inline void ____atomic_set_bit(unsigned int bit, volatile unsigned long *p)
{
	unsigned long flags;
	unsigned long mask = 1UL << (bit & 31);

	p += bit >> 5;

	local_irq_save(flags);
	*p |= mask;
	local_irq_restore(flags);
}

static inline void ____atomic_clear_bit(unsigned int bit, volatile unsigned long *p)
{
	unsigned long flags;
	unsigned long mask = 1UL << (bit & 31);

	p += bit >> 5;

	local_irq_save(flags);
	*p &= ~mask;
	local_irq_restore(flags);
}

static inline void ____atomic_change_bit(unsigned int bit, volatile unsigned long *p)
{
	unsigned long flags;
	unsigned long mask = 1UL << (bit & 31);

	p += bit >> 5;

	local_irq_save(flags);
	*p ^= mask;
	local_irq_restore(flags);
}

static inline int
____atomic_test_and_set_bit(unsigned int bit, volatile unsigned long *p)
{
	unsigned long flags;
	unsigned int res;
	unsigned long mask = 1UL << (bit & 31);

	p += bit >> 5;

	local_irq_save(flags);
	res = *p;
	*p = res | mask;
	local_irq_restore(flags);

	return res & mask;
}

static inline int
____atomic_test_and_clear_bit(unsigned int bit, volatile unsigned long *p)
{
	unsigned long flags;
	unsigned int res;
	unsigned long mask = 1UL << (bit & 31);

	p += bit >> 5;

	local_irq_save(flags);
	res = *p;
	*p = res & ~mask;
	local_irq_restore(flags);

	return res & mask;
}

static inline int
____atomic_test_and_change_bit(unsigned int bit, volatile unsigned long *p)
{
	unsigned long flags;
	unsigned int res;
	unsigned long mask = 1UL << (bit & 31);

	p += bit >> 5;

	local_irq_save(flags);
	res = *p;
	*p = res ^ mask;
	local_irq_restore(flags);

	return res & mask;
}

#include <asm-generic/bitops/non-atomic.h>

/*
 *  A note about Endian-ness.
 *  -------------------------
 *
 * When the ARM is put into big endian mode via CR15, the processor
 * merely swaps the order of bytes within words, thus:
 *
 *          ------------ physical data bus bits -----------
 *          D31 ... D24  D23 ... D16  D15 ... D8  D7 ... D0
 * little     byte 3       byte 2       byte 1      byte 0
 * big        byte 0       byte 1       byte 2      byte 3
 *
 * This means that reading a 32-bit word at address 0 returns the same
 * value irrespective of the endian mode bit.
 *
 * Peripheral devices should be connected with the data bus reversed in
 * "Big Endian" mode.  ARM Application Note 61 is applicable, and is
 * available from http://www.arm.com/.
 *
 * The following assumes that the data bus connectivity for big endian
 * mode has been followed.
 *
 * Note that bit 0 is defined to be 32-bit word bit 0, not byte 0 bit 0.
 */

/*
 * Little endian assembly bitops.  nr = 0 -> byte 0 bit 0.
 */
extern void _set_bit_le(int nr, volatile unsigned long * p);
extern void _clear_bit_le(int nr, volatile unsigned long * p);
extern void _change_bit_le(int nr, volatile unsigned long * p);
extern int _test_and_set_bit_le(int nr, volatile unsigned long * p);
extern int _test_and_clear_bit_le(int nr, volatile unsigned long * p);
extern int _test_and_change_bit_le(int nr, volatile unsigned long * p);
extern int _find_first_zero_bit_le(const void * p, unsigned size);
extern int _find_next_zero_bit_le(const void * p, int size, int offset);
extern int _find_first_bit_le(const unsigned long *p, unsigned size);
extern int _find_next_bit_le(const unsigned long *p, int size, int offset);

/*
 * Big endian assembly bitops.  nr = 0 -> byte 3 bit 0.
 */
extern void _set_bit_be(int nr, volatile unsigned long * p);
extern void _clear_bit_be(int nr, volatile unsigned long * p);
extern void _change_bit_be(int nr, volatile unsigned long * p);
extern int _test_and_set_bit_be(int nr, volatile unsigned long * p);
extern int _test_and_clear_bit_be(int nr, volatile unsigned long * p);
extern int _test_and_change_bit_be(int nr, volatile unsigned long * p);
extern int _find_first_zero_bit_be(const void * p, unsigned size);
extern int _find_next_zero_bit_be(const void * p, int size, int offset);
extern int _find_first_bit_be(const unsigned long *p, unsigned size);
extern int _find_next_bit_be(const unsigned long *p, int size, int offset);

#ifndef CONFIG_SMP
/*
 * The __* form of bitops are non-atomic and may be reordered.
 */
#define	ATOMIC_BITOP_LE(name,nr,p)		\
	(__builtin_constant_p(nr) ?		\
	 ____atomic_##name(nr, p) :		\
	 _##name##_le(nr,p))

#define	ATOMIC_BITOP_BE(name,nr,p)		\
	(__builtin_constant_p(nr) ?		\
	 ____atomic_##name(nr, p) :		\
	 _##name##_be(nr,p))
#else
#define ATOMIC_BITOP_LE(name,nr,p)	_##name##_le(nr,p)
#define ATOMIC_BITOP_BE(name,nr,p)	_##name##_be(nr,p)
#endif

#define NONATOMIC_BITOP(name,nr,p)		\
	(____nonatomic_##name(nr, p))

#ifndef __ARMEB__
/*
 * These are the little endian, atomic definitions.
 */
#define set_bit(nr,p)			ATOMIC_BITOP_LE(set_bit,nr,p)
#define clear_bit(nr,p)			ATOMIC_BITOP_LE(clear_bit,nr,p)
#define change_bit(nr,p)		ATOMIC_BITOP_LE(change_bit,nr,p)
#define test_and_set_bit(nr,p)		ATOMIC_BITOP_LE(test_and_set_bit,nr,p)
#define test_and_clear_bit(nr,p)	ATOMIC_BITOP_LE(test_and_clear_bit,nr,p)
#define test_and_change_bit(nr,p)	ATOMIC_BITOP_LE(test_and_change_bit,nr,p)
#define find_first_zero_bit(p,sz)	_find_first_zero_bit_le(p,sz)
#define find_next_zero_bit(p,sz,off)	_find_next_zero_bit_le(p,sz,off)
#define find_first_bit(p,sz)		_find_first_bit_le(p,sz)
#define find_next_bit(p,sz,off)		_find_next_bit_le(p,sz,off)

#define WORD_BITOFF_TO_LE(x)		((x))

#else

/*
 * These are the big endian, atomic definitions.
 */
#define set_bit(nr,p)			ATOMIC_BITOP_BE(set_bit,nr,p)
#define clear_bit(nr,p)			ATOMIC_BITOP_BE(clear_bit,nr,p)
#define change_bit(nr,p)		ATOMIC_BITOP_BE(change_bit,nr,p)
#define test_and_set_bit(nr,p)		ATOMIC_BITOP_BE(test_and_set_bit,nr,p)
#define test_and_clear_bit(nr,p)	ATOMIC_BITOP_BE(test_and_clear_bit,nr,p)
#define test_and_change_bit(nr,p)	ATOMIC_BITOP_BE(test_and_change_bit,nr,p)
#define find_first_zero_bit(p,sz)	_find_first_zero_bit_be(p,sz)
#define find_next_zero_bit(p,sz,off)	_find_next_zero_bit_be(p,sz,off)
#define find_first_bit(p,sz)		_find_first_bit_be(p,sz)
#define find_next_bit(p,sz,off)		_find_next_bit_be(p,sz,off)

#define WORD_BITOFF_TO_LE(x)		((x) ^ 0x18)

#endif

#if __LINUX_ARM_ARCH__ < 5

#include <asm-generic/bitops/ffz.h>
#include <asm-generic/bitops/__ffs.h>
#include <asm-generic/bitops/fls.h>
#include <asm-generic/bitops/ffs.h>

#else

static inline int constant_fls(int x)
{
	int r = 32;

	if (!x)
		return 0;
	if (!(x & 0xffff0000u)) {
		x <<= 16;
		r -= 16;
	}
	if (!(x & 0xff000000u)) {
		x <<= 8;
		r -= 8;
	}
	if (!(x & 0xf0000000u)) {
		x <<= 4;
		r -= 4;
	}
	if (!(x & 0xc0000000u)) {
		x <<= 2;
		r -= 2;
	}
	if (!(x & 0x80000000u)) {
		x <<= 1;
		r -= 1;
	}
	return r;
}

/*
 * On ARMv5 and above those functions can be implemented around
 * the clz instruction for much better code efficiency.
 */

#define fls(x) \
	( __builtin_constant_p(x) ? constant_fls(x) : \
	  ({ int __r; asm("clz\t%0, %1" : "=r"(__r) : "r"(x) : "cc"); 32-__r; }) )
#define ffs(x) ({ unsigned long __t = (x); fls(__t & -__t); })
#define __ffs(x) (ffs(x) - 1)
#define ffz(x) __ffs( ~(x) )

#endif

#include <asm-generic/bitops/fls64.h>

#include <asm-generic/bitops/sched.h>
#include <asm-generic/bitops/hweight.h>

/*
 * Ext2 is defined to use little-endian byte ordering.
 * These do not need to be atomic.
 */
#define ext2_set_bit(nr,p)			\
		__test_and_set_bit(WORD_BITOFF_TO_LE(nr), (unsigned long *)(p))
#define ext2_set_bit_atomic(lock,nr,p)          \
                test_and_set_bit(WORD_BITOFF_TO_LE(nr), (unsigned long *)(p))
#define ext2_clear_bit(nr,p)			\
		__test_and_clear_bit(WORD_BITOFF_TO_LE(nr), (unsigned long *)(p))
#define ext2_clear_bit_atomic(lock,nr,p)        \
                test_and_clear_bit(WORD_BITOFF_TO_LE(nr), (unsigned long *)(p))
#define ext2_test_bit(nr,p)			\
		test_bit(WORD_BITOFF_TO_LE(nr), (unsigned long *)(p))
#define ext2_find_first_zero_bit(p,sz)		\
		_find_first_zero_bit_le(p,sz)
#define ext2_find_next_zero_bit(p,sz,off)	\
		_find_next_zero_bit_le(p,sz,off)

/*
 * Minix is defined to use little-endian byte ordering.
 * These do not need to be atomic.
 */
#define minix_set_bit(nr,p)			\
		__set_bit(WORD_BITOFF_TO_LE(nr), (unsigned long *)(p))
#define minix_test_bit(nr,p)			\
		test_bit(WORD_BITOFF_TO_LE(nr), (unsigned long *)(p))
#define minix_test_and_set_bit(nr,p)		\
		__test_and_set_bit(WORD_BITOFF_TO_LE(nr), (unsigned long *)(p))
#define minix_test_and_clear_bit(nr,p)		\
		__test_and_clear_bit(WORD_BITOFF_TO_LE(nr), (unsigned long *)(p))
#define minix_find_first_zero_bit(p,sz)		\
		_find_first_zero_bit_le(p,sz)

#endif /* __KERNEL__ */

#endif /* _ARM_BITOPS_H */
