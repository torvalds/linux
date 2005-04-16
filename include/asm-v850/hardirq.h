#ifndef __V850_HARDIRQ_H__
#define __V850_HARDIRQ_H__

#include <linux/config.h>
#include <linux/threads.h>
#include <linux/cache.h>

typedef struct {
	unsigned int __softirq_pending;
} ____cacheline_aligned irq_cpustat_t;

#include <linux/irq_cpustat.h>	/* Standard mappings for irq_cpustat_t above */

#define HARDIRQ_BITS	8

/*
 * The hardirq mask has to be large enough to have
 * space for potentially all IRQ sources in the system
 * nesting on a single CPU:
 */
#if (1 << HARDIRQ_BITS) < NR_IRQS
# error HARDIRQ_BITS is too low!
#endif

#endif /* __V850_HARDIRQ_H__ */
