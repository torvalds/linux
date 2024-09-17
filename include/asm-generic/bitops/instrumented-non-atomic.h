/* SPDX-License-Identifier: GPL-2.0 */

/*
 * This file provides wrappers with sanitizer instrumentation for non-atomic
 * bit operations.
 *
 * To use this functionality, an arch's bitops.h file needs to define each of
 * the below bit operations with an arch_ prefix (e.g. arch_set_bit(),
 * arch___set_bit(), etc.).
 */
#ifndef _ASM_GENERIC_BITOPS_INSTRUMENTED_NON_ATOMIC_H
#define _ASM_GENERIC_BITOPS_INSTRUMENTED_NON_ATOMIC_H

#include <linux/instrumented.h>

/**
 * ___set_bit - Set a bit in memory
 * @nr: the bit to set
 * @addr: the address to start counting from
 *
 * Unlike set_bit(), this function is non-atomic. If it is called on the same
 * region of memory concurrently, the effect may be that only one operation
 * succeeds.
 */
static __always_inline void
___set_bit(unsigned long nr, volatile unsigned long *addr)
{
	instrument_write(addr + BIT_WORD(nr), sizeof(long));
	arch___set_bit(nr, addr);
}

/**
 * ___clear_bit - Clears a bit in memory
 * @nr: the bit to clear
 * @addr: the address to start counting from
 *
 * Unlike clear_bit(), this function is non-atomic. If it is called on the same
 * region of memory concurrently, the effect may be that only one operation
 * succeeds.
 */
static __always_inline void
___clear_bit(unsigned long nr, volatile unsigned long *addr)
{
	instrument_write(addr + BIT_WORD(nr), sizeof(long));
	arch___clear_bit(nr, addr);
}

/**
 * ___change_bit - Toggle a bit in memory
 * @nr: the bit to change
 * @addr: the address to start counting from
 *
 * Unlike change_bit(), this function is non-atomic. If it is called on the same
 * region of memory concurrently, the effect may be that only one operation
 * succeeds.
 */
static __always_inline void
___change_bit(unsigned long nr, volatile unsigned long *addr)
{
	instrument_write(addr + BIT_WORD(nr), sizeof(long));
	arch___change_bit(nr, addr);
}

static __always_inline void __instrument_read_write_bitop(long nr, volatile unsigned long *addr)
{
	if (IS_ENABLED(CONFIG_KCSAN_ASSUME_PLAIN_WRITES_ATOMIC)) {
		/*
		 * We treat non-atomic read-write bitops a little more special.
		 * Given the operations here only modify a single bit, assuming
		 * non-atomicity of the writer is sufficient may be reasonable
		 * for certain usage (and follows the permissible nature of the
		 * assume-plain-writes-atomic rule):
		 * 1. report read-modify-write races -> check read;
		 * 2. do not report races with marked readers, but do report
		 *    races with unmarked readers -> check "atomic" write.
		 */
		kcsan_check_read(addr + BIT_WORD(nr), sizeof(long));
		/*
		 * Use generic write instrumentation, in case other sanitizers
		 * or tools are enabled alongside KCSAN.
		 */
		instrument_write(addr + BIT_WORD(nr), sizeof(long));
	} else {
		instrument_read_write(addr + BIT_WORD(nr), sizeof(long));
	}
}

/**
 * ___test_and_set_bit - Set a bit and return its old value
 * @nr: Bit to set
 * @addr: Address to count from
 *
 * This operation is non-atomic. If two instances of this operation race, one
 * can appear to succeed but actually fail.
 */
static __always_inline bool
___test_and_set_bit(unsigned long nr, volatile unsigned long *addr)
{
	__instrument_read_write_bitop(nr, addr);
	return arch___test_and_set_bit(nr, addr);
}

/**
 * ___test_and_clear_bit - Clear a bit and return its old value
 * @nr: Bit to clear
 * @addr: Address to count from
 *
 * This operation is non-atomic. If two instances of this operation race, one
 * can appear to succeed but actually fail.
 */
static __always_inline bool
___test_and_clear_bit(unsigned long nr, volatile unsigned long *addr)
{
	__instrument_read_write_bitop(nr, addr);
	return arch___test_and_clear_bit(nr, addr);
}

/**
 * ___test_and_change_bit - Change a bit and return its old value
 * @nr: Bit to change
 * @addr: Address to count from
 *
 * This operation is non-atomic. If two instances of this operation race, one
 * can appear to succeed but actually fail.
 */
static __always_inline bool
___test_and_change_bit(unsigned long nr, volatile unsigned long *addr)
{
	__instrument_read_write_bitop(nr, addr);
	return arch___test_and_change_bit(nr, addr);
}

/**
 * _test_bit - Determine whether a bit is set
 * @nr: bit number to test
 * @addr: Address to start counting from
 */
static __always_inline bool
_test_bit(unsigned long nr, const volatile unsigned long *addr)
{
	instrument_atomic_read(addr + BIT_WORD(nr), sizeof(long));
	return arch_test_bit(nr, addr);
}

/**
 * _test_bit_acquire - Determine, with acquire semantics, whether a bit is set
 * @nr: bit number to test
 * @addr: Address to start counting from
 */
static __always_inline bool
_test_bit_acquire(unsigned long nr, const volatile unsigned long *addr)
{
	instrument_atomic_read(addr + BIT_WORD(nr), sizeof(long));
	return arch_test_bit_acquire(nr, addr);
}

#endif /* _ASM_GENERIC_BITOPS_INSTRUMENTED_NON_ATOMIC_H */
