#ifndef __ASM_SH64_HARDIRQ_H
#define __ASM_SH64_HARDIRQ_H

#include <linux/threads.h>
#include <linux/irq.h>

/* entry.S is sensitive to the offsets of these fields */
typedef struct {
	unsigned int __softirq_pending;
} ____cacheline_aligned irq_cpustat_t;

#include <linux/irq_cpustat.h>	/* Standard mappings for irq_cpustat_t above */

/* arch/sh64/kernel/irq.c */
extern void ack_bad_irq(unsigned int irq);

#endif /* __ASM_SH64_HARDIRQ_H */

