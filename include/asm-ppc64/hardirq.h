#ifndef __ASM_HARDIRQ_H
#define __ASM_HARDIRQ_H

/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/config.h>
#include <linux/cache.h>
#include <linux/preempt.h>

typedef struct {
	unsigned int __softirq_pending;
} ____cacheline_aligned irq_cpustat_t;

#include <linux/irq_cpustat.h>	/* Standard mappings for irq_cpustat_t above */

static inline void ack_bad_irq(int irq)
{
	printk(KERN_CRIT "illegal vector %d received!\n", irq);
	BUG();
}

#endif /* __ASM_HARDIRQ_H */
