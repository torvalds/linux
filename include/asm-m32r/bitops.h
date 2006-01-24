#ifndef _ASM_M32R_BITOPS_H
#define _ASM_M32R_BITOPS_H

/*
 *  linux/include/asm-m32r/bitops.h
 *
 *  Copyright 1992, Linus Torvalds.
 *
 *  M32R version:
 *    Copyright (C) 2001, 2002  Hitoshi Yamamoto
 *    Copyright (C) 2004  Hirokazu Takata <takata at linux-m32r.org>
 */

#include <linux/config.h>
#include <linux/compiler.h>
#include <asm/assembler.h>
#include <asm/system.h>
#include <asm/byteorder.h>
#include <asm/types.h>

/*
 * These have to be done with inline assembly: that way the bit-setting
 * is guaranteed to be atomic. All bit operations return 0 if the bit
 * was cleared before the operation and != 0 if it was not.
 *
 * bit 0 is the LSB of addr; bit 32 is the LSB of (addr+1).
 */

/**
 * set_bit - Atomically set a bit in memory
 * @nr: the bit to set
 * @addr: the address to start counting from
 *
 * This function is atomic and may not be reordered.  See __set_bit()
 * if you do not require the atomic guarantees.
 * Note that @nr may be almost arbitrarily large; this function is not
 * restricted to acting on a single-word quantity.
 */
