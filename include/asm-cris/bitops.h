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

#define __set_bit(nr, addr)    (void)__test_and_set_bit(nr, addr)

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

#define __clear_bit(nr, addr)  (void)__test_and_clear_bit(nr, addr)

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

/*
 * __change_bit - Toggle a bit in memory
 * @nr: the bit to change
 * @addr: the address to start counting from
 *
 * Unlike change_bit(), this function is non-atomic and may be reordered.
 * If it's called on the same region of memory simultaneously, the effect
 * may be that only one operation succeeds.
 */

#define __change_bit(nr, addr) (void)__test_and_change_bit(nr, addr)

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
	local_irq_restore(flags);
	return retval;
}

static inline int __test_and_set_bit(int nr, volatile unsigned long *addr)
{
	unsigned int mask, retval;
	unsigned int *adr = (unsigned int *)addr;
	
	adr += nr >> 5;
	mask = 1 << (nr & 0x1f);
	retval = (mask & *adr) != 0;
	*adr |= mask;
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
 * __test_and_clear_bit - Clear a bit and return its old value
 * @nr: Bit to clear
 * @addr: Address to count from
 *
 * This operation is non-atomic and can be reordered.  
 * If two examples of this operation race, one can appear to succeed
 * but actually fail.  You must protect multiple accesses with a lock.
 */

static inline int __test_and_clear_bit(int nr, volatile unsigned long *addr)
{
	unsigned int mask, retval;
	unsigned int *adr = (unsigned int *)addr;
	
	adr += nr >> 5;
	mask = 1 << (nr & 0x1f);
	retval = (mask & *adr) != 0;
	*adr &= ~mask;
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

/* WARNING: non atomic and it can be reordered! */

static inline int __test_and_change_bit(int nr, volatile unsigned long *addr)
{
	unsigned int mask, retval;
	unsigned int *adr = (unsigned int *)addr;

	adr += nr >> 5;
	mask = 1 << (nr & 0x1f);
	retval = (mask & *adr) != 0;
	*adr ^= mask;

	return retval;
}

/**
 * test_bit - Determine whether a bit is set
 * @nr: bit number to test
 * @addr: Address to start counting from
 *
 * This routine doesn't need to be atomic.
 */

static inline int test_bit(int nr, const volatile unsigned long *addr)
{
	unsigned int mask;
	unsigned int *adr = (unsigned int *)addr;
	
	adr += nr >> 5;
	mask = 1 << (nr & 0x1f);
	return ((mask & *adr) != 0);
}

/*
 * Find-bit routines..
 */

/*
 * Since we define it "external", it collides with the built-in
 * definition, which doesn't have the same semantics.  We don't want to
 * use -fno-builtin, so just hide the name ffs.
 */
#define ffs kernel_ffs

/*
 * fls: find last bit set.
 */

#define fls(x) generic_fls(x)
#define fls64(x)   generic_fls64(x)

/*
 * hweightN - returns the hamming weight of a N-bit word
 * @x: the word to weigh
 *
 * The Hamming Weight of a number is the total number of bits set in it.
 */

#define hweight32(x) generic_hweight32(x)
#define hweight16(x) generic_hweight16(x)
#define hweight8(x) generic_hweight8(x)

/**
 * find_next_zero_bit - find the first zero bit in a memory region
 * @addr: The address to base the search on
 * @offset: The bitnumber to start searching at
 * @size: The maximum size to search
 */
static inline int find_next_zero_bit (const unsigned long * addr, int size, int offset)
{
	unsigned long *p = ((unsigned long *) addr) + (offset >> 5);
	unsigned long result = offset & ~31UL;
	unsigned long tmp;
	
	if (offset >= size)
		return size;
	size -= result;
	offset &= 31UL;
	if (offset) {
		tmp = *(p++);
		tmp |= ~0UL >> (32-offset);
		if (size < 32)
			goto found_first;
		if (~tmp)
			goto found_middle;
		size -= 32;
		result += 32;
	}
	while (size & ~31UL) {
		if (~(tmp = *(p++)))
			goto found_middle;
		result += 32;
		size -= 32;
	}
	if (!size)
		return result;
	tmp = *p;
	
 found_first:
	tmp |= ~0UL << size;
 found_middle:
	return result + ffz(tmp);
}

/**
 * find_next_bit - find the first set bit in a memory region
 * @addr: The address to base the search on
 * @offset: The bitnumber to start searching at
 * @size: The maximum size to search
 */
static __inline__ int find_next_bit(const unsigned long *addr, int size, int offset)
{
	unsigned long *p = ((unsigned long *) addr) + (offset >> 5);
        unsigned long result = offset & ~31UL;
        unsigned long tmp;

        if (offset >= size)
                return size;
        size -= result;
        offset &= 31UL;
        if (offset) {
                tmp = *(p++);
                tmp &= (~0UL << offset);
                if (size < 32)
                        goto found_first;
                if (tmp)
                        goto found_middle;
                size -= 32;
                result += 32;
        }
        while (size & ~31UL) {
                if ((tmp = *(p++)))
                        goto found_middle;
                result += 32;
                size -= 32;
        }
        if (!size)
                return result;
        tmp = *p;

found_first:
        tmp &= (~0UL >> (32 - size));
        if (tmp == 0UL)        /* Are any bits set? */
                return result + size; /* Nope. */
found_middle:
        return result + __ffs(tmp);
}

/**
 * find_first_zero_bit - find the first zero bit in a memory region
 * @addr: The address to start the search at
 * @size: The maximum size to search
 *
 * Returns the bit-number of the first zero bit, not the number of the byte
 * containing a bit.
 */

#define find_first_zero_bit(addr, size) \
        find_next_zero_bit((addr), (size), 0)
#define find_first_bit(addr, size) \
        find_next_bit((addr), (size), 0)

#define ext2_set_bit                 test_and_set_bit
#define ext2_set_bit_atomic(l,n,a)   test_and_set_bit(n,a)
#define ext2_clear_bit               test_and_clear_bit
#define ext2_clear_bit_atomic(l,n,a) test_and_clear_bit(n,a)
#define ext2_test_bit                test_bit
#define ext2_find_first_zero_bit     find_first_zero_bit
#define ext2_find_next_zero_bit      find_next_zero_bit

/* Bitmap functions for the minix filesystem.  */
#define minix_set_bit(nr,addr) test_and_set_bit(nr,addr)
#define minix_clear_bit(nr,addr) test_and_clear_bit(nr,addr)
#define minix_test_bit(nr,addr) test_bit(nr,addr)
#define minix_find_first_zero_bit(addr,size) find_first_zero_bit(addr,size)

static inline int sched_find_first_bit(const unsigned long *b)
{
	if (unlikely(b[0]))
		return __ffs(b[0]);
	if (unlikely(b[1]))
		return __ffs(b[1]) + 32;
	if (unlikely(b[2]))
		return __ffs(b[2]) + 64;
	if (unlikely(b[3]))
		return __ffs(b[3]) + 96;
	if (b[4])
		return __ffs(b[4]) + 128;
	return __ffs(b[5]) + 32 + 128;
}

#endif /* __KERNEL__ */

#endif /* _CRIS_BITOPS_H */
