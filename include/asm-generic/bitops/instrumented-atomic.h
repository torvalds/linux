/* SPDX-License-Identifier: GPL-2.0 */

/*
 * This file provides wrappers with sanitizer instrumentation for atomic bit
 * operations.
 *
 * To use this functionality, an arch's bitops.h file needs to define each of
 * the below bit operations with an arch_ prefix (e.g. arch_set_bit(),
 * arch___set_bit(), etc.).
 */
#ifndef _ASM_GENERIC_BITOPS_INSTRUMENTED_ATOMIC_H
#define _ASM_GENERIC_BITOPS_INSTRUMENTED_ATOMIC_H

#include <linux/instrumented.h>

/**
 * set_bit - Atomically set a bit in memory
 * @nr: the bit to set
 * @addr: the address to start counting from
 *
 * This is a relaxed atomic operation (no implied memory barriers).
 *
 * Note that @nr may be almost arbitrarily large; this function is not
 * restricted to acting on a single-word quantity.
 */
static inline void set_bit(long nr, volatile unsigned long *addr)
{
	instrument_atomic_write(addr + BIT_WORD(nr), sizeof(long));
	arch_set_bit(nr, addr);
}

/**
 * clear_bit - Clears a bit in memory
 * @nr: Bit to clear
 * @addr: Address to start counting from
 *
 * This is a relaxed atomic operation (no implied memory barriers).
 */
static inline void clear_bit(long nr, volatile unsigned long *addr)
{
	instrument_atomic_write(addr + BIT_WORD(nr), sizeof(long));
	arch_clear_bit(nr, addr);
}

/**
 * change_bit - Toggle a bit in memory
 * @nr: Bit to change
 * @addr: Address to start counting from
 *
 * This is a relaxed atomic operation (no implied memory barriers).
 *
 * Note that @nr may be almost arbitrarily large; this function is not
 * restricted to acting on a single-word quantity.
 */
static inline void change_bit(long nr, volatile unsigned long *addr)
{
	instrument_atomic_write(addr + BIT_WORD(nr), sizeof(long));
	arch_change_bit(nr, addr);
}

/**
 * test_and_set_bit - Set a bit and return its old value
 * @nr: Bit to set
 * @addr: Address to count from
 *
 * This is an atomic fully-ordered operation (implied full memory barrier).
 */
static inline bool test_and_set_bit(long nr, volatile unsigned long *addr)
{
	instrument_atomic_write(addr + BIT_WORD(nr), sizeof(long));
	return arch_test_and_set_bit(nr, addr);
}

/**
 * test_and_clear_bit - Clear a bit and return its old value
 * @nr: Bit to clear
 * @addr: Address to count from
 *
 * This is an atomic fully-ordered operation (implied full memory barrier).
 */
static inline bool test_and_clear_bit(long nr, volatile unsigned long *addr)
{
	instrument_atomic_write(addr + BIT_WORD(nr), sizeof(long));
	return arch_test_and_clear_bit(nr, addr);
}

/**
 * test_and_change_bit - Change a bit and return its old value
 * @nr: Bit to change
 * @addr: Address to count from
 *
 * This is an atomic fully-ordered operation (implied full memory barrier).
 */
static inline bool test_and_change_bit(long nr, volatile unsigned long *addr)
{
	instrument_atomic_write(addr + BIT_WORD(nr), sizeof(long));
	return arch_test_and_change_bit(nr, addr);
}

#endif /* _ASM_GENERIC_BITOPS_INSTRUMENTED_NON_ATOMIC_H */
