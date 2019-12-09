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

#include <linux/kasan-checks.h>

/**
 * clear_bit_unlock - Clear a bit in memory, for unlock
 * @nr: the bit to set
 * @addr: the address to start counting from
 *
 * This operation is atomic and provides release barrier semantics.
 */
static inline void clear_bit_unlock(long nr, volatile unsigned long *addr)
{
	kasan_check_write(addr + BIT_WORD(nr), sizeof(long));
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
	kasan_check_write(addr + BIT_WORD(nr), sizeof(long));
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
	kasan_check_write(addr + BIT_WORD(nr), sizeof(long));
	return arch_test_and_set_bit_lock(nr, addr);
}

#if defined(arch_clear_bit_unlock_is_negative_byte)
/**
 * clear_bit_unlock_is_negative_byte - Clear a bit in memory and test if bottom
 *                                     byte is negative, for unlock.
 * @nr: the bit to clear
 * @addr: the address to start counting from
 *
 * This operation is atomic and provides release barrier semantics.
 *
 * This is a bit of a one-trick-pony for the filemap code, which clears
 * PG_locked and tests PG_waiters,
 */
static inline bool
clear_bit_unlock_is_negative_byte(long nr, volatile unsigned long *addr)
{
	kasan_check_write(addr + BIT_WORD(nr), sizeof(long));
	return arch_clear_bit_unlock_is_negative_byte(nr, addr);
}
/* Let everybody know we have it. */
#define clear_bit_unlock_is_negative_byte clear_bit_unlock_is_negative_byte
#endif

#endif /* _ASM_GENERIC_BITOPS_INSTRUMENTED_LOCK_H */
