#ifndef __ASM_HARDIRQ_H
#define __ASM_HARDIRQ_H

#include <linux/threads.h>
#include <linux/irq.h>

typedef struct {
	unsigned int __softirq_pending;
	unsigned long idle_timestamp;
	unsigned int __nmi_count;	/* arch dependent */
	unsigned int apic_timer_irqs;	/* arch dependent */
	unsigned int irq0_irqs;
} ____cacheline_aligned irq_cpustat_t;

DECLARE_PER_CPU(irq_cpustat_t, irq_stat);
extern irq_cpustat_t irq_stat[];

#define __ARCH_IRQ_STAT
#define __IRQ_STAT(cpu, member) (per_cpu(irq_stat, cpu).member)

void ack_bad_irq(unsigned int irq);
#include <linux/irq_cpustat.h>

#endif /* __ASM_HARDIRQ_H */
