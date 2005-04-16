#ifndef _ASM_IRQ_H
#define _ASM_IRQ_H

/*
 *	linux/include/asm/irq.h
 *
 *	(C) 1992, 1993 Linus Torvalds, (C) 1997 Ingo Molnar
 *
 *	IRQ/IPI changes taken from work by Thomas Radke
 *	<tomsoft@informatik.tu-chemnitz.de>
 */

#include <linux/config.h>
#include <linux/sched.h>
/* include comes from machine specific directory */
#include "irq_vectors.h"
#include <asm/thread_info.h>

static __inline__ int irq_canonicalize(int irq)
{
	return ((irq == 2) ? 9 : irq);
}

extern void release_vm86_irqs(struct task_struct *);

#ifdef CONFIG_X86_LOCAL_APIC
# define ARCH_HAS_NMI_WATCHDOG		/* See include/linux/nmi.h */
#endif

#ifdef CONFIG_4KSTACKS
  extern void irq_ctx_init(int cpu);
# define __ARCH_HAS_DO_SOFTIRQ
#else
# define irq_ctx_init(cpu) do { } while (0)
#endif

#ifdef CONFIG_IRQBALANCE
extern int irqbalance_disable(char *str);
#endif

#endif /* _ASM_IRQ_H */
