#ifndef _M68K_BITOPS_H
#define _M68K_BITOPS_H
/*
 * Copyright 1992, Linus Torvalds.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

#include <linux/compiler.h>

/*
 * Require 68020 or better.
 *
 * They use the standard big-endian m680x0 bit ordering.
 */

#define test_and_set_bit(nr,vaddr) \
  (__builtin_constant_p(nr) ? \
   __constant_test_and_set_bit(nr, vaddr) : \
   __generic_test_and_set_bit(nr, vaddr))

#define __test_and_set_bit(nr,vaddr) test_and_set_bit(nr,vaddr)

static inline int __constant_test_and_set_bit(int nr, unsigned long *vaddr)
{
	char *p = (char *)vaddr + (nr ^ 31) / 8;
	char retval;

	__asm__ __volatile__ ("bset %2,%1; sne %0"
			: "=d" (retval), "+m" (*p)
			: "di" (nr & 7));

	return retval;
}

static inline int __generic_test_and_set_bit(int nr, unsigned long *vaddr)
{
	char retval;

	__asm__ __volatile__ ("bfset %2{%1:#1}; sne %0"
			: "=d" (retval) : "d" (nr^31), "o" (*vaddr) : "memory");

	return retval;
}

#define set_bit(nr,vaddr) \
  (__builtin_constant_p(nr) ? \
   __constant_set_bit(nr, vaddr) : \
   __generic_set_bit(nr, vaddr))

#define __set_bit(nr,vaddr) set_bit(nr,vaddr)

static inline void __constant_set_bit(int nr, volatile unsigned long *vaddr)
{
	char *p = (char *)vaddr + (nr ^ 31) / 8;
	__asm__ __volatile__ ("bset %1,%0"
			: "+m" (*p) : "di" (nr & 7));
}

static inline void __generic_set_bit(int nr, volatile unsigned long *vaddr)
{
	__asm__ __volatile__ ("bfset %1{%0:#1}"
			: : "d" (nr^31), "o" (*vaddr) : "memory");
}

#define test_and_clear_bit(nr,vaddr) \
  (__builtin_constant_p(nr) ? \
   __constant_test_and_clear_bit(nr, vaddr) : \
   __generic_test_and_clear_bit(nr, vaddr))

#define __test_and_clear_bit(nr,vaddr) test_and_clear_bit(nr,vaddr)

static inline int __constant_test_and_clear_bit(int nr, unsigned long *vaddr)
{
	char *p = (char *)vaddr + (nr ^ 31) / 8;
	char retval;

	__asm__ __volatile__ ("bclr %2,%1; sne %0"
			: "=d" (retval), "+m" (*p)
			: "di" (nr & 7));

	return retval;
}

static inline int __generic_test_and_clear_bit(int nr, unsigned long *vaddr)
{
	char retval;

	__asm__ __volatile__ ("bfclr %2{%1:#1}; sne %0"
			: "=d" (retval) : "d" (nr^31), "o" (*vaddr) : "memory");

	return retval;
}

/*
 * clear_bit() doesn't provide any barrier for the compiler.
 */
#define smp_mb__before_clear_bit()	barrier()
#define smp_mb__after_clear_bit()	barrier()

#define clear_bit(nr,vaddr) \
  (__builtin_constant_p(nr) ? \
   __constant_clear_bit(nr, vaddr) : \
   __generic_clear_bit(nr, vaddr))
#define __clear_bit(nr,vaddr) clear_bit(nr,vaddr)

static inline void __constant_clear_bit(int nr, volatile unsigned long *vaddr)
{
	char *p = (char *)vaddr + (nr ^ 31) / 8;
	__asm__ __volatile__ ("bclr %1,%0"
			: "+m" (*p) : "di" (nr & 7));
}

static inline void __generic_clear_bit(int nr, volatile unsigned long *vaddr)
{
	__asm__ __volatile__ ("bfclr %1{%0:#1}"
			: : "d" (nr^31), "o" (*vaddr) : "memory");
}

#define test_and_change_bit(nr,vaddr) \
  (__builtin_constant_p(nr) ? \
   __constant_test_and_change_bit(nr, vaddr) : \
   __generic_test_and_change_bit(nr, vaddr))

#define __test_and_change_bit(nr,vaddr) test_and_change_bit(nr,vaddr)
#define __change_bit(nr,vaddr) change_bit(nr,vaddr)

