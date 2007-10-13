#ifndef _ASM_M32R_SYSTEM_H
#define _ASM_M32R_SYSTEM_H

/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001  Hiroyuki Kondo, Hirokazu Takata, and Hitoshi Yamamoto
 * Copyright (C) 2004, 2006  Hirokazu Takata <takata at linux-m32r.org>
 */

#include <linux/compiler.h>
#include <asm/assembler.h>

#ifdef __KERNEL__

/*
 * switch_to(prev, next) should switch from task `prev' to `next'
 * `prev' will never be the same as `next'.
 *
 * `next' and `prev' should be struct task_struct, but it isn't always defined
 */

#if defined(CONFIG_FRAME_POINTER) || \
	!defined(CONFIG_SCHED_NO_NO_OMIT_FRAME_POINTER)
#define M32R_PUSH_FP "	push fp\n"
#define M32R_POP_FP  "	pop  fp\n"
#else
#define M32R_PUSH_FP ""
#define M32R_POP_FP  ""
#endif

#define switch_to(prev, next, last)  do { \
	__asm__ __volatile__ ( \
		"	seth	lr, #high(1f)				\n" \
		"	or3	lr, lr, #low(1f)			\n" \
		"	st	lr, @%4  ; store old LR			\n" \
		"	ld	lr, @%5  ; load new LR			\n" \
			M32R_PUSH_FP \
		"	st	sp, @%2  ; store old SP			\n" \
		"	ld	sp, @%3  ; load new SP			\n" \
		"	push	%1  ; store `prev' on new stack		\n" \
		"	jmp	lr					\n" \
		"	.fillinsn					\n" \
		"1:							\n" \
		"	pop	%0  ; restore `__last' from new stack	\n" \
			M32R_POP_FP \
		: "=r" (last) \
		: "0" (prev), \
		  "r" (&(prev->thread.sp)), "r" (&(next->thread.sp)), \
		  "r" (&(prev->thread.lr)), "r" (&(next->thread.lr)) \
		: "memory", "lr" \
	); \
} while(0)

/* Interrupt Control */
#if !defined(CONFIG_CHIP_M32102) && !defined(CONFIG_CHIP_M32104)
#define local_irq_enable() \
	__asm__ __volatile__ ("setpsw #0x40 -> nop": : :"memory")
#define local_irq_disable() \
	__asm__ __volatile__ ("clrpsw #0x40 -> nop": : :"memory")
#else	/* CONFIG_CHIP_M32102 || CONFIG_CHIP_M32104 */
static inline void local_irq_enable(void)
{
	unsigned long tmpreg;
	__asm__ __volatile__(
		"mvfc	%0, psw;		\n\t"
		"or3	%0, %0, #0x0040;	\n\t"
		"mvtc	%0, psw;		\n\t"
	: "=&r" (tmpreg) : : "cbit", "memory");
}

static inline void local_irq_disable(void)
{
	unsigned long tmpreg0, tmpreg1;
	__asm__ __volatile__(
		"ld24	%0, #0	; Use 32-bit insn. \n\t"
		"mvfc	%1, psw	; No interrupt can be accepted here. \n\t"
		"mvtc	%0, psw	\n\t"
		"and3	%0, %1, #0xffbf	\n\t"
		"mvtc	%0, psw	\n\t"
	: "=&r" (tmpreg0), "=&r" (tmpreg1) : : "cbit", "memory");
}
#endif	/* CONFIG_CHIP_M32102 || CONFIG_CHIP_M32104 */

#define local_save_flags(x) \
	__asm__ __volatile__("mvfc %0,psw" : "=r"(x) : /* no input */)

#define local_irq_restore(x) \
	__asm__ __volatile__("mvtc %0,psw" : /* no outputs */ \
		: "r" (x) : "cbit", "memory")

#if !(defined(CONFIG_CHIP_M32102) || defined(CONFIG_CHIP_M32104))
#define local_irq_save(x)				\
	__asm__ __volatile__(				\
  		"mvfc	%0, psw;		\n\t"	\
	  	"clrpsw	#0x40 -> nop;		\n\t"	\
  		: "=r" (x) : /* no input */ : "memory")
