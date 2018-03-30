/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_GENERIC_BITOPS_LOCK_H_
#define _ASM_GENERIC_BITOPS_LOCK_H_

/**
 * test_and_set_bit_lock - Set a bit and return its old value, for lock
 * @nr: Bit to set
 * @addr: Address to count from
 *
 * This operation is atomic and provides acquire barrier semantics if
 * the returned value is 0.
 * It can be used to implement bit locks.
 */
#define test_and_set_bit_lock(nr, addr)	test_and_set_bit(nr, addr)

/**
 * clear_bit_unlock - Clear a bit in memory, for unlock
 * @nr: the bit to set
 * @addr: the address to start counting from
 *
 * This operation is atomic and provides release barrier semantics.
 */
#define clear_bit_unlock(nr, addr)	\
do {					\
	smp_mb__before_atomic();	\
	clear_bit(nr, addr);		\
} while (0)

/**
 * __clear_bit_unlock - Clear a bit in memory, for unlock
 * @nr: the bit to set
 * @addr: the address to start counting from
 *
 * A weaker form of clear_bit_unlock() as used by __bit_lock_unlock(). If all
 * the bits in the word are protected by this lock some archs can use weaker
 * ops to safely unlock.
 *
 * See for example x86's implementation.
 */
#define __clear_bit_unlock(nr, addr)	\
do {					\
	smp_mb__before_atomic();	\
	clear_bit(nr, addr);		\
} while (0)

#endif /* _ASM_GENERIC_BITOPS_LOCK_H_ */

