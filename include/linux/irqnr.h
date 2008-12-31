#ifndef _LINUX_IRQNR_H
#define _LINUX_IRQNR_H

/*
 * Generic irq_desc iterators:
 */
#ifdef __KERNEL__

#ifndef CONFIG_GENERIC_HARDIRQS
#include <asm/irq.h>
# define nr_irqs		NR_IRQS

# define for_each_irq_desc(irq, desc)		\
	for (irq = 0; irq < nr_irqs; irq++)

# define for_each_irq_desc_reverse(irq, desc)                          \
	for (irq = nr_irqs - 1; irq >= 0; irq--)
#else

extern int nr_irqs;

#ifndef CONFIG_SPARSE_IRQ

struct irq_desc;
# define for_each_irq_desc(irq, desc)		\
	for (irq = 0, desc = irq_desc; irq < nr_irqs; irq++, desc++)
# define for_each_irq_desc_reverse(irq, desc)                          \
	for (irq = nr_irqs - 1, desc = irq_desc + (nr_irqs - 1);        \
	    irq >= 0; irq--, desc--)
#endif
#endif

#define for_each_irq_nr(irq)                   \
       for (irq = 0; irq < nr_irqs; irq++)

#endif /* __KERNEL__ */

#endif
