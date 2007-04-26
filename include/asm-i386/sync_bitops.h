#ifndef _I386_SYNC_BITOPS_H
#define _I386_SYNC_BITOPS_H

/*
 * Copyright 1992, Linus Torvalds.
 */

/*
 * These have to be done with inline assembly: that way the bit-setting
 * is guaranteed to be atomic. All bit operations return 0 if the bit
 * was cleared before the operation and != 0 if it was not.
 *
 * bit 0 is the LSB of addr; bit 32 is the LSB of (addr+1).
 */

#define ADDR (*(volatile long *) addr)

/**
 * sync_set_bit - Atomically set a bit in memory
 * @nr: the bit to set
 * @addr: the address to start counting from
 *
 * This function is atomic and may not be reordered.  See __set_bit()
 * if you do not require the atomic guarantees.
 *
 * Note: there are no guarantees that this function will not be reordered
 * on non x86 architectures, so if you are writting portable code,
 * make sure not to rely on its reordering guarantees.
 *
 * Note that @nr may be almost arbitrarily large; this function is not
 * restricted to acting on a single-word quantity.
 */
static inline void sync_set_bit(int nr, volatile unsigned long * addr)
{
	__asm__ __volatile__("lock; btsl %1,%0"
			     :"+m" (ADDR)
			     :"Ir" (nr)
			     : "memory");
}

/**
 * sync_clear_bit - Clears a bit in memory
 * @nr: Bit to clear
 * @addr: Address to start counting from
 *
 * sync_clear_bit() is atomic and may not be reordered.  However, it does
 * not contain a memory barrier, so if it is used for locking purposes,
 * you should call smp_mb__before_clear_bit() and/or smp_mb__after_clear_bit()
 * in order to ensure changes are visible on other processors.
 */
static inline void sync_clear_bit(int nr, volatile unsigned long * addr)
{
	__asm__ __volatile__("lock; btrl %1,%0"
			     :"+m" (ADDR)
			     :"Ir" (nr)
			     : "memory");
}

/**
 * sync_change_bit - Toggle a bit in memory
 * @nr: Bit to change
 * @addr: Address to start counting from
 *
 * change_bit() is atomic and may not be reordered. It may be
 * reordered on other architectures than x86.
 * Note that @nr may be almost arbitrarily large; this function is not
 * restricted to acting on a single-word quantity.
 */
static inline void sync_change_bit(int nr, volatile unsigned long * addr)
{
	__asm__ __volatile__("lock; btcl %1,%0"
			     :"+m" (ADDR)
			     :"Ir" (nr)
			     : "memory");
}

/**
 * sync_test_and_set_bit - Set a bit and return its old value
 * @nr: Bit to set
 * @addr: Address to count from
 *
 * This operation is atomic and cannot be reordered.
 * It may be reordered on other architectures than x86.
 * It also implies a memory barrier.
 */
static inline int sync_test_and_set_bit(int nr, volatile unsigned long * addr)
{
	int oldbit;

	__asm__ __volatile__("lock; btsl %2,%1\n\tsbbl %0,%0"
			     :"=r" (oldbit),"+m" (ADDR)
			     :"Ir" (nr) : "memory");
	return oldbit;
}

/**
 * sync_test_and_clear_bit - Clear a bit and return its old value
 * @nr: Bit to clear
 * @addr: Address to count from
 *
 * This operation is atomic and cannot be reordered.
 * It can be reorderdered on other architectures other than x86.
 * It also implies a memory barrier.
 */
static inline int sync_test_and_clear_bit(int nr, volatile unsigned long * addr)
{
	int oldbit;

	__asm__ __volatile__("lock; btrl %2,%1\n\tsbbl %0,%0"
			     :"=r" (oldbit),"+m" (ADDR)
			     :"Ir" (nr) : "memory");
	return oldbit;
}

/**
 * sync_test_and_change_bit - Change a bit and return its old value
 * @nr: Bit to change
 * @addr: Address to count from
 *
 * This operation is atomic and cannot be reordered.
 * It also implies a memory barrier.
 */
static inline int sync_test_and_change_bit(int nr, volatile unsigned long* addr)
{
	int oldbit;

	__asm__ __volatile__("lock; btcl %2,%1\n\tsbbl %0,%0"
			     :"=r" (oldbit),"+m" (ADDR)
			     :"Ir" (nr) : "memory");
	return oldbit;
}

static __always_inline int sync_constant_test_bit(int nr, const volatile unsigned long *addr)
{
	return ((1UL << (nr & 31)) &
		(((const volatile unsigned int *)addr)[nr >> 5])) != 0;
}

static inline int sync_var_test_bit(int nr, const volatile unsigned long * addr)
{
	int oldbit;

	__asm__ __volatile__("btl %2,%1\n\tsbbl %0,%0"
			     :"=r" (oldbit)
			     :"m" (ADDR),"Ir" (nr));
	return oldbit;
}

#define sync_test_bit(nr,addr)			\
	(__builtin_constant_p(nr) ?		\
	 sync_constant_test_bit((nr),(addr)) :	\
	 sync_var_test_bit((nr),(addr)))

#undef ADDR

#endif /* _I386_SYNC_BITOPS_H */