static inline int __constant_test_and_change_bit(int nr, unsigned long *vaddr)
{
	char *p = (char *)vaddr + (nr ^ 31) / 8;
	char retval;

	__asm__ __volatile__ ("bchg %2,%1; sne %0"
			: "=d" (retval), "+m" (*p)
			: "di" (nr & 7));

	return retval;
}

static inline int __generic_test_and_change_bit(int nr, unsigned long *vaddr)
{
	char retval;

	__asm__ __volatile__ ("bfchg %2{%1:#1}; sne %0"
			: "=d" (retval) : "d" (nr^31), "o" (*vaddr) : "memory");

	return retval;
}

#define change_bit(nr,vaddr) \
  (__builtin_constant_p(nr) ? \
   __constant_change_bit(nr, vaddr) : \
   __generic_change_bit(nr, vaddr))

static inline void __constant_change_bit(int nr, unsigned long *vaddr)
{
	char *p = (char *)vaddr + (nr ^ 31) / 8;
	__asm__ __volatile__ ("bchg %1,%0"
			: "+m" (*p) : "di" (nr & 7));
}

static inline void __generic_change_bit(int nr, unsigned long *vaddr)
{
	__asm__ __volatile__ ("bfchg %1{%0:#1}"
			: : "d" (nr^31), "o" (*vaddr) : "memory");
}

static inline int test_bit(int nr, const unsigned long *vaddr)
{
	return (vaddr[nr >> 5] & (1UL << (nr & 31))) != 0;
}

static inline int find_first_zero_bit(const unsigned long *vaddr,
				      unsigned size)
{
	const unsigned long *p = vaddr;
	int res = 32;
	unsigned long num;

	if (!size)
		return 0;

	size = (size + 31) >> 5;
	while (!(num = ~*p++)) {
		if (!--size)
			goto out;
	}

	__asm__ __volatile__ ("bfffo %1{#0,#0},%0"
			      : "=d" (res) : "d" (num & -num));
	res ^= 31;
out:
	return ((long)p - (long)vaddr - 4) * 8 + res;
}

static inline int find_next_zero_bit(const unsigned long *vaddr, int size,
				     int offset)
{
	const unsigned long *p = vaddr + (offset >> 5);
	int bit = offset & 31UL, res;

	if (offset >= size)
		return size;

	if (bit) {
		unsigned long num = ~*p++ & (~0UL << bit);
		offset -= bit;

		/* Look for zero in first longword */
		__asm__ __volatile__ ("bfffo %1{#0,#0},%0"
				      : "=d" (res) : "d" (num & -num));
		if (res < 32)
			return offset + (res ^ 31);
		offset += 32;
	}
	/* No zero yet, search remaining full bytes for a zero */
	res = find_first_zero_bit(p, size - ((long)p - (long)vaddr) * 8);
	return offset + res;
}

static inline int find_first_bit(const unsigned long *vaddr, unsigned size)
{
	const unsigned long *p = vaddr;
	int res = 32;
	unsigned long num;

	if (!size)
		return 0;

	size = (size + 31) >> 5;
	while (!(num = *p++)) {
		if (!--size)
			goto out;
	}

	__asm__ __volatile__ ("bfffo %1{#0,#0},%0"
			      : "=d" (res) : "d" (num & -num));
	res ^= 31;
out:
	return ((long)p - (long)vaddr - 4) * 8 + res;
}

static inline int find_next_bit(const unsigned long *vaddr, int size,
				int offset)
{
	const unsigned long *p = vaddr + (offset >> 5);
	int bit = offset & 31UL, res;

	if (offset >= size)
		return size;

	if (bit) {
		unsigned long num = *p++ & (~0UL << bit);
		offset -= bit;

		/* Look for one in first longword */
		__asm__ __volatile__ ("bfffo %1{#0,#0},%0"
				      : "=d" (res) : "d" (num & -num));
		if (res < 32)
			return offset + (res ^ 31);
		offset += 32;
	}
	/* No one yet, search remaining full bytes for a one */
	res = find_first_bit(p, size - ((long)p - (long)vaddr) * 8);
	return offset + res;
}

/*
 * ffz = Find First Zero in word. Undefined if no zero exists,
 * so code should check against ~0UL first..
 */
