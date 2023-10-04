/* SPDX-License-Identifier: GPL-2.0 */

/*
 * This file provides wrappers with sanitizer instrumentation for bit
 * locking operations.
 *
 * To use this functionality, an arch's bitops.h file needs to define each of
 * the below bit operations with an arch_ prefix (e.g. arch_set_bit(),
 * arch___set_bit(), etc.).
 */
#ifndef _ASM_GENERIC_BITOPS_INSTRUMENTED_LOCK_H
#define _ASM_GENERIC_BITOPS_INSTRUMENTED_LOCK_H

#include <linux/instrumented.h>

/**
 * clear_bit_unlock - Clear a bit in memory, for unlock
 * @nr: the bit to set
 * @addr: the address to start counting from
 *
 * This operation is atomic and provides release barrier semantics.
 */
static inline void clear_bit_unlock(long nr, volatile unsigned long *addr)
{
	kcsan_release();
	instrument_atomic_write(addr + BIT_WORD(nr), sizeof(long));
	arch_clear_bit_unlock(nr, addr);
}

/**
 * __clear_bit_unlock - Clears a bit in memory
 * @nr: Bit to clear
 * @addr: Address to start counting from
 *
 * This is a non-atomic operation but implies a release barrier before the
 * memory operation. It can be used for an unlock if no other CPUs can
 * concurrently modify other bits in the word.
 */
static inline void __clear_bit_unlock(long nr, volatile unsigned long *addr)
{
	kcsan_release();
	instrument_write(addr + BIT_WORD(nr), sizeof(long));
	arch___clear_bit_unlock(nr, addr);
}

/**
 * test_and_set_bit_lock - Set a bit and return its old value, for lock
 * @nr: Bit to set
 * @addr: Address to count from
 *
 * This operation is atomic and provides acquire barrier semantics if
 * the returned value is 0.
 * It can be used to implement bit locks.
 */
static inline bool test_and_set_bit_lock(long nr, volatile unsigned long *addr)
{
	instrument_atomic_read_write(addr + BIT_WORD(nr), sizeof(long));
	return arch_test_and_set_bit_lock(nr, addr);
}

#if defined(arch_xor_unlock_is_negative_byte)
/**
 * xor_unlock_is_negative_byte - XOR a single byte in memory and test if
 * it is negative, for unlock.
 * @mask: Change the bits which are set in this mask.
 * @addr: The address of the word containing the byte to change.
 *
 * Changes some of bits 0-6 in the word pointed to by @addr.
 * This operation is atomic and provides release barrier semantics.
 * Used to optimise some folio operations which are commonly paired
 * with an unlock or end of writeback.  Bit 7 is used as PG_waiters to
 * indicate whether anybody is waiting for the unlock.
 *
 * Return: Whether the top bit of the byte is set.
 */
static inline bool xor_unlock_is_negative_byte(unsigned long mask,
		volatile unsigned long *addr)
{
	kcsan_release();
	instrument_atomic_write(addr, sizeof(long));
	return arch_xor_unlock_is_negative_byte(mask, addr);
}
/* Let everybody know we have it. */
#define xor_unlock_is_negative_byte xor_unlock_is_negative_byte
#endif

#endif /* _ASM_GENERIC_BITOPS_INSTRUMENTED_LOCK_H */
