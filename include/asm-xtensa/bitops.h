/*
 * include/asm-xtensa/bitops.h
 *
 * Atomic operations that C can't guarantee us.Useful for resource counting etc.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001 - 2005 Tensilica Inc.
 */

#ifndef _XTENSA_BITOPS_H
#define _XTENSA_BITOPS_H

#ifdef __KERNEL__

#include <asm/processor.h>
#include <asm/byteorder.h>
#include <asm/system.h>

#ifdef CONFIG_SMP
# error SMP not supported on this architecture
#endif

static __inline__ void set_bit(int nr, volatile void * addr)
{
	unsigned long mask = 1 << (nr & 0x1f);
	unsigned long *a = ((unsigned long *)addr) + (nr >> 5);
	unsigned long flags;

	local_irq_save(flags);
	*a |= mask;
	local_irq_restore(flags);
}

static __inline__ void __set_bit(int nr, volatile unsigned long * addr)
{
	unsigned long mask = 1 << (nr & 0x1f);
	unsigned long *a = ((unsigned long *)addr) + (nr >> 5);

	*a |= mask;
}

static __inline__ void clear_bit(int nr, volatile void * addr)
{
	unsigned long mask = 1 << (nr & 0x1f);
	unsigned long *a = ((unsigned long *)addr) + (nr >> 5);
	unsigned long flags;

	local_irq_save(flags);
	*a &= ~mask;
	local_irq_restore(flags);
}

static __inline__ void __clear_bit(int nr, volatile unsigned long *addr)
{
	unsigned long mask = 1 << (nr & 0x1f);
	unsigned long *a = ((unsigned long *)addr) + (nr >> 5);

	*a &= ~mask;
}

/*
 * clear_bit() doesn't provide any barrier for the compiler.
 */

#define smp_mb__before_clear_bit()	barrier()
#define smp_mb__after_clear_bit()	barrier()

static __inline__ void change_bit(int nr, volatile void * addr)
{
	unsigned long mask = 1 << (nr & 0x1f);
	unsigned long *a = ((unsigned long *)addr) + (nr >> 5);
	unsigned long flags;

	local_irq_save(flags);
	*a ^= mask;
	local_irq_restore(flags);
}

static __inline__ void __change_bit(int nr, volatile void * addr)
{
	unsigned long mask = 1 << (nr & 0x1f);
	unsigned long *a = ((unsigned long *)addr) + (nr >> 5);

	*a ^= mask;
}

static __inline__ int test_and_set_bit(int nr, volatile void * addr)
{
  	unsigned long retval;
	unsigned long mask = 1 << (nr & 0x1f);
	unsigned long *a = ((unsigned long *)addr) + (nr >> 5);
	unsigned long flags;

	local_irq_save(flags);
	retval = (mask & *a) != 0;
	*a |= mask;
	local_irq_restore(flags);

	return retval;
}

static __inline__ int __test_and_set_bit(int nr, volatile void * addr)
{
  	unsigned long retval;
	unsigned long mask = 1 << (nr & 0x1f);
	unsigned long *a = ((unsigned long *)addr) + (nr >> 5);

	retval = (mask & *a) != 0;
	*a |= mask;

	return retval;
}

static __inline__ int test_and_clear_bit(int nr, volatile void * addr)
{
  	unsigned long retval;
	unsigned long mask = 1 << (nr & 0x1f);
	unsigned long *a = ((unsigned long *)addr) + (nr >> 5);
	unsigned long flags;

	local_irq_save(flags);
	retval = (mask & *a) != 0;
	*a &= ~mask;
	local_irq_restore(flags);

	return retval;
}

static __inline__ int __test_and_clear_bit(int nr, volatile void * addr)
{
	unsigned long mask = 1 << (nr & 0x1f);
	unsigned long *a = ((unsigned long *)addr) + (nr >> 5);
  	unsigned long old = *a;

	*a = old & ~mask;
	return (old & mask) != 0;
}

static __inline__ int test_and_change_bit(int nr, volatile void * addr)
{
  	unsigned long retval;
	unsigned long mask = 1 << (nr & 0x1f);
	unsigned long *a = ((unsigned long *)addr) + (nr >> 5);
	unsigned long flags;

	local_irq_save(flags);

	retval = (mask & *a) != 0;
	*a ^= mask;
	local_irq_restore(flags);

	return retval;
}

/*
 * non-atomic version; can be reordered
 */

static __inline__ int __test_and_change_bit(int nr, volatile void *addr)
{
	unsigned long mask = 1 << (nr & 0x1f);
	unsigned long *a = ((unsigned long *)addr) + (nr >> 5);
	unsigned long old = *a;

	*a = old ^ mask;
	return (old & mask) != 0;
}

static __inline__ int test_bit(int nr, const volatile void *addr)
{
	return 1UL & (((const volatile unsigned int *)addr)[nr>>5] >> (nr&31));
}

#if XCHAL_HAVE_NSA

static __inline__ int __cntlz (unsigned long x)
{
	int lz;
	asm ("nsau %0, %1" : "=r" (lz) : "r" (x));
	return 31 - lz;
}

#else

