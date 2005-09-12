#ifndef _M68KNOMMU_BITOPS_H
#define _M68KNOMMU_BITOPS_H

/*
 * Copyright 1992, Linus Torvalds.
 */

#include <linux/config.h>
#include <linux/compiler.h>
#include <asm/byteorder.h>	/* swab32 */
#include <asm/system.h>		/* save_flags */

#ifdef __KERNEL__

/*
 *	Generic ffs().
 */
static inline int ffs(int x)
{
	int r = 1;

	if (!x)
		return 0;
	if (!(x & 0xffff)) {
		x >>= 16;
		r += 16;
	}
	if (!(x & 0xff)) {
		x >>= 8;
		r += 8;
	}
	if (!(x & 0xf)) {
		x >>= 4;
		r += 4;
	}
	if (!(x & 3)) {
		x >>= 2;
		r += 2;
	}
	if (!(x & 1)) {
		x >>= 1;
		r += 1;
	}
	return r;
}

/*
 *	Generic __ffs().
 */
static inline int __ffs(int x)
{
	int r = 0;

	if (!x)
		return 0;
	if (!(x & 0xffff)) {
		x >>= 16;
		r += 16;
	}
	if (!(x & 0xff)) {
		x >>= 8;
		r += 8;
	}
	if (!(x & 0xf)) {
		x >>= 4;
		r += 4;
	}
	if (!(x & 3)) {
		x >>= 2;
		r += 2;
	}
	if (!(x & 1)) {
		x >>= 1;
		r += 1;
	}
	return r;
}

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

/*
 * ffz = Find First Zero in word. Undefined if no zero exists,
 * so code should check against ~0UL first..
 */
static __inline__ unsigned long ffz(unsigned long word)
{
	unsigned long result = 0;

	while(word & 1) {
		result++;
		word >>= 1;
	}
	return result;
}


static __inline__ void set_bit(int nr, volatile unsigned long * addr)
{
#ifdef CONFIG_COLDFIRE
	__asm__ __volatile__ ("lea %0,%%a0; bset %1,(%%a0)"
	     : "+m" (((volatile char *)addr)[(nr^31) >> 3])
	     : "d" (nr)
	     : "%a0", "cc");
#else
	__asm__ __volatile__ ("bset %1,%0"
	     : "+m" (((volatile char *)addr)[(nr^31) >> 3])
	     : "di" (nr)
	     : "cc");
#endif
}

#define __set_bit(nr, addr) set_bit(nr, addr)

/*
 * clear_bit() doesn't provide any barrier for the compiler.
 */
#define smp_mb__before_clear_bit()	barrier()
#define smp_mb__after_clear_bit()	barrier()

static __inline__ void clear_bit(int nr, volatile unsigned long * addr)
{
#ifdef CONFIG_COLDFIRE
	__asm__ __volatile__ ("lea %0,%%a0; bclr %1,(%%a0)"
	     : "+m" (((volatile char *)addr)[(nr^31) >> 3])
	     : "d" (nr)
	     : "%a0", "cc");
#else
	__asm__ __volatile__ ("bclr %1,%0"
	     : "+m" (((volatile char *)addr)[(nr^31) >> 3])
	     : "di" (nr)
	     : "cc");
#endif
}

#define __clear_bit(nr, addr) clear_bit(nr, addr)

static __inline__ void change_bit(int nr, volatile unsigned long * addr)
{
#ifdef CONFIG_COLDFIRE
	__asm__ __volatile__ ("lea %0,%%a0; bchg %1,(%%a0)"
	     : "+m" (((volatile char *)addr)[(nr^31) >> 3])
	     : "d" (nr)
	     : "%a0", "cc");
#else
	__asm__ __volatile__ ("bchg %1,%0"
	     : "+m" (((volatile char *)addr)[(nr^31) >> 3])
	     : "di" (nr)
	     : "cc");
#endif
}

#define __change_bit(nr, addr) change_bit(nr, addr)

static __inline__ int test_and_set_bit(int nr, volatile unsigned long * addr)
{
	char retval;

#ifdef CONFIG_COLDFIRE
	__asm__ __volatile__ ("lea %1,%%a0; bset %2,(%%a0); sne %0"
	     : "=d" (retval), "+m" (((volatile char *)addr)[(nr^31) >> 3])
	     : "d" (nr)
	     : "%a0");
#else
	__asm__ __volatile__ ("bset %2,%1; sne %0"
	     : "=d" (retval), "+m" (((volatile char *)addr)[(nr^31) >> 3])
	     : "di" (nr)
	     /* No clobber */);
#endif

	return retval;
}

#define __test_and_set_bit(nr, addr) test_and_set_bit(nr, addr)

static __inline__ int test_and_clear_bit(int nr, volatile unsigned long * addr)
{
	char retval;

#ifdef CONFIG_COLDFIRE
	__asm__ __volatile__ ("lea %1,%%a0; bclr %2,(%%a0); sne %0"
	     : "=d" (retval), "+m" (((volatile char *)addr)[(nr^31) >> 3])
	     : "d" (nr)
	     : "%a0");
#else
	__asm__ __volatile__ ("bclr %2,%1; sne %0"
	     : "=d" (retval), "+m" (((volatile char *)addr)[(nr^31) >> 3])
	     : "di" (nr)
	     /* No clobber */);
#endif

	return retval;
}