static __inline__ void set_bit(int nr, volatile void * addr)
{
	__u32 mask;
	volatile __u32 *a = addr;
	unsigned long flags;
	unsigned long tmp;

	a += (nr >> 5);
	mask = (1 << (nr & 0x1F));

	local_irq_save(flags);
	__asm__ __volatile__ (
		DCACHE_CLEAR("%0", "r6", "%1")
		M32R_LOCK" %0, @%1;		\n\t"
		"or	%0, %2;			\n\t"
		M32R_UNLOCK" %0, @%1;		\n\t"
		: "=&r" (tmp)
		: "r" (a), "r" (mask)
		: "memory"
#ifdef CONFIG_CHIP_M32700_TS1
		, "r6"
#endif	/* CONFIG_CHIP_M32700_TS1 */
	);
	local_irq_restore(flags);
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
static __inline__ void __set_bit(int nr, volatile void * addr)
{
	__u32 mask;
	volatile __u32 *a = addr;

	a += (nr >> 5);
	mask = (1 << (nr & 0x1F));
	*a |= mask;
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
static __inline__ void clear_bit(int nr, volatile void * addr)
{
	__u32 mask;
	volatile __u32 *a = addr;
	unsigned long flags;
	unsigned long tmp;

	a += (nr >> 5);
	mask = (1 << (nr & 0x1F));

	local_irq_save(flags);

	__asm__ __volatile__ (
		DCACHE_CLEAR("%0", "r6", "%1")
		M32R_LOCK" %0, @%1;		\n\t"
		"and	%0, %2;			\n\t"
		M32R_UNLOCK" %0, @%1;		\n\t"
		: "=&r" (tmp)
		: "r" (a), "r" (~mask)
		: "memory"
#ifdef CONFIG_CHIP_M32700_TS1
		, "r6"
#endif	/* CONFIG_CHIP_M32700_TS1 */
	);
	local_irq_restore(flags);
}

static __inline__ void __clear_bit(int nr, volatile unsigned long * addr)
{
	unsigned long mask;
	volatile unsigned long *a = addr;

	a += (nr >> 5);
	mask = (1 << (nr & 0x1F));
	*a &= ~mask;
}

#define smp_mb__before_clear_bit()	barrier()
#define smp_mb__after_clear_bit()	barrier()

/**
 * __change_bit - Toggle a bit in memory
 * @nr: the bit to set
 * @addr: the address to start counting from
 *
 * Unlike change_bit(), this function is non-atomic and may be reordered.
 * If it's called on the same region of memory simultaneously, the effect
 * may be that only one operation succeeds.
 */
static __inline__ void __change_bit(int nr, volatile void * addr)
{
	__u32 mask;
	volatile __u32 *a = addr;

	a += (nr >> 5);
	mask = (1 << (nr & 0x1F));
	*a ^= mask;
}

/**
 * change_bit - Toggle a bit in memory
 * @nr: Bit to clear
 * @addr: Address to start counting from
 *
 * change_bit() is atomic and may not be reordered.
 * Note that @nr may be almost arbitrarily large; this function is not
 * restricted to acting on a single-word quantity.
 */
static __inline__ void change_bit(int nr, volatile void * addr)
{
	__u32  mask;
	volatile __u32  *a = addr;
	unsigned long flags;
	unsigned long tmp;

	a += (nr >> 5);
	mask = (1 << (nr & 0x1F));

	local_irq_save(flags);
	__asm__ __volatile__ (
		DCACHE_CLEAR("%0", "r6", "%1")
		M32R_LOCK" %0, @%1;		\n\t"
		"xor	%0, %2;			\n\t"
		M32R_UNLOCK" %0, @%1;		\n\t"
		: "=&r" (tmp)
		: "r" (a), "r" (mask)
		: "memory"
#ifdef CONFIG_CHIP_M32700_TS1
		, "r6"
#endif	/* CONFIG_CHIP_M32700_TS1 */
	);
	local_irq_restore(flags);
}

/**
 * test_and_set_bit - Set a bit and return its old value
 * @nr: Bit to set
 * @addr: Address to count from
 *
 * This operation is atomic and cannot be reordered.
 * It also implies a memory barrier.
 */
static __inline__ int test_and_set_bit(int nr, volatile void * addr)
{
	__u32 mask, oldbit;
	volatile __u32 *a = addr;
	unsigned long flags;
	unsigned long tmp;

	a += (nr >> 5);
	mask = (1 << (nr & 0x1F));

	local_irq_save(flags);
	__asm__ __volatile__ (
		DCACHE_CLEAR("%0", "%1", "%2")
		M32R_LOCK" %0, @%2;		\n\t"
		"mv	%1, %0;			\n\t"
		"and	%0, %3;			\n\t"
		"or	%1, %3;			\n\t"
		M32R_UNLOCK" %1, @%2;		\n\t"
		: "=&r" (oldbit), "=&r" (tmp)
		: "r" (a), "r" (mask)
		: "memory"
	);
	local_irq_restore(flags);

	return (oldbit != 0);
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
static __inline__ int __test_and_set_bit(int nr, volatile void * addr)
{
	__u32 mask, oldbit;
	volatile __u32 *a = addr;

	a += (nr >> 5);
	mask = (1 << (nr & 0x1F));
	oldbit = (*a & mask);
	*a |= mask;

	return (oldbit != 0);
}

/**
 * test_and_clear_bit - Clear a bit and return its old value
 * @nr: Bit to set
 * @addr: Address to count from
 *
 * This operation is atomic and cannot be reordered.
 * It also implies a memory barrier.
 */
static __inline__ int test_and_clear_bit(int nr, volatile void * addr)
{
	__u32 mask, oldbit;
	volatile __u32 *a = addr;
	unsigned long flags;
	unsigned long tmp;

	a += (nr >> 5);
	mask = (1 << (nr & 0x1F));

	local_irq_save(flags);

	__asm__ __volatile__ (
		DCACHE_CLEAR("%0", "%1", "%3")
		M32R_LOCK" %0, @%3;		\n\t"
		"mv	%1, %0;			\n\t"
		"and	%0, %2;			\n\t"
		"not	%2, %2;			\n\t"
		"and	%1, %2;			\n\t"
		M32R_UNLOCK" %1, @%3;		\n\t"
		: "=&r" (oldbit), "=&r" (tmp), "+r" (mask)
		: "r" (a)
		: "memory"
	);
	local_irq_restore(flags);

	return (oldbit != 0);
}

/**
 * __test_and_clear_bit - Clear a bit and return its old value
 * @nr: Bit to set
 * @addr: Address to count from
 *
 * This operation is non-atomic and can be reordered.
 * If two examples of this operation race, one can appear to succeed
 * but actually fail.  You must protect multiple accesses with a lock.
 */
static __inline__ int __test_and_clear_bit(int nr, volatile void * addr)
{
	__u32 mask, oldbit;
	volatile __u32 *a = addr;

	a += (nr >> 5);
	mask = (1 << (nr & 0x1F));
	oldbit = (*a & mask);
	*a &= ~mask;

	return (oldbit != 0);
}

/* WARNING: non atomic and it can be reordered! */
static __inline__ int __test_and_change_bit(int nr, volatile void * addr)
{
	__u32 mask, oldbit;
	volatile __u32 *a = addr;

	a += (nr >> 5);
	mask = (1 << (nr & 0x1F));
	oldbit = (*a & mask);
	*a ^= mask;

	return (oldbit != 0);
}

/**
 * test_and_change_bit - Change a bit and return its old value
 * @nr: Bit to set
 * @addr: Address to count from
 *
 * This operation is atomic and cannot be reordered.
 * It also implies a memory barrier.
 */
static __inline__ int test_and_change_bit(int nr, volatile void * addr)
{
	__u32 mask, oldbit;
	volatile __u32 *a = addr;
	unsigned long flags;
	unsigned long tmp;

	a += (nr >> 5);
	mask = (1 << (nr & 0x1F));

	local_irq_save(flags);
	__asm__ __volatile__ (
		DCACHE_CLEAR("%0", "%1", "%2")
		M32R_LOCK" %0, @%2;		\n\t"
		"mv	%1, %0;			\n\t"
		"and	%0, %3;			\n\t"
		"xor	%1, %3;			\n\t"
		M32R_UNLOCK" %1, @%2;		\n\t"
		: "=&r" (oldbit), "=&r" (tmp)
		: "r" (a), "r" (mask)
		: "memory"
	);
	local_irq_restore(flags);

	return (oldbit != 0);
}

/**
 * test_bit - Determine whether a bit is set
 * @nr: bit number to test
 * @addr: Address to start counting from
 */
static __inline__ int test_bit(int nr, const volatile void * addr)
{
	__u32 mask;
	const volatile __u32 *a = addr;

	a += (nr >> 5);
	mask = (1 << (nr & 0x1F));

	return ((*a & mask) != 0);
}

/**
 * ffz - find first zero in word.
 * @word: The word to search
 *
 * Undefined if no zero exists, so code should check against ~0UL first.
 */
static __inline__ unsigned long ffz(unsigned long word)
{
	int k;

	word = ~word;
	k = 0;
	if (!(word & 0x0000ffff)) { k += 16; word >>= 16; }
	if (!(word & 0x000000ff)) { k += 8; word >>= 8; }
	if (!(word & 0x0000000f)) { k += 4; word >>= 4; }
	if (!(word & 0x00000003)) { k += 2; word >>= 2; }
	if (!(word & 0x00000001)) { k += 1; }

	return k;
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

/**
 * find_next_zero_bit - find the first zero bit in a memory region
 * @addr: The address to base the search on
 * @offset: The bitnumber to start searching at
 * @size: The maximum size to search
 */
static __inline__ int find_next_zero_bit(const unsigned long *addr,
					 int size, int offset)
{
	const unsigned long *p = addr + (offset >> 5);
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
 * __ffs - find first bit in word.
 * @word: The word to search
 *
 * Undefined if no bit exists, so code should check against 0 first.
 */
static __inline__ unsigned long __ffs(unsigned long word)
{
	int k = 0;

	if (!(word & 0x0000ffff)) { k += 16; word >>= 16; }
	if (!(word & 0x000000ff)) { k += 8; word >>= 8; }
	if (!(word & 0x0000000f)) { k += 4; word >>= 4; }
	if (!(word & 0x00000003)) { k += 2; word >>= 2; }
	if (!(word & 0x00000001)) { k += 1;}

	return k;
}

/*
 * fls: find last bit set.
 */
#define fls(x) generic_fls(x)
#define fls64(x)   generic_fls64(x)

#ifdef __KERNEL__

/*
 * Every architecture must define this function. It's the fastest
 * way of searching a 140-bit bitmap where the first 100 bits are
 * unlikely to be set. It's guaranteed that at least one of the 140
 * bits is cleared.
 */
static inline int sched_find_first_bit(unsigned long *b)
{
	if (unlikely(b[0]))
		return __ffs(b[0]);
	if (unlikely(b[1]))
		return __ffs(b[1]) + 32;
	if (unlikely(b[2]))
		return __ffs(b[2]) + 64;
	if (b[3])
		return __ffs(b[3]) + 96;
	return __ffs(b[4]) + 128;
}

/**
 * find_next_bit - find the first set bit in a memory region
 * @addr: The address to base the search on
 * @offset: The bitnumber to start searching at
 * @size: The maximum size to search
 */
static inline unsigned long find_next_bit(const unsigned long *addr,
	unsigned long size, unsigned long offset)
{
	unsigned int *p = ((unsigned int *) addr) + (offset >> 5);
	unsigned int result = offset & ~31UL;
	unsigned int tmp;

	if (offset >= size)
		return size;
	size -= result;
	offset &= 31UL;
	if (offset) {
		tmp = *p++;
		tmp &= ~0UL << offset;
		if (size < 32)
			goto found_first;
		if (tmp)
			goto found_middle;
		size -= 32;
		result += 32;
	}
	while (size >= 32) {
		if ((tmp = *p++) != 0)
			goto found_middle;
		result += 32;
		size -= 32;
	}
	if (!size)
		return result;
	tmp = *p;

found_first:
	tmp &= ~0UL >> (32 - size);
	if (tmp == 0UL)        /* Are any bits set? */
		return result + size; /* Nope. */
found_middle:
	return result + __ffs(tmp);
}

/**
 * find_first_bit - find the first set bit in a memory region
 * @addr: The address to start the search at
 * @size: The maximum size to search
 *
 * Returns the bit-number of the first set bit, not the number of the byte
 * containing a bit.
 */
#define find_first_bit(addr, size) \
	find_next_bit((addr), (size), 0)

/**
 * ffs - find first bit set
 * @x: the word to search
 *
 * This is defined the same way as
 * the libc and compiler builtin ffs routines, therefore
 * differs in spirit from the above ffz (man ffs).
 */
#define ffs(x)	generic_ffs(x)

/**
 * hweightN - returns the hamming weight of a N-bit word
 * @x: the word to weigh
 *
 * The Hamming Weight of a number is the total number of bits set in it.
 */

#define hweight32(x)	generic_hweight32(x)
#define hweight16(x)	generic_hweight16(x)
#define hweight8(x)	generic_hweight8(x)

#endif /* __KERNEL__ */

#ifdef __KERNEL__

/*
 * ext2_XXXX function
 * orig: include/asm-sh/bitops.h
 */

#ifdef __LITTLE_ENDIAN__
#define ext2_set_bit			test_and_set_bit
#define ext2_clear_bit			__test_and_clear_bit
#define ext2_test_bit			test_bit
#define ext2_find_first_zero_bit	find_first_zero_bit
#define ext2_find_next_zero_bit		find_next_zero_bit
#else
static inline int ext2_set_bit(int nr, volatile void * addr)
{
	__u8 mask, oldbit;
	volatile __u8 *a = addr;

	a += (nr >> 3);
	mask = (1 << (nr & 0x07));
	oldbit = (*a & mask);
	*a |= mask;

	return (oldbit != 0);
}

static inline int ext2_clear_bit(int nr, volatile void * addr)
{
	__u8 mask, oldbit;
	volatile __u8 *a = addr;

	a += (nr >> 3);
	mask = (1 << (nr & 0x07));
	oldbit = (*a & mask);
	*a &= ~mask;

	return (oldbit != 0);
}

static inline int ext2_test_bit(int nr, const volatile void * addr)
{
	__u32 mask;
	const volatile __u8 *a = addr;

	a += (nr >> 3);
	mask = (1 << (nr & 0x07));

	return ((mask & *a) != 0);
}

#define ext2_find_first_zero_bit(addr, size) \
	ext2_find_next_zero_bit((addr), (size), 0)

static inline unsigned long ext2_find_next_zero_bit(void *addr,
	unsigned long size, unsigned long offset)
{
	unsigned long *p = ((unsigned long *) addr) + (offset >> 5);
	unsigned long result = offset & ~31UL;
	unsigned long tmp;

	if (offset >= size)
		return size;
	size -= result;
	offset &= 31UL;
	if(offset) {
		/* We hold the little endian value in tmp, but then the
		 * shift is illegal. So we could keep a big endian value
		 * in tmp, like this:
		 *
		 * tmp = __swab32(*(p++));
		 * tmp |= ~0UL >> (32-offset);
		 *
		 * but this would decrease preformance, so we change the
		 * shift:
		 */
		tmp = *(p++);
		tmp |= __swab32(~0UL >> (32-offset));
		if(size < 32)
			goto found_first;
		if(~tmp)
			goto found_middle;
		size -= 32;
		result += 32;
	}
	while(size & ~31UL) {
		if(~(tmp = *(p++)))
			goto found_middle;
		result += 32;
		size -= 32;
	}
	if(!size)
		return result;
	tmp = *p;

found_first:
	/* tmp is little endian, so we would have to swab the shift,
	 * see above. But then we have to swab tmp below for ffz, so
	 * we might as well do this here.
	 */
	return result + ffz(__swab32(tmp) | (~0UL << size));
found_middle:
	return result + ffz(__swab32(tmp));
}
#endif

#define ext2_set_bit_atomic(lock, nr, addr)		\
	({						\
		int ret;				\
		spin_lock(lock);			\
		ret = ext2_set_bit((nr), (addr));	\
		spin_unlock(lock);			\
		ret;					\
	})

#define ext2_clear_bit_atomic(lock, nr, addr)		\
	({						\
		int ret;				\
		spin_lock(lock);			\
		ret = ext2_clear_bit((nr), (addr));	\
		spin_unlock(lock);			\
		ret;					\
	})

/* Bitmap functions for the minix filesystem.  */
#define minix_test_and_set_bit(nr,addr)		__test_and_set_bit(nr,addr)
#define minix_set_bit(nr,addr)			__set_bit(nr,addr)
#define minix_test_and_clear_bit(nr,addr)	__test_and_clear_bit(nr,addr)
#define minix_test_bit(nr,addr) test_bit(nr,addr)
#define minix_find_first_zero_bit(addr,size)	find_first_zero_bit(addr,size)

#endif /* __KERNEL__ */

#endif /* _ASM_M32R_BITOPS_H */
