/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994, 95, 96, 97, 98, 99, 2003 by Ralf Baechle
 * Copyright (C) 1996 by Paul M. Antoine
 * Copyright (C) 1999 Silicon Graphics
 * Kevin D. Kissell, kevink@mips.org and Carsten Langgaard, carstenl@mips.com
 * Copyright (C) 2000 MIPS Technologies, Inc.
 */
#ifndef _ASM_SYSTEM_H
#define _ASM_SYSTEM_H

#include <linux/config.h>
#include <linux/types.h>

#include <asm/addrspace.h>
#include <asm/cpu-features.h>
#include <asm/dsp.h>
#include <asm/ptrace.h>
#include <asm/war.h>
#include <asm/interrupt.h>

/*
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
 *	CPU 0				CPU 1
 *
 *	b = 2;
 *	memory_barrier();
 *	p = &b;				q = p;
 *					read_barrier_depends();
 *					d = *q;
 * </programlisting>
 *
 * because the read of "*q" depends on the read of "p" and these
 * two reads are separated by a read_barrier_depends().  However,
 * the following code, with the same initial values for "a" and "b":
 *
 * <programlisting>
 *	CPU 0				CPU 1
 *
 *	a = 2;
 *	memory_barrier();
 *	b = 3;				y = b;
 *					read_barrier_depends();
 *					x = a;
 * </programlisting>
 *
 * does not enforce ordering, since there is no data dependency between
 * the read of "a" and the read of "b".  Therefore, on some CPUs, such
 * as Alpha, "y" could be set to 3 and "x" to 0.  Use rmb()
 * in cases like this where there are no data dependencies.
 */

#define read_barrier_depends()	do { } while(0)

#ifdef CONFIG_CPU_HAS_SYNC
#define __sync()				\
	__asm__ __volatile__(			\
		".set	push\n\t"		\
		".set	noreorder\n\t"		\
		".set	mips2\n\t"		\
		"sync\n\t"			\
		".set	pop"			\
		: /* no output */		\
		: /* no input */		\
		: "memory")
#else
#define __sync()	do { } while(0)
#endif

#define __fast_iob()				\
	__asm__ __volatile__(			\
		".set	push\n\t"		\
		".set	noreorder\n\t"		\
		"lw	$0,%0\n\t"		\
		"nop\n\t"			\
		".set	pop"			\
		: /* no output */		\
		: "m" (*(int *)CKSEG1)		\
		: "memory")

#define fast_wmb()	__sync()
#define fast_rmb()	__sync()
#define fast_mb()	__sync()
#define fast_iob()				\
	do {					\
		__sync();			\
		__fast_iob();			\
	} while (0)

#ifdef CONFIG_CPU_HAS_WB

#include <asm/wbflush.h>

#define wmb()		fast_wmb()
#define rmb()		fast_rmb()
#define mb()		wbflush()
#define iob()		wbflush()

#else /* !CONFIG_CPU_HAS_WB */

#define wmb()		fast_wmb()
#define rmb()		fast_rmb()
#define mb()		fast_mb()
#define iob()		fast_iob()

#endif /* !CONFIG_CPU_HAS_WB */

#ifdef CONFIG_SMP
#define smp_mb()	mb()
#define smp_rmb()	rmb()
#define smp_wmb()	wmb()
#define smp_read_barrier_depends()	read_barrier_depends()
#else
#define smp_mb()	barrier()
#define smp_rmb()	barrier()
#define smp_wmb()	barrier()
#define smp_read_barrier_depends()	do { } while(0)
#endif

#define set_mb(var, value) \
do { var = value; mb(); } while (0)

#define set_wmb(var, value) \
do { var = value; wmb(); } while (0)

/*
 * switch_to(n) should switch tasks to task nr n, first
 * checking that n isn't the current task, in which case it does nothing.
 */
extern asmlinkage void *resume(void *last, void *next, void *next_ti);

struct task_struct;

#define switch_to(prev,next,last)					\
do {									\
	if (cpu_has_dsp)						\
		__save_dsp(prev);					\
	(last) = resume(prev, next, task_thread_info(next));		\
	if (cpu_has_dsp)						\
		__restore_dsp(current);					\
} while(0)

/*
 * On SMP systems, when the scheduler does migration-cost autodetection,
 * it needs a way to flush as much of the CPU's caches as possible.
 *
 * TODO: fill this in!
 */
static inline void sched_cacheflush(void)
{
}