#else	/* CONFIG_CHIP_M32102 || CONFIG_CHIP_M32104 */
#define local_irq_save(x) 				\
	({						\
		unsigned long tmpreg;			\
		__asm__ __volatile__( 			\
			"ld24	%1, #0 \n\t" 		\
			"mvfc	%0, psw \n\t"		\
			"mvtc	%1, psw \n\t"		\
			"and3	%1, %0, #0xffbf \n\t"	\
			"mvtc	%1, psw \n\t" 		\
			: "=r" (x), "=&r" (tmpreg)	\
			: : "cbit", "memory");		\
	})
#endif	/* CONFIG_CHIP_M32102 || CONFIG_CHIP_M32104 */

#define irqs_disabled()					\
	({						\
		unsigned long flags;			\
		local_save_flags(flags);		\
		!(flags & 0x40);			\
	})

#define nop()	__asm__ __volatile__ ("nop" : : )

#define xchg(ptr,x) \
	((__typeof__(*(ptr)))__xchg((unsigned long)(x),(ptr),sizeof(*(ptr))))

#ifdef CONFIG_SMP
extern void  __xchg_called_with_bad_pointer(void);
#endif

#ifdef CONFIG_CHIP_M32700_TS1
#define DCACHE_CLEAR(reg0, reg1, addr)				\
	"seth	"reg1", #high(dcache_dummy);		\n\t"	\
	"or3	"reg1", "reg1", #low(dcache_dummy);	\n\t"	\
	"lock	"reg0", @"reg1";			\n\t"	\
	"add3	"reg0", "addr", #0x1000;		\n\t"	\
	"ld	"reg0", @"reg0";			\n\t"	\
	"add3	"reg0", "addr", #0x2000;		\n\t"	\
	"ld	"reg0", @"reg0";			\n\t"	\
	"unlock	"reg0", @"reg1";			\n\t"
	/* FIXME: This workaround code cannot handle kernel modules
	 * correctly under SMP environment.
	 */
#else	/* CONFIG_CHIP_M32700_TS1 */
#define DCACHE_CLEAR(reg0, reg1, addr)
#endif	/* CONFIG_CHIP_M32700_TS1 */

static __always_inline unsigned long
__xchg(unsigned long x, volatile void * ptr, int size)
{
	unsigned long flags;
	unsigned long tmp = 0;

	local_irq_save(flags);

	switch (size) {
#ifndef CONFIG_SMP
	case 1:
		__asm__ __volatile__ (
			"ldb	%0, @%2 \n\t"
			"stb	%1, @%2 \n\t"
			: "=&r" (tmp) : "r" (x), "r" (ptr) : "memory");
		break;
	case 2:
		__asm__ __volatile__ (
			"ldh	%0, @%2 \n\t"
			"sth	%1, @%2 \n\t"
			: "=&r" (tmp) : "r" (x), "r" (ptr) : "memory");
		break;
	case 4:
		__asm__ __volatile__ (
			"ld	%0, @%2 \n\t"
			"st	%1, @%2 \n\t"
			: "=&r" (tmp) : "r" (x), "r" (ptr) : "memory");
		break;
#else  /* CONFIG_SMP */
	case 4:
		__asm__ __volatile__ (
			DCACHE_CLEAR("%0", "r4", "%2")
			"lock	%0, @%2;	\n\t"
			"unlock	%1, @%2;	\n\t"
			: "=&r" (tmp) : "r" (x), "r" (ptr)
			: "memory"
#ifdef CONFIG_CHIP_M32700_TS1
			, "r4"
#endif	/* CONFIG_CHIP_M32700_TS1 */
		);
		break;
	default:
		__xchg_called_with_bad_pointer();
#endif  /* CONFIG_SMP */
	}

	local_irq_restore(flags);

	return (tmp);
}

#define __HAVE_ARCH_CMPXCHG	1

static inline unsigned long
__cmpxchg_u32(volatile unsigned int *p, unsigned int old, unsigned int new)
{
	unsigned long flags;
	unsigned int retval;

	local_irq_save(flags);
	__asm__ __volatile__ (
			DCACHE_CLEAR("%0", "r4", "%1")
			M32R_LOCK" %0, @%1;	\n"
		"	bne	%0, %2, 1f;	\n"
			M32R_UNLOCK" %3, @%1;	\n"
		"	bra	2f;		\n"
                "       .fillinsn		\n"
		"1:"
			M32R_UNLOCK" %0, @%1;	\n"
                "       .fillinsn		\n"
		"2:"
			: "=&r" (retval)
			: "r" (p), "r" (old), "r" (new)
			: "cbit", "memory"
#ifdef CONFIG_CHIP_M32700_TS1
			, "r4"
#endif  /* CONFIG_CHIP_M32700_TS1 */
		);
	local_irq_restore(flags);

	return retval;
}

