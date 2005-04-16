/*
 * Copyright (C) 1999 Cort Dougan <cort@cs.nmt.edu>
 *
 * Use inline IRQs where possible - Anton Blanchard <anton@au.ibm.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#ifdef __KERNEL__
#ifndef _PPC64_HW_IRQ_H
#define _PPC64_HW_IRQ_H

#include <linux/config.h>
#include <linux/errno.h>
#include <asm/irq.h>

int timer_interrupt(struct pt_regs *);
extern void ppc_irq_dispatch_handler(struct pt_regs *regs, int irq);

#ifdef CONFIG_PPC_ISERIES

extern unsigned long local_get_flags(void);
extern unsigned long local_irq_disable(void);
extern void local_irq_restore(unsigned long);

#define local_irq_enable()	local_irq_restore(1)
#define local_save_flags(flags)	((flags) = local_get_flags())
#define local_irq_save(flags)	((flags) = local_irq_disable())

#define irqs_disabled()		(local_get_flags() == 0)

#else

#define local_save_flags(flags)	((flags) = mfmsr())
#define local_irq_restore(flags) do { \
	__asm__ __volatile__("": : :"memory"); \
	__mtmsrd((flags), 1); \
} while(0)

static inline void local_irq_disable(void)
{
	unsigned long msr;
	msr = mfmsr();
	__mtmsrd(msr & ~MSR_EE, 1);
	__asm__ __volatile__("": : :"memory");
}

static inline void local_irq_enable(void)
{
	unsigned long msr;
	__asm__ __volatile__("": : :"memory");
	msr = mfmsr();
	__mtmsrd(msr | MSR_EE, 1);
}

static inline void __do_save_and_cli(unsigned long *flags)
{
	unsigned long msr;
	msr = mfmsr();
	*flags = msr;
	__mtmsrd(msr & ~MSR_EE, 1);
	__asm__ __volatile__("": : :"memory");
}

#define local_irq_save(flags)          __do_save_and_cli(&flags)

#define irqs_disabled()				\
({						\
	unsigned long flags;			\
	local_save_flags(flags);		\
	!(flags & MSR_EE);			\
})

#endif /* CONFIG_PPC_ISERIES */

#define mask_irq(irq)						\
	({							\
	 	irq_desc_t *desc = get_irq_desc(irq);		\
		if (desc->handler && desc->handler->disable)	\
			desc->handler->disable(irq);		\
	})
#define unmask_irq(irq)						\
	({							\
	 	irq_desc_t *desc = get_irq_desc(irq);		\
		if (desc->handler && desc->handler->enable)	\
			desc->handler->enable(irq);		\
	})
#define ack_irq(irq)						\
	({							\
	 	irq_desc_t *desc = get_irq_desc(irq);		\
		if (desc->handler && desc->handler->ack)	\
			desc->handler->ack(irq);		\
	})

/* Should we handle this via lost interrupts and IPIs or should we don't care like
 * we do now ? --BenH.
 */
struct hw_interrupt_type;
static inline void hw_resend_irq(struct hw_interrupt_type *h, unsigned int i) {}
 
#endif /* _PPC64_HW_IRQ_H */
#endif /* __KERNEL__ */
