/* MN10300 System definitions
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */
#ifndef _ASM_SYSTEM_H
#define _ASM_SYSTEM_H

#include <asm/cpu-regs.h>

#ifdef __KERNEL__
#ifndef __ASSEMBLY__

#include <linux/kernel.h>

struct task_struct;
struct thread_struct;

extern asmlinkage
struct task_struct *__switch_to(struct thread_struct *prev,
				struct thread_struct *next,
				struct task_struct *prev_task);

/* context switching is now performed out-of-line in switch_to.S */
#define switch_to(prev, next, last)					\
do {									\
	current->thread.wchan = (u_long) __builtin_return_address(0);	\
	(last) = __switch_to(&(prev)->thread, &(next)->thread, (prev));	\
	mb();								\
	current->thread.wchan = 0;					\
} while (0)

#define arch_align_stack(x) (x)

#define nop() asm volatile ("nop")

#endif /* !__ASSEMBLY__ */

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

#define mb()	asm volatile ("": : :"memory")
#define rmb()	mb()
#define wmb()	asm volatile ("": : :"memory")

#ifdef CONFIG_SMP
#define smp_mb()	mb()
#define smp_rmb()	rmb()
#define smp_wmb()	wmb()
#else
#define smp_mb()	barrier()
#define smp_rmb()	barrier()
#define smp_wmb()	barrier()
#endif

#define set_mb(var, value)  do { var = value;  mb(); } while (0)
#define set_wmb(var, value) do { var = value; wmb(); } while (0)

#define read_barrier_depends()		do {} while (0)
#define smp_read_barrier_depends()	do {} while (0)

/*****************************************************************************/
/*
 * interrupt control
 * - "disabled": run in IM1/2
 *   - level 0 - GDB stub
 *   - level 1 - virtual serial DMA (if present)
 *   - level 5 - normal interrupt priority
 *   - level 6 - timer interrupt
 * - "enabled":  run in IM7
 */
#ifdef CONFIG_MN10300_TTYSM
#define MN10300_CLI_LEVEL	EPSW_IM_2
#else
#define MN10300_CLI_LEVEL	EPSW_IM_1
#endif

#define local_save_flags(x)			\
do {						\
	typecheck(unsigned long, x);		\
	asm volatile(				\
		"	mov epsw,%0	\n"	\
		: "=d"(x)			\
		);				\
} while (0)

#define local_irq_disable()						\
do {									\
	asm volatile(							\
		"	and %0,epsw	\n"				\
		"	or %1,epsw	\n"				\
		"	nop		\n"				\
		"	nop		\n"				\
		"	nop		\n"				\
		:							\
		: "i"(~EPSW_IM), "i"(EPSW_IE | MN10300_CLI_LEVEL)	\
		);							\
} while (0)

#define local_irq_save(x)			\
do {						\
	local_save_flags(x);			\
	local_irq_disable();			\
} while (0)

/*
 * we make sure local_irq_enable() doesn't cause priority inversion
 */
#ifndef __ASSEMBLY__

extern unsigned long __mn10300_irq_enabled_epsw;

#endif

#define local_irq_enable()						\
do {									\
	unsigned long tmp;						\
									\
	asm volatile(							\
		"	mov	epsw,%0		\n"			\
		"	and	%1,%0		\n"			\
		"	or	%2,%0		\n"			\
		"	mov	%0,epsw		\n"			\
		: "=&d"(tmp)						\
		: "i"(~EPSW_IM), "r"(__mn10300_irq_enabled_epsw)	\
		);							\
} while (0)

#define local_irq_restore(x)			\
do {						\
	typecheck(unsigned long, x);		\
	asm volatile(				\
		"	mov %0,epsw	\n"	\
		"	nop		\n"	\
		"	nop		\n"	\
		"	nop		\n"	\
		:				\
		: "d"(x)			\
		: "memory", "cc"		\
		);				\
} while (0)

#define irqs_disabled()				\
({						\
	unsigned long flags;			\
	local_save_flags(flags);		\
	(flags & EPSW_IM) <= MN10300_CLI_LEVEL;	\
})

/* hook to save power by halting the CPU
 * - called from the idle loop
 * - must reenable interrupts (which takes three instruction cycles to complete)
 */
#define safe_halt()							\
do {									\
	asm volatile("	or	%0,epsw	\n"				\
		     "	nop		\n"				\
		     "	nop		\n"				\
		     "	bset	%2,(%1)	\n"				\
		     :							\
		     : "i"(EPSW_IE|EPSW_IM), "n"(&CPUM), "i"(CPUM_SLEEP)\
		     : "cc"						\
		     );							\
} while (0)

#define STI	or EPSW_IE|EPSW_IM,epsw
#define CLI	and ~EPSW_IM,epsw; or EPSW_IE|MN10300_CLI_LEVEL,epsw; nop; nop; nop

/*****************************************************************************/
/*
 * MN10300 doesn't actually have an exchange instruction
 */
#ifndef __ASSEMBLY__

struct __xchg_dummy { unsigned long a[100]; };
#define __xg(x) ((struct __xchg_dummy *)(x))

static inline
unsigned long __xchg(volatile unsigned long *m, unsigned long val)
{
	unsigned long retval;
	unsigned long flags;

	local_irq_save(flags);
	retval = *m;
	*m = val;
	local_irq_restore(flags);
	return retval;
}

#define xchg(ptr, v)						\
	((__typeof__(*(ptr))) __xchg((unsigned long *)(ptr),	\
				     (unsigned long)(v)))

static inline unsigned long __cmpxchg(volatile unsigned long *m,
				      unsigned long old, unsigned long new)
{
	unsigned long retval;
	unsigned long flags;

	local_irq_save(flags);
	retval = *m;
	if (retval == old)
		*m = new;
	local_irq_restore(flags);
	return retval;
}

#define cmpxchg(ptr, o, n)					\
	((__typeof__(*(ptr))) __cmpxchg((unsigned long *)(ptr), \
					(unsigned long)(o),	\
					(unsigned long)(n)))

#endif /* !__ASSEMBLY__ */

#endif /* __KERNEL__ */
#endif /* _ASM_SYSTEM_H */