static inline unsigned long __xchg_u32(volatile int * m, unsigned int val)
{
	__u32 retval;

	if (cpu_has_llsc && R10000_LLSC_WAR) {
		unsigned long dummy;

		__asm__ __volatile__(
		"	.set	mips3					\n"
		"1:	ll	%0, %3			# xchg_u32	\n"
		"	.set	mips0					\n"
		"	move	%2, %z4					\n"
		"	.set	mips3					\n"
		"	sc	%2, %1					\n"
		"	beqzl	%2, 1b					\n"
#ifdef CONFIG_SMP
		"	sync						\n"
#endif
		"	.set	mips0					\n"
		: "=&r" (retval), "=m" (*m), "=&r" (dummy)
		: "R" (*m), "Jr" (val)
		: "memory");
	} else if (cpu_has_llsc) {
		unsigned long dummy;

		__asm__ __volatile__(
		"	.set	mips3					\n"
		"1:	ll	%0, %3			# xchg_u32	\n"
		"	.set	mips0					\n"
		"	move	%2, %z4					\n"
		"	.set	mips3					\n"
		"	sc	%2, %1					\n"
		"	beqz	%2, 1b					\n"
#ifdef CONFIG_SMP
		"	sync						\n"
#endif
		"	.set	mips0					\n"
		: "=&r" (retval), "=m" (*m), "=&r" (dummy)
		: "R" (*m), "Jr" (val)
		: "memory");
	} else {
		unsigned long flags;

		local_irq_save(flags);
		retval = *m;
		*m = val;
		local_irq_restore(flags);	/* implies memory barrier  */
	}

	return retval;
}

#ifdef CONFIG_64BIT
static inline __u64 __xchg_u64(volatile __u64 * m, __u64 val)
{
	__u64 retval;

	if (cpu_has_llsc && R10000_LLSC_WAR) {
		unsigned long dummy;

		__asm__ __volatile__(
		"	.set	mips3					\n"
		"1:	lld	%0, %3			# xchg_u64	\n"
		"	move	%2, %z4					\n"
		"	scd	%2, %1					\n"
		"	beqzl	%2, 1b					\n"
#ifdef CONFIG_SMP
		"	sync						\n"
#endif
		"	.set	mips0					\n"
		: "=&r" (retval), "=m" (*m), "=&r" (dummy)
		: "R" (*m), "Jr" (val)
		: "memory");
	} else if (cpu_has_llsc) {
		unsigned long dummy;

		__asm__ __volatile__(
		"	.set	mips3					\n"
		"1:	lld	%0, %3			# xchg_u64	\n"
		"	move	%2, %z4					\n"
		"	scd	%2, %1					\n"
		"	beqz	%2, 1b					\n"
#ifdef CONFIG_SMP
		"	sync						\n"
#endif
		"	.set	mips0					\n"
		: "=&r" (retval), "=m" (*m), "=&r" (dummy)
		: "R" (*m), "Jr" (val)
		: "memory");
	} else {
		unsigned long flags;

		local_irq_save(flags);
		retval = *m;
		*m = val;
		local_irq_restore(flags);	/* implies memory barrier  */
	}

	return retval;
}
#else
extern __u64 __xchg_u64_unsupported_on_32bit_kernels(volatile __u64 * m, __u64 val);
#define __xchg_u64 __xchg_u64_unsupported_on_32bit_kernels
#endif

/* This function doesn't exist, so you'll get a linker error
   if something tries to do an invalid xchg().  */
extern void __xchg_called_with_bad_pointer(void);

static inline unsigned long __xchg(unsigned long x, volatile void * ptr, int size)
{
	switch (size) {
		case 4:
			return __xchg_u32(ptr, x);
		case 8:
			return __xchg_u64(ptr, x);
	}
	__xchg_called_with_bad_pointer();
	return x;
}

#define xchg(ptr,x) ((__typeof__(*(ptr)))__xchg((unsigned long)(x),(ptr),sizeof(*(ptr))))
#define tas(ptr) (xchg((ptr),1))

#define __HAVE_ARCH_CMPXCHG 1

