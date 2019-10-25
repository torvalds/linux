/* SPDX-License-Identifier: GPL-2.0 */

/*
 * This file provides wrappers with sanitizer instrumentation for bit
 * operations.
 *
 * To use this functionality, an arch's bitops.h file needs to define each of
 * the below bit operations with an arch_ prefix (e.g. arch_set_bit(),
 * arch___set_bit(), etc.).
 */
#ifndef _ASM_GENERIC_BITOPS_INSTRUMENTED_H
#define _ASM_GENERIC_BITOPS_INSTRUMENTED_H

#include <linux/kasan-checks.h>

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
	kasan_check_write(addr + BIT_WORD(nr), sizeof(long));
	arch_set_bit(nr, addr);
}

/**
 * __set_bit - Set a bit in memory
 * @nr: the bit to set
 * @addr: the address to start counting from
 *
 * Unlike set_bit(), this function is non-atomic. If it is called on the same
 * region of memory concurrently, the effect may be that only one operation
 * succeeds.
 */
static inline void __set_bit(long nr, volatile unsigned long *addr)
{
	kasan_check_write(addr + BIT_WORD(nr), sizeof(long));
	arch___set_bit(nr, addr);
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
	kasan_check_write(addr + BIT_WORD(nr), sizeof(long));
	arch_clear_bit(nr, addr);
}

/**
 * __clear_bit - Clears a bit in memory
 * @nr: the bit to clear
 * @addr: the address to start counting from
 *
 * Unlike clear_bit(), this function is non-atomic. If it is called on the same
 * region of memory concurrently, the effect may be that only one operation
 * succeeds.
 */
static inline void __clear_bit(long nr, volatile unsigned long *addr)
{
	kasan_check_write(addr + BIT_WORD(nr), sizeof(long));
	arch___clear_bit(nr, addr);
}

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
	kasan_check_write(addr + BIT_WORD(nr), sizeof(long));
	arch_change_bit(nr, addr);
}

/**
 * __change_bit - Toggle a bit in memory
 * @nr: the bit to change
 * @addr: the address to start counting from
 *
 * Unlike change_bit(), this function is non-atomic. If it is called on the same
 * region of memory concurrently, the effect may be that only one operation
 * succeeds.
 */
static inline void __change_bit(long nr, volatile unsigned long *addr)
{
	kasan_check_write(addr + BIT_WORD(nr), sizeof(long));
	arch___change_bit(nr, addr);
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
	kasan_check_write(addr + BIT_WORD(nr), sizeof(long));
	return arch_test_and_set_bit(nr, addr);
}

/**
 * __test_and_set_bit - Set a bit and return its old value
 * @nr: Bit to set
 * @addr: Address to count from
 *
 * This operation is non-atomic. If two instances of this operation race, one
 * can appear to succeed but actually fail.
 */
static inline bool __test_and_set_bit(long nr, volatile unsigned long *addr)
{
	kasan_check_write(addr + BIT_WORD(nr), sizeof(long));
	return arch___test_and_set_bit(nr, addr);
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

/**
 * test_and_clear_bit - Clear a bit and return its old value
 * @nr: Bit to clear
 * @addr: Address to count from
 *
 * This is an atomic fully-ordered operation (implied full memory barrier).
 */
static inline bool test_and_clear_bit(long nr, volatile unsigned long *addr)
{
	kasan_check_write(addr + BIT_WORD(nr), sizeof(long));
	return arch_test_and_clear_bit(nr, addr);
}

/**
 * __test_and_clear_bit - Clear a bit and return its old value
 * @nr: Bit to clear
 * @addr: Address to count from
 *
 * This operation is non-atomic. If two instances of this operation race, one
 * can appear to succeed but actually fail.
 */
static inline bool __test_and_clear_bit(long nr, volatile unsigned long *addr)
{
	kasan_check_write(addr + BIT_WORD(nr), sizeof(long));
	return arch___test_and_clear_bit(nr, addr);
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
	kasan_check_write(addr + BIT_WORD(nr), sizeof(long));
	return arch_test_and_change_bit(nr, addr);
}

/**
 * __test_and_change_bit - Change a bit and return its old value
 * @nr: Bit to change
 * @addr: Address to count from
 *
 * This operation is non-atomic. If two instances of this operation race, one
 * can appear to succeed but actually fail.
 */
static inline bool __test_and_change_bit(long nr, volatile unsigned long *addr)
{
	kasan_check_write(addr + BIT_WORD(nr), sizeof(long));
	return arch___test_and_change_bit(nr, addr);
}

/**
 * test_bit - Determine whether a bit is set
 * @nr: bit number to test
 * @addr: Address to start counting from
 */
static inline bool test_bit(long nr, const volatile unsigned long *addr)
{
	kasan_check_read(addr + BIT_WORD(nr), sizeof(long));
	return arch_test_bit(nr, addr);
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

#endif /* _ASM_GENERIC_BITOPS_INSTRUMENTED_H */
