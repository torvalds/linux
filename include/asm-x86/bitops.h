#ifndef _ASM_X86_BITOPS_H
#define _ASM_X86_BITOPS_H

/*
 * Copyright 1992, Linus Torvalds.
 */

#ifndef _LINUX_BITOPS_H
#error only <linux/bitops.h> can be included directly
#endif

#include <linux/compiler.h>
#include <asm/alternative.h>

/*
 * These have to be done with inline assembly: that way the bit-setting
 * is guaranteed to be atomic. All bit operations return 0 if the bit
 * was cleared before the operation and != 0 if it was not.
 *
 * bit 0 is the LSB of addr; bit 32 is the LSB of (addr+1).
 */

#if __GNUC__ < 4 || (__GNUC__ == 4 && __GNUC_MINOR__ < 1)
/* Technically wrong, but this avoids compilation errors on some gcc
   versions. */
#define ADDR "=m" (*(volatile long *) addr)
#else
#define ADDR "+m" (*(volatile long *) addr)
#endif

/**
 * set_bit - Atomically set a bit in memory
 * @nr: the bit to set
 * @addr: the address to start counting from
 *
 * This function is atomic and may not be reordered.  See __set_bit()
 * if you do not require the atomic guarantees.
 *
 * Note: there are no guarantees that this function will not be reordered
 * on non x86 architectures, so if you are writing portable code,
 * make sure not to rely on its reordering guarantees.
 *
 * Note that @nr may be almost arbitrarily large; this function is not
 * restricted to acting on a single-word quantity.
 */
static inline void set_bit(int nr, volatile void *addr)
{
	asm volatile(LOCK_PREFIX "bts %1,%0"
		     : ADDR
		     : "Ir" (nr) : "memory");
}

/**
 * __set_bit - Set a bit in memory
 * @nr: the bit to set
 * @addr: the address to start counting from
 *
 * Unlike set_bit(), this function is non-atomic and may be reordered.
 * If it's called on the same region of memory simultaneously, the effect
 * may be that only one operation succeeds.
 */
static inline void __set_bit(int nr, volatile void *addr)
{
	asm volatile("bts %1,%0"
		     : ADDR
		     : "Ir" (nr) : "memory");
}


/**
 * clear_bit - Clears a bit in memory
 * @nr: Bit to clear
 * @addr: Address to start counting from
 *
 * clear_bit() is atomic and may not be reordered.  However, it does
 * not contain a memory barrier, so if it is used for locking purposes,
 * you should call smp_mb__before_clear_bit() and/or smp_mb__after_clear_bit()
 * in order to ensure changes are visible on other processors.
 */
static inline void clear_bit(int nr, volatile void *addr)
{
	asm volatile(LOCK_PREFIX "btr %1,%0"
		     : ADDR
		     : "Ir" (nr));
}

/*
 * clear_bit_unlock - Clears a bit in memory
 * @nr: Bit to clear
 * @addr: Address to start counting from
 *
 * clear_bit() is atomic and implies release semantics before the memory
 * operation. It can be used for an unlock.
 */
static inline void clear_bit_unlock(unsigned nr, volatile void *addr)
{
	barrier();
	clear_bit(nr, addr);
}

static inline void __clear_bit(int nr, volatile void *addr)
{
	asm volatile("btr %1,%0" : ADDR : "Ir" (nr));
}

/*
 * __clear_bit_unlock - Clears a bit in memory
 * @nr: Bit to clear
 * @addr: Address to start counting from
 *
 * __clear_bit() is non-atomic and implies release semantics before the memory
 * operation. It can be used for an unlock if no other CPUs can concurrently
 * modify other bits in the word.
 *
 * No memory barrier is required here, because x86 cannot reorder stores past
 * older loads. Same principle as spin_unlock.
 */
static inline void __clear_bit_unlock(unsigned nr, volatile void *addr)
{
	barrier();
	__clear_bit(nr, addr);
}

#define smp_mb__before_clear_bit()	barrier()
#define smp_mb__after_clear_bit()	barrier()

/**
 * __change_bit - Toggle a bit in memory
 * @nr: the bit to change
 * @addr: the address to start counting from
 *
 * Unlike change_bit(), this function is non-atomic and may be reordered.
 * If it's called on the same region of memory simultaneously, the effect
 * may be that only one operation succeeds.
 */
static inline void __change_bit(int nr, volatile void *addr)
{
	asm volatile("btc %1,%0" : ADDR : "Ir" (nr));
}

/**
 * change_bit - Toggle a bit in memory
 * @nr: Bit to change
 * @addr: Address to start counting from
 *
 * change_bit() is atomic and may not be reordered.
 * Note that @nr may be almost arbitrarily large; this function is not
 * restricted to acting on a single-word quantity.
 */
