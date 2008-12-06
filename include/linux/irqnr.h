#ifndef _LINUX_IRQNR_H
#define _LINUX_IRQNR_H

#ifndef CONFIG_GENERIC_HARDIRQS
#include <asm/irq.h>
# define nr_irqs		NR_IRQS

# define for_each_irq_desc(irq, desc)		\
	for (irq = 0; irq < nr_irqs; irq++)

static inline early_sparse_irq_init(void)
{
}
#endif

#endif
