#ifndef _ASM_IA64_HARDIRQ_H
#define _ASM_IA64_HARDIRQ_H

/*
 * Modified 1998-2002, 2004 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 */


#include <linux/threads.h>
#include <linux/irq.h>

#include <asm/processor.h>

/*
 * No irq_cpustat_t for IA-64.  The data is held in the per-CPU data structure.
 */

#define __ARCH_IRQ_STAT	1

#define local_softirq_pending()		(local_cpu_data->softirq_pending)

#define HARDIRQ_BITS	14

/*
 * The hardirq mask has to be large enough to have space for potentially all IRQ sources
 * in the system nesting on a single CPU:
 */
#if (1 << HARDIRQ_BITS) < NR_IRQS
# error HARDIRQ_BITS is too low!
#endif

extern void __iomem *ipi_base_addr;

void ack_bad_irq(unsigned int irq);

#endif /* _ASM_IA64_HARDIRQ_H */