static inline unsigned long ffz(unsigned long word)
{
	int res;

	__asm__ __volatile__ ("bfffo %1{#0,#0},%0"
			      : "=d" (res) : "d" (~word & -~word));
	return res ^ 31;
}

#ifdef __KERNEL__

/*
 * ffs: find first bit set. This is defined the same way as
 * the libc and compiler builtin ffs routines, therefore
 * differs in spirit from the above ffz (man ffs).
 */

static inline int ffs(int x)
{
	int cnt;

	asm ("bfffo %1{#0:#0},%0" : "=d" (cnt) : "dm" (x & -x));

	return 32 - cnt;
}
#define __ffs(x) (ffs(x) - 1)

/*
 * fls: find last bit set.
 */

static inline int fls(int x)
{
	int cnt;

	asm ("bfffo %1{#0,#0},%0" : "=d" (cnt) : "dm" (x));

	return 32 - cnt;
}

#include <asm-generic/bitops/fls64.h>
#include <asm-generic/bitops/sched.h>
#include <asm-generic/bitops/hweight.h>

/* Bitmap functions for the minix filesystem */

static inline int minix_find_first_zero_bit(const void *vaddr, unsigned size)
{
	const unsigned short *p = vaddr, *addr = vaddr;
	int res;
	unsigned short num;

	if (!size)
		return 0;

	size = (size >> 4) + ((size & 15) > 0);
	while (*p++ == 0xffff)
	{
		if (--size == 0)
			return (p - addr) << 4;
	}

	num = ~*--p;
	__asm__ __volatile__ ("bfffo %1{#16,#16},%0"
			      : "=d" (res) : "d" (num & -num));
	return ((p - addr) << 4) + (res ^ 31);
}

#define minix_test_and_set_bit(nr, addr)	__test_and_set_bit((nr) ^ 16, (unsigned long *)(addr))
#define minix_set_bit(nr,addr)			__set_bit((nr) ^ 16, (unsigned long *)(addr))
#define minix_test_and_clear_bit(nr, addr)	__test_and_clear_bit((nr) ^ 16, (unsigned long *)(addr))

static inline int minix_test_bit(int nr, const void *vaddr)
{
	const unsigned short *p = vaddr;
	return (p[nr >> 4] & (1U << (nr & 15))) != 0;
}

/* Bitmap functions for the ext2 filesystem. */

#define ext2_set_bit(nr, addr)			__test_and_set_bit((nr) ^ 24, (unsigned long *)(addr))
#define ext2_set_bit_atomic(lock, nr, addr)	test_and_set_bit((nr) ^ 24, (unsigned long *)(addr))
#define ext2_clear_bit(nr, addr)		__test_and_clear_bit((nr) ^ 24, (unsigned long *)(addr))
#define ext2_clear_bit_atomic(lock, nr, addr)	test_and_clear_bit((nr) ^ 24, (unsigned long *)(addr))

static inline int ext2_test_bit(int nr, const void *vaddr)
{
	const unsigned char *p = vaddr;
	return (p[nr >> 3] & (1U << (nr & 7))) != 0;
}

static inline int ext2_find_first_zero_bit(const void *vaddr, unsigned size)
{
	const unsigned long *p = vaddr, *addr = vaddr;
	int res;

	if (!size)
		return 0;

	size = (size >> 5) + ((size & 31) > 0);
	while (*p++ == ~0UL)
	{
		if (--size == 0)
			return (p - addr) << 5;
	}

	--p;
	for (res = 0; res < 32; res++)
		if (!ext2_test_bit (res, p))
			break;
	return (p - addr) * 32 + res;
}

static inline int ext2_find_next_zero_bit(const void *vaddr, unsigned size,
					  unsigned offset)
{
	const unsigned long *addr = vaddr;
	const unsigned long *p = addr + (offset >> 5);
	int bit = offset & 31UL, res;

	if (offset >= size)
		return size;

	if (bit) {
		/* Look for zero in first longword */
		for (res = bit; res < 32; res++)
			if (!ext2_test_bit (res, p))
				return (p - addr) * 32 + res;
		p++;
	}
	/* No zero yet, search remaining full bytes for a zero */
	res = ext2_find_first_zero_bit (p, size - 32 * (p - addr));
	return (p - addr) * 32 + res;
}

#endif /* __KERNEL__ */

#endif /* _M68K_BITOPS_H */