static inline void change_bit(int nr, volatile void *addr)
{
	asm volatile(LOCK_PREFIX "btc %1,%0"
		     : ADDR : "Ir" (nr));
}

/**
 * test_and_set_bit - Set a bit and return its old value
 * @nr: Bit to set
 * @addr: Address to count from
 *
 * This operation is atomic and cannot be reordered.
 * It also implies a memory barrier.
 */
static inline int test_and_set_bit(int nr, volatile void *addr)
{
	int oldbit;

	asm volatile(LOCK_PREFIX "bts %2,%1\n\t"
		     "sbb %0,%0"
		     : "=r" (oldbit), ADDR
		     : "Ir" (nr) : "memory");

	return oldbit;
}

/**
 * test_and_set_bit_lock - Set a bit and return its old value for lock
 * @nr: Bit to set
 * @addr: Address to count from
 *
 * This is the same as test_and_set_bit on x86.
 */
static inline int test_and_set_bit_lock(int nr, volatile void *addr)
{
	return test_and_set_bit(nr, addr);
}

/**
 * __test_and_set_bit - Set a bit and return its old value
 * @nr: Bit to set
 * @addr: Address to count from
 *
 * This operation is non-atomic and can be reordered.
 * If two examples of this operation race, one can appear to succeed
 * but actually fail.  You must protect multiple accesses with a lock.
 */
static inline int __test_and_set_bit(int nr, volatile void *addr)
{
	int oldbit;

	asm("bts %2,%1\n\t"
	    "sbb %0,%0"
	    : "=r" (oldbit), ADDR
	    : "Ir" (nr));
	return oldbit;
}

/**
 * test_and_clear_bit - Clear a bit and return its old value
 * @nr: Bit to clear
 * @addr: Address to count from
 *
 * This operation is atomic and cannot be reordered.
 * It also implies a memory barrier.
 */
static inline int test_and_clear_bit(int nr, volatile void *addr)
{
	int oldbit;

	asm volatile(LOCK_PREFIX "btr %2,%1\n\t"
		     "sbb %0,%0"
		     : "=r" (oldbit), ADDR
		     : "Ir" (nr) : "memory");

	return oldbit;
}

/**
 * __test_and_clear_bit - Clear a bit and return its old value
 * @nr: Bit to clear
 * @addr: Address to count from
 *
 * This operation is non-atomic and can be reordered.
 * If two examples of this operation race, one can appear to succeed
 * but actually fail.  You must protect multiple accesses with a lock.
 */
static inline int __test_and_clear_bit(int nr, volatile void *addr)
{
	int oldbit;

	asm volatile("btr %2,%1\n\t"
		     "sbb %0,%0"
		     : "=r" (oldbit), ADDR
		     : "Ir" (nr));
	return oldbit;
}

/* WARNING: non atomic and it can be reordered! */
static inline int __test_and_change_bit(int nr, volatile void *addr)
{
	int oldbit;

	asm volatile("btc %2,%1\n\t"
		     "sbb %0,%0"
		     : "=r" (oldbit), ADDR
		     : "Ir" (nr) : "memory");

	return oldbit;
}

/**
 * test_and_change_bit - Change a bit and return its old value
 * @nr: Bit to change
 * @addr: Address to count from
 *
 * This operation is atomic and cannot be reordered.
 * It also implies a memory barrier.
 */
static inline int test_and_change_bit(int nr, volatile void *addr)
{
	int oldbit;

	asm volatile(LOCK_PREFIX "btc %2,%1\n\t"
		     "sbb %0,%0"
		     : "=r" (oldbit), ADDR
		     : "Ir" (nr) : "memory");

	return oldbit;
}

static inline int constant_test_bit(int nr, const volatile void *addr)
{
	return ((1UL << (nr % BITS_PER_LONG)) &
		(((unsigned long *)addr)[nr / BITS_PER_LONG])) != 0;
}

static inline int variable_test_bit(int nr, volatile const void *addr)
{
	int oldbit;

	asm volatile("bt %2,%1\n\t"
		     "sbb %0,%0"
		     : "=r" (oldbit)
		     : "m" (*(unsigned long *)addr), "Ir" (nr));

	return oldbit;
}

#if 0 /* Fool kernel-doc since it doesn't do macros yet */
/**
 * test_bit - Determine whether a bit is set
 * @nr: bit number to test
 * @addr: Address to start counting from
 */
static int test_bit(int nr, const volatile unsigned long *addr);
#endif

#define test_bit(nr,addr)			\
	(__builtin_constant_p(nr) ?		\
	 constant_test_bit((nr),(addr)) :	\
	 variable_test_bit((nr),(addr)))

#undef ADDR

#ifdef CONFIG_X86_32
# include "bitops_32.h"
#else
# include "bitops_64.h"
#endif

#endif	/* _ASM_X86_BITOPS_H */
