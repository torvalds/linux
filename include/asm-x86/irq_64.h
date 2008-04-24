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

#define TIMER_IRQ 0

/*
 * 16 8259A IRQ's, 208 potential APIC interrupt sources.
 * Right now the APIC is mostly only used for SMP.
 * 256 vectors is an architectural limit. (we can have
 * more than 256 devices theoretically, but they will
 * have to use shared interrupts)
 * Since vectors 0x00-0x1f are used/reserved for the CPU,
 * the usable vector space is 0x20-0xff (224 vectors)
 */

/*
 * The maximum number of vectors supported by x86_64 processors
 * is limited to 256. For processors other than x86_64, NR_VECTORS
 * should be changed accordingly.
 */
#define NR_VECTORS 256

#define FIRST_SYSTEM_VECTOR	0xef   /* duplicated in hw_irq.h */

#define NR_IRQS (NR_VECTORS + (32 * NR_CPUS))
#define NR_IRQ_VECTORS NR_IRQS

static inline int irq_canonicalize(int irq)
{
	return ((irq == 2) ? 9 : irq);
}

#define ARCH_HAS_NMI_WATCHDOG		/* See include/linux/nmi.h */

#ifdef CONFIG_HOTPLUG_CPU
#include <linux/cpumask.h>
extern void fixup_irqs(cpumask_t map);
#endif

#define __ARCH_HAS_DO_SOFTIRQ 1

#endif /* _ASM_IRQ_H */