#define __test_and_clear_bit(nr, addr) test_and_clear_bit(nr, addr)

static __inline__ int test_and_change_bit(int nr, volatile unsigned long * addr)
{
	char retval;

#ifdef CONFIG_COLDFIRE
	__asm__ __volatile__ ("lea %1,%%a0\n\tbchg %2,(%%a0)\n\tsne %0"
	     : "=d" (retval), "+m" (((volatile char *)addr)[(nr^31) >> 3])
	     : "d" (nr)
	     : "%a0");
#else
	__asm__ __volatile__ ("bchg %2,%1; sne %0"
	     : "=d" (retval), "+m" (((volatile char *)addr)[(nr^31) >> 3])
	     : "di" (nr)
	     /* No clobber */);
#endif

	return retval;
}

#define __test_and_change_bit(nr, addr) test_and_change_bit(nr, addr)

/*
 * This routine doesn't need to be atomic.
 */
static __inline__ int __constant_test_bit(int nr, const volatile unsigned long * addr)
{
	return ((1UL << (nr & 31)) & (((const volatile unsigned int *) addr)[nr >> 5])) != 0;
}

static __inline__ int __test_bit(int nr, const volatile unsigned long * addr)
{
	int 	* a = (int *) addr;
	int	mask;

	a += nr >> 5;
	mask = 1 << (nr & 0x1f);
	return ((mask & *a) != 0);
}

#define test_bit(nr,addr) \
(__builtin_constant_p(nr) ? \
 __constant_test_bit((nr),(addr)) : \
 __test_bit((nr),(addr)))

#define find_first_zero_bit(addr, size) \
        find_next_zero_bit((addr), (size), 0)
#define find_first_bit(addr, size) \
        find_next_bit((addr), (size), 0)

static __inline__ int find_next_zero_bit (const void * addr, int size, int offset)
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
	tmp |= ~0UL >> size;
found_middle:
	return result + ffz(tmp);
}

/*
 * Find next one bit in a bitmap reasonably efficiently.
 */
static __inline__ unsigned long find_next_bit(const unsigned long *addr,
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

/*
 * hweightN: returns the hamming weight (i.e. the number
 * of bits set) of a N-bit word
 */

#define hweight32(x) generic_hweight32(x)
#define hweight16(x) generic_hweight16(x)
#define hweight8(x) generic_hweight8(x)


static __inline__ int ext2_set_bit(int nr, volatile void * addr)
{
	char retval;

#ifdef CONFIG_COLDFIRE
	__asm__ __volatile__ ("lea %1,%%a0; bset %2,(%%a0); sne %0"
	     : "=d" (retval), "+m" (((volatile char *)addr)[nr >> 3])
	     : "d" (nr)
	     : "%a0");
#else
	__asm__ __volatile__ ("bset %2,%1; sne %0"
	     : "=d" (retval), "+m" (((volatile char *)addr)[nr >> 3])
	     : "di" (nr)
	     /* No clobber */);
#endif

	return retval;
}

static __inline__ int ext2_clear_bit(int nr, volatile void * addr)
{
	char retval;

#ifdef CONFIG_COLDFIRE
	__asm__ __volatile__ ("lea %1,%%a0; bclr %2,(%%a0); sne %0"
	     : "=d" (retval), "+m" (((volatile char *)addr)[nr >> 3])
	     : "d" (nr)
	     : "%a0");
#else
	__asm__ __volatile__ ("bclr %2,%1; sne %0"
	     : "=d" (retval), "+m" (((volatile char *)addr)[nr >> 3])
	     : "di" (nr)
	     /* No clobber */);
#endif

	return retval;
}

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

static __inline__ int ext2_test_bit(int nr, const volatile void * addr)
{
	char retval;

#ifdef CONFIG_COLDFIRE
	__asm__ __volatile__ ("lea %1,%%a0; btst %2,(%%a0); sne %0"
	     : "=d" (retval)
	     : "m" (((const volatile char *)addr)[nr >> 3]), "d" (nr)
	     : "%a0");
#else
	__asm__ __volatile__ ("btst %2,%1; sne %0"
	     : "=d" (retval)
	     : "m" (((const volatile char *)addr)[nr >> 3]), "di" (nr)
	     /* No clobber */);
#endif

	return retval;
}

#define ext2_find_first_zero_bit(addr, size) \
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

/* Bitmap functions for the minix filesystem.  */
#define minix_test_and_set_bit(nr,addr) test_and_set_bit(nr,addr)
#define minix_set_bit(nr,addr) set_bit(nr,addr)
#define minix_test_and_clear_bit(nr,addr) test_and_clear_bit(nr,addr)
#define minix_test_bit(nr,addr) test_bit(nr,addr)
#define minix_find_first_zero_bit(addr,size) find_first_zero_bit(addr,size)

/**
 * hweightN - returns the hamming weight of a N-bit word
 * @x: the word to weigh
 *
 * The Hamming Weight of a number is the total number of bits set in it.
 */

#define hweight32(x) generic_hweight32(x)
#define hweight16(x) generic_hweight16(x)
#define hweight8(x) generic_hweight8(x)

#endif /* __KERNEL__ */

/*
 * fls: find last bit set.
 */
#define fls(x) generic_fls(x)

#endif /* _M68KNOMMU_BITOPS_H */
