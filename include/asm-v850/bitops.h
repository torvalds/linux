/*
 * include/asm-v850/bitops.h -- Bit operations
 *
 *  Copyright (C) 2001,02,03,04,05  NEC Electronics Corporation
 *  Copyright (C) 2001,02,03,04,05  Miles Bader <miles@gnu.org>
 *  Copyright (C) 1992  Linus Torvalds.
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 */

#ifndef __V850_BITOPS_H__
#define __V850_BITOPS_H__


#include <linux/config.h>
#include <linux/compiler.h>	/* unlikely  */
#include <asm/byteorder.h>	/* swab32 */
#include <asm/system.h>		/* interrupt enable/disable */


#ifdef __KERNEL__

/*
 * The __ functions are not atomic
 */

/*
 * ffz = Find First Zero in word. Undefined if no zero exists,
 * so code should check against ~0UL first..
 */
static inline unsigned long ffz (unsigned long word)
{
	unsigned long result = 0;

	while (word & 1) {
		result++;
		word >>= 1;
	}
	return result;
}


/* In the following constant-bit-op macros, a "g" constraint is used when
   we really need an integer ("i" constraint).  This is to avoid
   warnings/errors from the compiler in the case where the associated
   operand _isn't_ an integer, and shouldn't produce bogus assembly because
   use of that form is protected by a guard statement that checks for
   constants, and should otherwise be removed by the optimizer.  This
   _usually_ works -- however, __builtin_constant_p returns true for a
   variable with a known constant value too, and unfortunately gcc will
   happily put the variable in a register and use the register for the "g"
   constraint'd asm operand.  To avoid the latter problem, we add a
   constant offset to the operand and subtract it back in the asm code;
   forcing gcc to do arithmetic on the value is usually enough to get it
   to use a real constant value.  This is horrible, and ultimately
   unreliable too, but it seems to work for now (hopefully gcc will offer
   us more control in the future, so we can do a better job).  */

#define __const_bit_op(op, nr, addr)					\
  ({ __asm__ (op " (%0 - 0x123), %1"					\
	      :: "g" (((nr) & 0x7) + 0x123),				\
		 "m" (*((char *)(addr) + ((nr) >> 3)))			\
	      : "memory"); })
#define __var_bit_op(op, nr, addr)					\
  ({ int __nr = (nr);							\
     __asm__ (op " %0, [%1]"						\
	      :: "r" (__nr & 0x7),					\
		 "r" ((char *)(addr) + (__nr >> 3))			\
	      : "memory"); })
#define __bit_op(op, nr, addr)						\
  ((__builtin_constant_p (nr) && (unsigned)(nr) <= 0x7FFFF)		\
   ? __const_bit_op (op, nr, addr)					\
   : __var_bit_op (op, nr, addr))

#define __set_bit(nr, addr)		__bit_op ("set1", nr, addr)
#define __clear_bit(nr, addr)		__bit_op ("clr1", nr, addr)
#define __change_bit(nr, addr)		__bit_op ("not1", nr, addr)

/* The bit instructions used by `non-atomic' variants are actually atomic.  */
#define set_bit __set_bit
#define clear_bit __clear_bit
#define change_bit __change_bit


#define __const_tns_bit_op(op, nr, addr)				      \
  ({ int __tns_res;							      \
     __asm__ __volatile__ (						      \
	     "tst1 (%1 - 0x123), %2; setf nz, %0; " op " (%1 - 0x123), %2"    \
	     : "=&r" (__tns_res)					      \
	     : "g" (((nr) & 0x7) + 0x123),				      \
	       "m" (*((char *)(addr) + ((nr) >> 3)))			      \
	     : "memory");						      \
     __tns_res;								      \
  })
#define __var_tns_bit_op(op, nr, addr)					      \
  ({ int __nr = (nr);							      \
     int __tns_res;							      \
     __asm__ __volatile__ (						      \
	     "tst1 %1, [%2]; setf nz, %0; " op " %1, [%2]"		      \
	      : "=&r" (__tns_res)					      \
	      : "r" (__nr & 0x7),					      \
		"r" ((char *)(addr) + (__nr >> 3))			      \
	      : "memory");						      \
     __tns_res;								      \
  })
#define __tns_bit_op(op, nr, addr)					\
  ((__builtin_constant_p (nr) && (unsigned)(nr) <= 0x7FFFF)		\
   ? __const_tns_bit_op (op, nr, addr)					\
   : __var_tns_bit_op (op, nr, addr))
#define __tns_atomic_bit_op(op, nr, addr)				\
  ({ int __tns_atomic_res, __tns_atomic_flags;				\
     local_irq_save (__tns_atomic_flags);				\
     __tns_atomic_res = __tns_bit_op (op, nr, addr);			\
     local_irq_restore (__tns_atomic_flags);				\
     __tns_atomic_res;							\
  })

