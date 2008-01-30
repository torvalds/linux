#ifndef __ASM_SYSTEM_H
#define __ASM_SYSTEM_H

#include <asm/segment.h>
#include <asm/cpufeature.h>
#include <asm/cmpxchg.h>

#ifdef __KERNEL__
#define AT_VECTOR_SIZE_ARCH 2 /* entries in ARCH_DLINFO */

struct task_struct;	/* one of the stranger aspects of C forward declarations.. */
extern struct task_struct * FASTCALL(__switch_to(struct task_struct *prev, struct task_struct *next));

/*
 * Saving eflags is important. It switches not only IOPL between tasks,
 * it also protects other tasks from NT leaking through sysenter etc.
 */
#define switch_to(prev,next,last) do {					\
	unsigned long esi,edi;						\
	asm volatile("pushfl\n\t"		/* Save flags */	\
		     "pushl %%ebp\n\t"					\
		     "movl %%esp,%0\n\t"	/* save ESP */		\
		     "movl %5,%%esp\n\t"	/* restore ESP */	\
		     "movl $1f,%1\n\t"		/* save EIP */		\
		     "pushl %6\n\t"		/* restore EIP */	\
		     "jmp __switch_to\n"				\
		     "1:\t"						\
		     "popl %%ebp\n\t"					\
		     "popfl"						\
		     :"=m" (prev->thread.sp),"=m" (prev->thread.ip),	\
		      "=a" (last),"=S" (esi),"=D" (edi)			\
		     :"m" (next->thread.sp),"m" (next->thread.ip),	\
		      "2" (prev), "d" (next));				\
} while (0)

#endif	/* __KERNEL__ */


/*
 * Force strict CPU ordering.
 * And yes, this is required on UP too when we're talking
 * to devices.
 *
 * For now, "wmb()" doesn't actually do anything, as all
 * Intel CPU's follow what Intel calls a *Processor Order*,
 * in which all writes are seen in the program order even
 * outside the CPU.
 *
 * I expect future Intel CPU's to have a weaker ordering,
 * but I'd also expect them to finally get their act together
 * and add some real memory barriers if so.
 *
 * Some non intel clones support out of order store. wmb() ceases to be a
 * nop for these.
 */
 

#define mb() alternative("lock; addl $0,0(%%esp)", "mfence", X86_FEATURE_XMM2)
#define rmb() alternative("lock; addl $0,0(%%esp)", "lfence", X86_FEATURE_XMM2)
#define wmb() alternative("lock; addl $0,0(%%esp)", "sfence", X86_FEATURE_XMM)

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
 **/

#define read_barrier_depends()	do { } while(0)

#ifdef CONFIG_SMP
#define smp_mb()	mb()
#ifdef CONFIG_X86_PPRO_FENCE
# define smp_rmb()	rmb()
#else
# define smp_rmb()	barrier()
#endif
#ifdef CONFIG_X86_OOSTORE
# define smp_wmb() 	wmb()
#else
# define smp_wmb()	barrier()
#endif
#define smp_read_barrier_depends()	read_barrier_depends()
#define set_mb(var, value) do { (void) xchg(&var, value); } while (0)
#else
#define smp_mb()	barrier()
#define smp_rmb()	barrier()
#define smp_wmb()	barrier()
#define smp_read_barrier_depends()	do { } while(0)
#define set_mb(var, value) do { var = value; barrier(); } while (0)
#endif

#include <linux/irqflags.h>

/*
 * disable hlt during certain critical i/o operations
 */
#define HAVE_DISABLE_HLT

#endif