/* This function doesn't exist, so you'll get a linker error
   if something tries to do an invalid cmpxchg().  */
extern void __cmpxchg_called_with_bad_pointer(void);

static inline unsigned long
__cmpxchg(volatile void *ptr, unsigned long old, unsigned long new, int size)
{
	switch (size) {
	case 4:
		return __cmpxchg_u32(ptr, old, new);
#if 0	/* we don't have __cmpxchg_u64 */
	case 8:
		return __cmpxchg_u64(ptr, old, new);
#endif /* 0 */
	}
	__cmpxchg_called_with_bad_pointer();
	return old;
}

#define cmpxchg(ptr,o,n)						 \
  ({									 \
     __typeof__(*(ptr)) _o_ = (o);					 \
     __typeof__(*(ptr)) _n_ = (n);					 \
     (__typeof__(*(ptr))) __cmpxchg((ptr), (unsigned long)_o_,		 \
				    (unsigned long)_n_, sizeof(*(ptr))); \
  })

#endif  /* __KERNEL__ */

/*
 * Memory barrier.
 *
 * mb() prevents loads and stores being reordered across this point.
 * rmb() prevents loads being reordered across this point.
 * wmb() prevents stores being reordered across this point.
 */
#define mb()   barrier()
#define rmb()  mb()
#define wmb()  mb()

/**
 * read_barrier_depends - Flush all pending reads that subsequents reads
 * depend on.
 *
 * No data-dependent reads from memory-like regions are ever reordered
 * over this barrier.  All reads preceding this primitive are guaranteed
 * to access memory (but not necessarily other CPUs' caches) before any
 * reads following this primitive that depend on the data return by
 * any of the preceding reads.  This primitive is much lighter weight than
 * rmb() on most CPUs, and is never heavier weight than is
 * rmb().
 *
 * These ordering constraints are respected by both the local CPU
 * and the compiler.
 *
 * Ordering is not guaranteed by anything other than these primitives,
 * not even by data dependencies.  See the documentation for
 * memory_barrier() for examples and URLs to more information.
 *
 * For example, the following code would force ordering (the initial
 * value of "a" is zero, "b" is one, and "p" is "&a"):
 *
 * <programlisting>
 *      CPU 0                           CPU 1
 *
 *      b = 2;
 *      memory_barrier();
 *      p = &b;                         q = p;
 *                                      read_barrier_depends();
 *                                      d = *q;
 * </programlisting>
 *
 *
 * because the read of "*q" depends on the read of "p" and these
 * two reads are separated by a read_barrier_depends().  However,
 * the following code, with the same initial values for "a" and "b":
 *
 * <programlisting>
 *      CPU 0                           CPU 1
 *
 *      a = 2;
 *      memory_barrier();
 *      b = 3;                          y = b;
 *                                      read_barrier_depends();
 *                                      x = a;
 * </programlisting>
 *
 * does not enforce ordering, since there is no data dependency between
 * the read of "a" and the read of "b".  Therefore, on some CPUs, such
 * as Alpha, "y" could be set to 3 and "x" to 0.  Use rmb()
 * in cases like this where there are no data dependencies.
 **/

#define read_barrier_depends()	do { } while (0)

#ifdef CONFIG_SMP
#define smp_mb()	mb()
#define smp_rmb()	rmb()
#define smp_wmb()	wmb()
#define smp_read_barrier_depends()	read_barrier_depends()
#define set_mb(var, value) do { (void) xchg(&var, value); } while (0)
#else
#define smp_mb()	barrier()
#define smp_rmb()	barrier()
#define smp_wmb()	barrier()
#define smp_read_barrier_depends()	do { } while (0)
#define set_mb(var, value) do { var = value; barrier(); } while (0)
#endif

#define arch_align_stack(x) (x)

#endif /* _ASM_M32R_SYSTEM_H */