#define __test_and_set_bit(nr, addr)	__tns_bit_op ("set1", nr, addr)
#define test_and_set_bit(nr, addr)	__tns_atomic_bit_op ("set1", nr, addr)

#define __test_and_clear_bit(nr, addr)	__tns_bit_op ("clr1", nr, addr)
#define test_and_clear_bit(nr, addr)	__tns_atomic_bit_op ("clr1", nr, addr)

#define __test_and_change_bit(nr, addr)	__tns_bit_op ("not1", nr, addr)
#define test_and_change_bit(nr, addr)	__tns_atomic_bit_op ("not1", nr, addr)


#define __const_test_bit(nr, addr)					      \
  ({ int __test_bit_res;						      \
     __asm__ __volatile__ ("tst1 (%1 - 0x123), %2; setf nz, %0"		      \
			   : "=r" (__test_bit_res)			      \
			   : "g" (((nr) & 0x7) + 0x123),		      \
			     "m" (*((const char *)(addr) + ((nr) >> 3))));    \
     __test_bit_res;							      \
  })
static inline int __test_bit (int nr, const void *addr)
{
	int res;
	__asm__ __volatile__ ("tst1 %1, [%2]; setf nz, %0"
			      : "=r" (res)
			      : "r" (nr & 0x7), "r" (addr + (nr >> 3)));
	return res;
}
#define test_bit(nr,addr)						\
  ((__builtin_constant_p (nr) && (unsigned)(nr) <= 0x7FFFF)		\
   ? __const_test_bit ((nr), (addr))					\
   : __test_bit ((nr), (addr)))


/* clear_bit doesn't provide any barrier for the compiler.  */
#define smp_mb__before_clear_bit()	barrier ()
#define smp_mb__after_clear_bit()	barrier ()


#define find_first_zero_bit(addr, size) \
  find_next_zero_bit ((addr), (size), 0)

static inline int find_next_zero_bit(const void *addr, int size, int offset)
{
	unsigned long *p = ((unsigned long *) addr) + (offset >> 5);
	unsigned long result = offset & ~31UL;
	unsigned long tmp;

	if (offset >= size)
		return size;
	size -= result;
	offset &= 31UL;
	if (offset) {
		tmp = * (p++);
		tmp |= ~0UL >> (32-offset);
		if (size < 32)
			goto found_first;
		if (~tmp)
			goto found_middle;
		size -= 32;
		result += 32;
	}
	while (size & ~31UL) {
		if (~ (tmp = * (p++)))
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
	return result + ffz (tmp);
}


/* This is the same as generic_ffs, but we can't use that because it's
   inline and the #include order mucks things up.  */
static inline int generic_ffs_for_find_next_bit(int x)
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
	return result + generic_ffs_for_find_next_bit(tmp);
}

/*
 * find_first_bit - find the first set bit in a memory region
 */
#define find_first_bit(addr, size) \
	find_next_bit((addr), (size), 0)


#define ffs(x) generic_ffs (x)
#define fls(x) generic_fls (x)
#define fls64(x) generic_fls64(x)
#define __ffs(x) ffs(x)


/*
 * This is just `generic_ffs' from <linux/bitops.h>, except that it assumes
 * that at least one bit is set, and returns the real index of the bit
 * (rather than the bit index + 1, like ffs does).
 */
static inline int sched_ffs(int x)
{
	int r = 0;

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
 * bits is set.
 */
static inline int sched_find_first_bit(unsigned long *b)
{
	unsigned offs = 0;
	while (! *b) {
		b++;
		offs += 32;
	}
	return sched_ffs (*b) + offs;
}

/*
 * hweightN: returns the hamming weight (i.e. the number
 * of bits set) of a N-bit word
 */
#define hweight32(x) 			generic_hweight32 (x)
#define hweight16(x) 			generic_hweight16 (x)
#define hweight8(x) 			generic_hweight8 (x)

#define ext2_set_bit			test_and_set_bit
#define ext2_set_bit_atomic(l,n,a)      test_and_set_bit(n,a)
#define ext2_clear_bit			test_and_clear_bit
#define ext2_clear_bit_atomic(l,n,a)    test_and_clear_bit(n,a)
#define ext2_test_bit			test_bit
#define ext2_find_first_zero_bit	find_first_zero_bit
#define ext2_find_next_zero_bit		find_next_zero_bit

/* Bitmap functions for the minix filesystem.  */
#define minix_test_and_set_bit		test_and_set_bit
#define minix_set_bit			set_bit
#define minix_test_and_clear_bit	test_and_clear_bit
#define minix_test_bit 			test_bit
#define minix_find_first_zero_bit 	find_first_zero_bit

#endif /* __KERNEL__ */

#endif /* __V850_BITOPS_H__ */
