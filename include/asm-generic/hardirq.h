/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_GENERIC_HARDIRQ_H
#define __ASM_GENERIC_HARDIRQ_H

#include <linux/cache.h>
#include <linux/threads.h>

typedef struct {
	unsigned int __softirq_pending;
#ifdef ARCH_WANTS_NMI_IRQSTAT
	unsigned int __nmi_count;
#endif
} ____cacheline_aligned irq_cpustat_t;

DECLARE_PER_CPU_ALIGNED(irq_cpustat_t, irq_stat);

#include <linux/irq.h>

#ifndef ack_bad_irq
static inline void ack_bad_irq(unsigned int irq)
{
	printk(KERN_CRIT "unexpected IRQ trap at vector %02x\n", irq);
}
#endif

#endif /* __ASM_GENERIC_HARDIRQ_H */
