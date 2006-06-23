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


#include <linux/compiler.h>	/* unlikely  */
#include <asm/byteorder.h>	/* swab32 */
#include <asm/system.h>		/* interrupt enable/disable */


#ifdef __KERNEL__

#include <asm-generic/bitops/ffz.h>

/*
 * The __ functions are not atomic
 */

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

#include <asm-generic/bitops/ffs.h>
#include <asm-generic/bitops/fls.h>
#include <asm-generic/bitops/fls64.h>
#include <asm-generic/bitops/__ffs.h>
#include <asm-generic/bitops/find.h>
#include <asm-generic/bitops/sched.h>
#include <asm-generic/bitops/hweight.h>

#include <asm-generic/bitops/ext2-non-atomic.h>
#define ext2_set_bit_atomic(l,n,a)      test_and_set_bit(n,a)
#define ext2_clear_bit_atomic(l,n,a)    test_and_clear_bit(n,a)

#include <asm-generic/bitops/minix.h>

#endif /* __KERNEL__ */

#endif /* __V850_BITOPS_H__ */
