/*
 * Copyright 1995, Russell King.
 *
 * Based on the arm32 version by RMK (and others). Their copyrights apply to
 * Those parts.
 * Modified for arm26 by Ian Molton on 25/11/04
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

#define smp_mb__before_clear_bit()	do { } while (0)
#define smp_mb__after_clear_bit()	do { } while (0)

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

/*
 * Now the non-atomic variants.  We let the compiler handle all
 * optimisations for these.  These are all _native_ endian.
 */
static inline void __set_bit(int nr, volatile unsigned long *p)
{
	p[nr >> 5] |= (1UL << (nr & 31));
}

static inline void __clear_bit(int nr, volatile unsigned long *p)
{
	p[nr >> 5] &= ~(1UL << (nr & 31));
}

static inline void __change_bit(int nr, volatile unsigned long *p)
{
	p[nr >> 5] ^= (1UL << (nr & 31));
}

static inline int __test_and_set_bit(int nr, volatile unsigned long *p)
{
	unsigned long oldval, mask = 1UL << (nr & 31);

	p += nr >> 5;

	oldval = *p;
	*p = oldval | mask;
	return oldval & mask;
}

static inline int __test_and_clear_bit(int nr, volatile unsigned long *p)
{
	unsigned long oldval, mask = 1UL << (nr & 31);

	p += nr >> 5;

	oldval = *p;
	*p = oldval & ~mask;
	return oldval & mask;
}

static inline int __test_and_change_bit(int nr, volatile unsigned long *p)
{
	unsigned long oldval, mask = 1UL << (nr & 31);

	p += nr >> 5;

	oldval = *p;
	*p = oldval ^ mask;
	return oldval & mask;
}

/*
 * This routine doesn't need to be atomic.
 */
static inline int __test_bit(int nr, const volatile unsigned long * p)
{
	return (p[nr >> 5] >> (nr & 31)) & 1UL;
}

/*
 * Little endian assembly bitops.  nr = 0 -> byte 0 bit 0.
 */
extern void _set_bit_le(int nr, volatile unsigned long * p);
extern void _clear_bit_le(int nr, volatile unsigned long * p);
extern void _change_bit_le(int nr, volatile unsigned long * p);
extern int _test_and_set_bit_le(int nr, volatile unsigned long * p);
extern int _test_and_clear_bit_le(int nr, volatile unsigned long * p);
extern int _test_and_change_bit_le(int nr, volatile unsigned long * p);
extern int _find_first_zero_bit_le(void * p, unsigned size);
extern int _find_next_zero_bit_le(void * p, int size, int offset);
extern int _find_first_bit_le(const unsigned long *p, unsigned size);
extern int _find_next_bit_le(const unsigned long *p, int size, int offset);

/*
 * The __* form of bitops are non-atomic and may be reordered.
 */
#define	ATOMIC_BITOP_LE(name,nr,p)		\
	(__builtin_constant_p(nr) ?		\
	 ____atomic_##name(nr, p) :		\
	 _##name##_le(nr,p))

#define NONATOMIC_BITOP(name,nr,p)		\
	(____nonatomic_##name(nr, p))

/*
 * These are the little endian, atomic definitions.
 */
#define set_bit(nr,p)			ATOMIC_BITOP_LE(set_bit,nr,p)
#define clear_bit(nr,p)			ATOMIC_BITOP_LE(clear_bit,nr,p)
#define change_bit(nr,p)		ATOMIC_BITOP_LE(change_bit,nr,p)
#define test_and_set_bit(nr,p)		ATOMIC_BITOP_LE(test_and_set_bit,nr,p)
#define test_and_clear_bit(nr,p)	ATOMIC_BITOP_LE(test_and_clear_bit,nr,p)
#define test_and_change_bit(nr,p)	ATOMIC_BITOP_LE(test_and_change_bit,nr,p)
#define test_bit(nr,p)			__test_bit(nr,p)
#define find_first_zero_bit(p,sz)	_find_first_zero_bit_le(p,sz)
#define find_next_zero_bit(p,sz,off)	_find_next_zero_bit_le(p,sz,off)
#define find_first_bit(p,sz)		_find_first_bit_le(p,sz)
#define find_next_bit(p,sz,off)		_find_next_bit_le(p,sz,off)

#define WORD_BITOFF_TO_LE(x)		((x))

/*
 * ffz = Find First Zero in word. Undefined if no zero exists,
 * so code should check against ~0UL first..
 */
static inline unsigned long ffz(unsigned long word)
{
	int k;

	word = ~word;
	k = 31;
	if (word & 0x0000ffff) { k -= 16; word <<= 16; }
	if (word & 0x00ff0000) { k -= 8;  word <<= 8;  }
	if (word & 0x0f000000) { k -= 4;  word <<= 4;  }
	if (word & 0x30000000) { k -= 2;  word <<= 2;  }
	if (word & 0x40000000) { k -= 1; }
        return k;
}

/*
 * ffz = Find First Zero in word. Undefined if no zero exists,
 * so code should check against ~0UL first..
 */
static inline unsigned long __ffs(unsigned long word)
{
	int k;

	k = 31;
	if (word & 0x0000ffff) { k -= 16; word <<= 16; }
	if (word & 0x00ff0000) { k -= 8;  word <<= 8;  }
	if (word & 0x0f000000) { k -= 4;  word <<= 4;  }
	if (word & 0x30000000) { k -= 2;  word <<= 2;  }
	if (word & 0x40000000) { k -= 1; }
        return k;
}

/*
 * fls: find last bit set.
 */

#define fls(x) generic_fls(x)
#define fls64(x)   generic_fls64(x)

/*
 * ffs: find first bit set. This is defined the same way as
 * the libc and compiler builtin ffs routines, therefore
 * differs in spirit from the above ffz (man ffs).
 */

#define ffs(x) generic_ffs(x)

/*
 * Find first bit set in a 168-bit bitmap, where the first
 * 128 bits are unlikely to be set.
 */
static inline int sched_find_first_bit(unsigned long *b)
{
	unsigned long v;
	unsigned int off;

	for (off = 0; v = b[off], off < 4; off++) {
		if (unlikely(v))
			break;
	}
	return __ffs(v) + off * 32;
}

/*
 * hweightN: returns the hamming weight (i.e. the number
 * of bits set) of a N-bit word
 */

#define hweight32(x) generic_hweight32(x)
#define hweight16(x) generic_hweight16(x)
#define hweight8(x) generic_hweight8(x)

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
		__test_bit(WORD_BITOFF_TO_LE(nr), (unsigned long *)(p))
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
		__test_bit(WORD_BITOFF_TO_LE(nr), (unsigned long *)(p))
#define minix_test_and_set_bit(nr,p)		\
		__test_and_set_bit(WORD_BITOFF_TO_LE(nr), (unsigned long *)(p))
#define minix_test_and_clear_bit(nr,p)		\
		__test_and_clear_bit(WORD_BITOFF_TO_LE(nr), (unsigned long *)(p))
#define minix_find_first_zero_bit(p,sz)		\
		_find_first_zero_bit_le(p,sz)

#endif /* __KERNEL__ */

#endif /* _ARM_BITOPS_H */
