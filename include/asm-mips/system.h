/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994, 95, 96, 97, 98, 99, 2003, 06 by Ralf Baechle
 * Copyright (C) 1996 by Paul M. Antoine
 * Copyright (C) 1999 Silicon Graphics
 * Kevin D. Kissell, kevink@mips.org and Carsten Langgaard, carstenl@mips.com
 * Copyright (C) 2000 MIPS Technologies, Inc.
 */
#ifndef _ASM_SYSTEM_H
#define _ASM_SYSTEM_H

#include <linux/types.h>
#include <linux/irqflags.h>

#include <asm/addrspace.h>
#include <asm/barrier.h>
#include <asm/cmpxchg.h>
#include <asm/cpu-features.h>
#include <asm/dsp.h>
#include <asm/war.h>


/*
 * switch_to(n) should switch tasks to task nr n, first
 * checking that n isn't the current task, in which case it does nothing.
 */
extern asmlinkage void *resume(void *last, void *next, void *next_ti);

struct task_struct;

#ifdef CONFIG_MIPS_MT_FPAFF

/*
 * Handle the scheduler resume end of FPU affinity management.  We do this
 * inline to try to keep the overhead down. If we have been forced to run on
 * a "CPU" with an FPU because of a previous high level of FP computation,
 * but did not actually use the FPU during the most recent time-slice (CU1
 * isn't set), we undo the restriction on cpus_allowed.
 *
 * We're not calling set_cpus_allowed() here, because we have no need to
 * force prompt migration - we're already switching the current CPU to a
 * different thread.
 */

#define __mips_mt_fpaff_switch_to(prev)					\
do {									\
	struct thread_info *__prev_ti = task_thread_info(prev);		\
									\
	if (cpu_has_fpu &&						\
	    test_ti_thread_flag(__prev_ti, TIF_FPUBOUND) &&		\
	    (!(KSTK_STATUS(prev) & ST0_CU1))) {				\
		clear_ti_thread_flag(__prev_ti, TIF_FPUBOUND);		\
		prev->cpus_allowed = prev->thread.user_cpus_allowed;	\
	}								\
	next->thread.emulated_fp = 0;					\
} while(0)

#else
#define __mips_mt_fpaff_switch_to(prev) do { (void) (prev); } while (0)
#endif

#define switch_to(prev, next, last)					\
do {									\
	__mips_mt_fpaff_switch_to(prev);				\
	if (cpu_has_dsp)						\
		__save_dsp(prev);					\
	(last) = resume(prev, next, task_thread_info(next));		\
} while (0)

#define finish_arch_switch(prev)					\
do {									\
	if (cpu_has_dsp)						\
		__restore_dsp(current);					\
	if (cpu_has_userlocal)						\
		write_c0_userlocal(current_thread_info()->tp_value);	\
} while (0)

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
		"	beqz	%2, 2f					\n"
		"	.subsection 2					\n"
		"2:	b	1b					\n"
		"	.previous					\n"
		"	.set	mips0					\n"
		: "=&r" (retval), "=m" (*m), "=&r" (dummy)
		: "R" (*m), "Jr" (val)
		: "memory");
	} else {
		unsigned long flags;

		raw_local_irq_save(flags);
		retval = *m;
		*m = val;
		raw_local_irq_restore(flags);	/* implies memory barrier  */
	}

	smp_llsc_mb();

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
		"	beqz	%2, 2f					\n"
		"	.subsection 2					\n"
		"2:	b	1b					\n"
		"	.previous					\n"
		"	.set	mips0					\n"
		: "=&r" (retval), "=m" (*m), "=&r" (dummy)
		: "R" (*m), "Jr" (val)
		: "memory");
	} else {
		unsigned long flags;

		raw_local_irq_save(flags);
		retval = *m;
		*m = val;
		raw_local_irq_restore(flags);	/* implies memory barrier  */
	}

	smp_llsc_mb();

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

#define xchg(ptr, x) ((__typeof__(*(ptr)))__xchg((unsigned long)(x), (ptr), sizeof(*(ptr))))

extern void set_handler(unsigned long offset, void *addr, unsigned long len);
extern void set_uncached_handler(unsigned long offset, void *addr, unsigned long len);

typedef void (*vi_handler_t)(void);
extern void *set_vi_handler(int n, vi_handler_t addr);

extern void *set_except_vector(int n, void *addr);
extern unsigned long ebase;
extern void per_cpu_trap_init(void);

extern int stop_a_enabled;

/*
 * See include/asm-ia64/system.h; prevents deadlock on SMP
 * systems.
 */
#define __ARCH_WANT_UNLOCKED_CTXSW

extern unsigned long arch_align_stack(unsigned long sp);

#endif /* _ASM_SYSTEM_H */