static inline unsigned long __cmpxchg_u32(volatile int * m, unsigned long old,
	unsigned long new)
{
	__u32 retval;

	if (cpu_has_llsc && R10000_LLSC_WAR) {
		__asm__ __volatile__(
		"	.set	push					\n"
		"	.set	noat					\n"
		"	.set	mips3					\n"
		"1:	ll	%0, %2			# __cmpxchg_u32	\n"
		"	bne	%0, %z3, 2f				\n"
		"	.set	mips0					\n"
		"	move	$1, %z4					\n"
		"	.set	mips3					\n"
		"	sc	$1, %1					\n"
		"	beqzl	$1, 1b					\n"
#ifdef CONFIG_SMP
		"	sync						\n"
#endif
		"2:							\n"
		"	.set	pop					\n"
		: "=&r" (retval), "=m" (*m)
		: "R" (*m), "Jr" (old), "Jr" (new)
		: "memory");
	} else if (cpu_has_llsc) {
		__asm__ __volatile__(
		"	.set	push					\n"
		"	.set	noat					\n"
		"	.set	mips3					\n"
		"1:	ll	%0, %2			# __cmpxchg_u32	\n"
		"	bne	%0, %z3, 2f				\n"
		"	.set	mips0					\n"
		"	move	$1, %z4					\n"
		"	.set	mips3					\n"
		"	sc	$1, %1					\n"
		"	beqz	$1, 1b					\n"
#ifdef CONFIG_SMP
		"	sync						\n"
#endif
		"2:							\n"
		"	.set	pop					\n"
		: "=&r" (retval), "=m" (*m)
		: "R" (*m), "Jr" (old), "Jr" (new)
		: "memory");
	} else {
		unsigned long flags;

		local_irq_save(flags);
		retval = *m;
		if (retval == old)
			*m = new;
		local_irq_restore(flags);	/* implies memory barrier  */
	}

	return retval;
}

#ifdef CONFIG_64BIT
static inline unsigned long __cmpxchg_u64(volatile int * m, unsigned long old,
	unsigned long new)
{
	__u64 retval;

	if (cpu_has_llsc) {
		__asm__ __volatile__(
		"	.set	push					\n"
		"	.set	noat					\n"
		"	.set	mips3					\n"
		"1:	lld	%0, %2			# __cmpxchg_u64	\n"
		"	bne	%0, %z3, 2f				\n"
		"	move	$1, %z4					\n"
		"	scd	$1, %1					\n"
		"	beqzl	$1, 1b					\n"
#ifdef CONFIG_SMP
		"	sync						\n"
#endif
		"2:							\n"
		"	.set	pop					\n"
		: "=&r" (retval), "=m" (*m)
		: "R" (*m), "Jr" (old), "Jr" (new)
		: "memory");
	} else if (cpu_has_llsc) {
		__asm__ __volatile__(
		"	.set	push					\n"
		"	.set	noat					\n"
		"	.set	mips3					\n"
		"1:	lld	%0, %2			# __cmpxchg_u64	\n"
		"	bne	%0, %z3, 2f				\n"
		"	move	$1, %z4					\n"
		"	scd	$1, %1					\n"
		"	beqz	$1, 1b					\n"
#ifdef CONFIG_SMP
		"	sync						\n"
#endif
		"2:							\n"
		"	.set	pop					\n"
		: "=&r" (retval), "=m" (*m)
		: "R" (*m), "Jr" (old), "Jr" (new)
		: "memory");
	} else {
		unsigned long flags;

		local_irq_save(flags);
		retval = *m;
		if (retval == old)
			*m = new;
		local_irq_restore(flags);	/* implies memory barrier  */
	}

	return retval;
}
#else
extern unsigned long __cmpxchg_u64_unsupported_on_32bit_kernels(
	volatile int * m, unsigned long old, unsigned long new);
#define __cmpxchg_u64 __cmpxchg_u64_unsupported_on_32bit_kernels
#endif

/* This function doesn't exist, so you'll get a linker error
   if something tries to do an invalid cmpxchg().  */
extern void __cmpxchg_called_with_bad_pointer(void);

static inline unsigned long __cmpxchg(volatile void * ptr, unsigned long old,
	unsigned long new, int size)
{
	switch (size) {
	case 4:
		return __cmpxchg_u32(ptr, old, new);
	case 8:
		return __cmpxchg_u64(ptr, old, new);
	}
	__cmpxchg_called_with_bad_pointer();
	return old;
}

#define cmpxchg(ptr,old,new) ((__typeof__(*(ptr)))__cmpxchg((ptr), (unsigned long)(old), (unsigned long)(new),sizeof(*(ptr))))

extern void set_handler (unsigned long offset, void *addr, unsigned long len);
extern void set_uncached_handler (unsigned long offset, void *addr, unsigned long len);
extern void *set_vi_handler (int n, void *addr);
extern void *set_vi_srs_handler (int n, void *addr, int regset);
extern void *set_except_vector(int n, void *addr);
extern void per_cpu_trap_init(void);

extern NORET_TYPE void die(const char *, struct pt_regs *);

static inline void die_if_kernel(const char *str, struct pt_regs *regs)
{
	if (unlikely(!user_mode(regs)))
		die(str, regs);
}

extern int stop_a_enabled;

/*
 * See include/asm-ia64/system.h; prevents deadlock on SMP
 * systems.
 */
#define __ARCH_WANT_UNLOCKED_CTXSW

#define arch_align_stack(x) (x)

#endif /* _ASM_SYSTEM_H */
