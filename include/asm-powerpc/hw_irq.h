/*
 * Copyright (C) 1999 Cort Dougan <cort@cs.nmt.edu>
 */
#ifndef _ASM_POWERPC_HW_IRQ_H
#define _ASM_POWERPC_HW_IRQ_H

#ifdef __KERNEL__

#include <linux/errno.h>
#include <linux/compiler.h>
#include <asm/ptrace.h>
#include <asm/processor.h>

extern void timer_interrupt(struct pt_regs *);

#ifdef CONFIG_PPC64
#include <asm/paca.h>

static inline unsigned long local_get_flags(void)
{
	return get_paca()->soft_enabled;
}

static inline unsigned long local_irq_disable(void)
{
	unsigned long flag = get_paca()->soft_enabled;
	get_paca()->soft_enabled = 0;
	barrier();
	return flag;
}

extern void local_irq_restore(unsigned long);
extern void iseries_handle_interrupts(void);

#define local_irq_enable()	local_irq_restore(1)
#define local_save_flags(flags)	((flags) = local_get_flags())
#define local_irq_save(flags)	((flags) = local_irq_disable())

#define irqs_disabled()		(local_get_flags() == 0)

#define hard_irq_enable()	__mtmsrd(mfmsr() | MSR_EE, 1)
#define hard_irq_disable()	__mtmsrd(mfmsr() & ~MSR_EE, 1)

#else

#if defined(CONFIG_BOOKE)
#define SET_MSR_EE(x)	mtmsr(x)
#define local_irq_restore(flags)	__asm__ __volatile__("wrtee %0" : : "r" (flags) : "memory")
#else
#define SET_MSR_EE(x)	mtmsr(x)
#define local_irq_restore(flags)	mtmsr(flags)
#endif

static inline void local_irq_disable(void)
{
#ifdef CONFIG_BOOKE
	__asm__ __volatile__("wrteei 0": : :"memory");
#else
	unsigned long msr;
	__asm__ __volatile__("": : :"memory");
	msr = mfmsr();
	SET_MSR_EE(msr & ~MSR_EE);
#endif
}

static inline void local_irq_enable(void)
{
#ifdef CONFIG_BOOKE
	__asm__ __volatile__("wrteei 1": : :"memory");
#else
	unsigned long msr;
	__asm__ __volatile__("": : :"memory");
	msr = mfmsr();
	SET_MSR_EE(msr | MSR_EE);
#endif
}

static inline void local_irq_save_ptr(unsigned long *flags)
{
	unsigned long msr;
	msr = mfmsr();
	*flags = msr;
#ifdef CONFIG_BOOKE
	__asm__ __volatile__("wrteei 0": : :"memory");
#else
	SET_MSR_EE(msr & ~MSR_EE);
#endif
	__asm__ __volatile__("": : :"memory");
}

#define local_save_flags(flags)	((flags) = mfmsr())
#define local_irq_save(flags)	local_irq_save_ptr(&flags)
#define irqs_disabled()		((mfmsr() & MSR_EE) == 0)

#endif /* CONFIG_PPC64 */

#define mask_irq(irq)						\
	({							\
	 	irq_desc_t *desc = get_irq_desc(irq);		\
		if (desc->chip && desc->chip->disable)	\
			desc->chip->disable(irq);		\
	})
#define unmask_irq(irq)						\
	({							\
	 	irq_desc_t *desc = get_irq_desc(irq);		\
		if (desc->chip && desc->chip->enable)	\
			desc->chip->enable(irq);		\
	})
#define ack_irq(irq)						\
	({							\
	 	irq_desc_t *desc = get_irq_desc(irq);		\
		if (desc->chip && desc->chip->ack)	\
			desc->chip->ack(irq);		\
	})

/*
 * interrupt-retrigger: should we handle this via lost interrupts and IPIs
 * or should we not care like we do now ? --BenH.
 */
struct hw_interrupt_type;

#endif	/* __KERNEL__ */
#endif	/* _ASM_POWERPC_HW_IRQ_H */
