/* (c) 2004 cw@f00f.org, GPLv2 blah blah */

#ifndef __ASM_UM_HARDIRQ_H
#define __ASM_UM_HARDIRQ_H

#include <linux/config.h>
#include <linux/threads.h>
#include <linux/irq.h>

/* NOTE: When SMP works again we might want to make this
 * ____cacheline_aligned or maybe use per_cpu state? --cw */
typedef struct {
	unsigned int __softirq_pending;
} irq_cpustat_t;

#include <linux/irq_cpustat.h>

/* As this would be very strange for UML to get we BUG() after the
 * printk. */
static inline void ack_bad_irq(unsigned int irq)
{
	printk(KERN_ERR "unexpected IRQ %02x\n", irq);
	BUG();
}

#endif /* __ASM_UM_HARDIRQ_H */
