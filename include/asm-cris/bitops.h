/* asm/bitops.h for Linux/CRIS
 *
 * TODO: asm versions if speed is needed
 *
 * All bit operations return 0 if the bit was cleared before the
 * operation and != 0 if it was not.
 *
 * bit 0 is the LSB of addr; bit 32 is the LSB of (addr+1).
 */

#ifndef _CRIS_BITOPS_H
#define _CRIS_BITOPS_H

/* Currently this is unsuitable for consumption outside the kernel.  */
#ifdef __KERNEL__ 

#ifndef _LINUX_BITOPS_H
#error only <linux/bitops.h> can be included directly
#endif

#include <asm/arch/bitops.h>
#include <asm/system.h>
#include <asm/atomic.h>
#include <linux/compiler.h>

/*
 * Some hacks to defeat gcc over-optimizations..
 */
struct __dummy { unsigned long a[100]; };
#define ADDR (*(struct __dummy *) addr)
#define CONST_ADDR (*(const struct __dummy *) addr)

/*
 * set_bit - Atomically set a bit in memory
 * @nr: the bit to set
 * @addr: the address to start counting from
 *
 * This function is atomic and may not be reordered.  See __set_bit()
 * if you do not require the atomic guarantees.
 * Note that @nr may be almost arbitrarily large; this function is not
 * restricted to acting on a single-word quantity.
 */

#define set_bit(nr, addr)    (void)test_and_set_bit(nr, addr)

/*
 * clear_bit - Clears a bit in memory
 * @nr: Bit to clear
 * @addr: Address to start counting from
 *
 * clear_bit() is atomic and may not be reordered.  However, it does
 * not contain a memory barrier, so if it is used for locking purposes,
 * you should call smp_mb__before_clear_bit() and/or smp_mb__after_clear_bit()
 * in order to ensure changes are visible on other processors.
 */

#define clear_bit(nr, addr)  (void)test_and_clear_bit(nr, addr)

/*
 * change_bit - Toggle a bit in memory
 * @nr: Bit to change
 * @addr: Address to start counting from
 *
 * change_bit() is atomic and may not be reordered.
 * Note that @nr may be almost arbitrarily large; this function is not
 * restricted to acting on a single-word quantity.
 */

#define change_bit(nr, addr) (void)test_and_change_bit(nr, addr)

/**
 * test_and_set_bit - Set a bit and return its old value
 * @nr: Bit to set
 * @addr: Address to count from
 *
 * This operation is atomic and cannot be reordered.  
 * It also implies a memory barrier.
 */

static inline int test_and_set_bit(int nr, volatile unsigned long *addr)
{
	unsigned int mask, retval;
	unsigned long flags;
	unsigned int *adr = (unsigned int *)addr;
	
	adr += nr >> 5;
	mask = 1 << (nr & 0x1f);
	cris_atomic_save(addr, flags);
	retval = (mask & *adr) != 0;
	*adr |= mask;
	cris_atomic_restore(addr, flags);
	return retval;
}

/*
 * clear_bit() doesn't provide any barrier for the compiler.
 */
#define smp_mb__before_clear_bit()      barrier()
#define smp_mb__after_clear_bit()       barrier()

/**
 * test_and_clear_bit - Clear a bit and return its old value
 * @nr: Bit to clear
 * @addr: Address to count from
 *
 * This operation is atomic and cannot be reordered.  
 * It also implies a memory barrier.
 */

static inline int test_and_clear_bit(int nr, volatile unsigned long *addr)
{
	unsigned int mask, retval;
	unsigned long flags;
	unsigned int *adr = (unsigned int *)addr;
	
	adr += nr >> 5;
	mask = 1 << (nr & 0x1f);
	cris_atomic_save(addr, flags);
	retval = (mask & *adr) != 0;
	*adr &= ~mask;
	cris_atomic_restore(addr, flags);
	return retval;
}

/**
 * test_and_change_bit - Change a bit and return its old value
 * @nr: Bit to change
 * @addr: Address to count from
 *
 * This operation is atomic and cannot be reordered.  
 * It also implies a memory barrier.
 */

static inline int test_and_change_bit(int nr, volatile unsigned long *addr)
{
	unsigned int mask, retval;
	unsigned long flags;
	unsigned int *adr = (unsigned int *)addr;
	adr += nr >> 5;
	mask = 1 << (nr & 0x1f);
	cris_atomic_save(addr, flags);
	retval = (mask & *adr) != 0;
	*adr ^= mask;
	cris_atomic_restore(addr, flags);
	return retval;
}

#include <asm-generic/bitops/non-atomic.h>

/*
 * Since we define it "external", it collides with the built-in
 * definition, which doesn't have the same semantics.  We don't want to
 * use -fno-builtin, so just hide the name ffs.
 */
#define ffs kernel_ffs

#include <asm-generic/bitops/fls.h>
#include <asm-generic/bitops/fls64.h>
#include <asm-generic/bitops/hweight.h>
#include <asm-generic/bitops/find.h>
#include <asm-generic/bitops/lock.h>

#include <asm-generic/bitops/ext2-non-atomic.h>

#define ext2_set_bit_atomic(l,n,a)   test_and_set_bit(n,a)
#define ext2_clear_bit_atomic(l,n,a) test_and_clear_bit(n,a)

#include <asm-generic/bitops/minix.h>
#include <asm-generic/bitops/sched.h>

#endif /* __KERNEL__ */

#endif /* _CRIS_BITOPS_H */
