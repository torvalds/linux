/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_GENERIC_BITOPS_LOCK_H_
#define _ASM_GENERIC_BITOPS_LOCK_H_

#include <linux/atomic.h>
#include <linux/compiler.h>
#include <asm/barrier.h>

/**
 * arch_test_and_set_bit_lock - Set a bit and return its old value, for lock
 * @nr: Bit to set
 * @addr: Address to count from
 *
 * This operation is atomic and provides acquire barrier semantics if
 * the returned value is 0.
 * It can be used to implement bit locks.
 */
static __always_inline int
arch_test_and_set_bit_lock(unsigned int nr, volatile unsigned long *p)
{
	long old;
	unsigned long mask = BIT_MASK(nr);

	p += BIT_WORD(nr);
	if (READ_ONCE(*p) & mask)
		return 1;

	old = raw_atomic_long_fetch_or_acquire(mask, (atomic_long_t *)p);
	return !!(old & mask);
}


/**
 * arch_clear_bit_unlock - Clear a bit in memory, for unlock
 * @nr: the bit to set
 * @addr: the address to start counting from
 *
 * This operation is atomic and provides release barrier semantics.
 */
static __always_inline void
arch_clear_bit_unlock(unsigned int nr, volatile unsigned long *p)
{
	p += BIT_WORD(nr);
	raw_atomic_long_fetch_andnot_release(BIT_MASK(nr), (atomic_long_t *)p);
}

/**
 * arch___clear_bit_unlock - Clear a bit in memory, for unlock
 * @nr: the bit to set
 * @addr: the address to start counting from
 *
 * A weaker form of clear_bit_unlock() as used by __bit_lock_unlock(). If all
 * the bits in the word are protected by this lock some archs can use weaker
 * ops to safely unlock.
 *
 * See for example x86's implementation.
 */
static inline void
arch___clear_bit_unlock(unsigned int nr, volatile unsigned long *p)
{
	unsigned long old;

	p += BIT_WORD(nr);
	old = READ_ONCE(*p);
	old &= ~BIT_MASK(nr);
	raw_atomic_long_set_release((atomic_long_t *)p, old);
}

#ifndef arch_xor_unlock_is_negative_byte
static inline bool arch_xor_unlock_is_negative_byte(unsigned long mask,
		volatile unsigned long *p)
{
	long old;

	old = raw_atomic_long_fetch_xor_release(mask, (atomic_long_t *)p);
	return !!(old & BIT(7));
}
#endif

#include <asm-generic/bitops/instrumented-lock.h>

#endif /* _ASM_GENERIC_BITOPS_LOCK_H_ */