static __inline__ int __cntlz (unsigned long x)
{
	unsigned long sum, x1, x2, x4, x8, x16;
	x1  = x & 0xAAAAAAAA;
	x2  = x & 0xCCCCCCCC;
	x4  = x & 0xF0F0F0F0;
	x8  = x & 0xFF00FF00;
	x16 = x & 0xFFFF0000;
	sum = x2 ? 2 : 0;
	sum += (x16 != 0) * 16;
	sum += (x8 != 0) * 8;
	sum += (x4 != 0) * 4;
	sum += (x1 != 0);

	return sum;
}

#endif

/*
 * ffz: Find first zero in word. Undefined if no zero exists.
 * bit 0 is the LSB of addr; bit 32 is the LSB of (addr+1).
 */

static __inline__ int ffz(unsigned long x)
{
	if ((x = ~x) == 0)
		return 32;
	return __cntlz(x & -x);
}

/*
 * __ffs: Find first bit set in word. Return 0 for bit 0
 */

static __inline__ int __ffs(unsigned long x)
{
	return __cntlz(x & -x);
}

/*
 * ffs: Find first bit set in word. This is defined the same way as
 * the libc and compiler builtin ffs routines, therefore
 * differs in spirit from the above ffz (man ffs).
 */

static __inline__ int ffs(unsigned long x)
{
	return __cntlz(x & -x) + 1;
}

/*
 * fls: Find last (most-significant) bit set in word.
 * Note fls(0) = 0, fls(1) = 1, fls(0x80000000) = 32.
 */

static __inline__ int fls (unsigned int x)
{
	return __cntlz(x);
}
#define fls64(x)   generic_fls64(x)

static __inline__ int
find_next_bit(const unsigned long *addr, int size, int offset)
{
	const unsigned long *p = addr + (offset >> 5);
	unsigned long result = offset & ~31UL;
	unsigned long tmp;

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
	if (tmp == 0UL)	/* Are any bits set? */
		return result + size;	/* Nope. */
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

static __inline__ int
find_next_zero_bit(const unsigned long *addr, int size, int offset)
{
	const unsigned long *p = addr + (offset >> 5);
	unsigned long result = offset & ~31UL;
	unsigned long tmp;

	if (offset >= size)
		return size;
	size -= result;
	offset &= 31UL;
	if (offset) {
		tmp = *p++;
		tmp |= ~0UL >> (32-offset);
		if (size < 32)
			goto found_first;
		if (~tmp)
			goto found_middle;
		size -= 32;
		result += 32;
	}
	while (size & ~31UL) {
		if (~(tmp = *p++))
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

#define find_first_zero_bit(addr, size) \
        find_next_zero_bit((addr), (size), 0)

#ifdef __XTENSA_EL__
# define ext2_set_bit(nr,addr) __test_and_set_bit((nr), (addr))
# define ext2_set_bit_atomic(lock,nr,addr) test_and_set_bit((nr),(addr))
# define ext2_clear_bit(nr,addr) __test_and_clear_bit((nr), (addr))
# define ext2_clear_bit_atomic(lock,nr,addr) test_and_clear_bit((nr),(addr))
# define ext2_test_bit(nr,addr) test_bit((nr), (addr))
# define ext2_find_first_zero_bit(addr, size) find_first_zero_bit((addr),(size))
# define ext2_find_next_zero_bit(addr, size, offset) \
                find_next_zero_bit((addr), (size), (offset))
#elif defined(__XTENSA_EB__)
# define ext2_set_bit(nr,addr) __test_and_set_bit((nr) ^ 0x18, (addr))
# define ext2_set_bit_atomic(lock,nr,addr) test_and_set_bit((nr) ^ 0x18, (addr))
# define ext2_clear_bit(nr,addr) __test_and_clear_bit((nr) ^ 18, (addr))
# define ext2_clear_bit_atomic(lock,nr,addr) test_and_clear_bit((nr)^0x18,(addr))
# define ext2_test_bit(nr,addr) test_bit((nr) ^ 0x18, (addr))
# define ext2_find_first_zero_bit(addr, size) \
        ext2_find_next_zero_bit((addr), (size), 0)

static __inline__ unsigned long ext2_find_next_zero_bit(void *addr, unsigned long size, unsigned long offset)
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

#else
# error processor byte order undefined!
#endif


#define hweight32(x)	generic_hweight32(x)
#define hweight16(x)	generic_hweight16(x)
#define hweight8(x)	generic_hweight8(x)

/*
 * Find the first bit set in a 140-bit bitmap.
 * The first 100 bits are unlikely to be set.
 */

static inline int sched_find_first_bit(const unsigned long *b)
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


/* Bitmap functions for the minix filesystem.  */

#define minix_test_and_set_bit(nr,addr) test_and_set_bit(nr,addr)
#define minix_set_bit(nr,addr) set_bit(nr,addr)
#define minix_test_and_clear_bit(nr,addr) test_and_clear_bit(nr,addr)
#define minix_test_bit(nr,addr) test_bit(nr,addr)
#define minix_find_first_zero_bit(addr,size) find_first_zero_bit(addr,size)

#endif	/* __KERNEL__ */

#endif	/* _XTENSA_BITOPS_H */
