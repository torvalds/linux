#ifndef _ALPHA_HARDIRQ_H
#define _ALPHA_HARDIRQ_H

#include <linux/config.h>
#include <linux/threads.h>
#include <linux/cache.h>


/* entry.S is sensitive to the offsets of these fields */
typedef struct {
	unsigned long __softirq_pending;
} ____cacheline_aligned irq_cpustat_t;

#include <linux/irq_cpustat.h>	/* Standard mappings for irq_cpustat_t above */

#define HARDIRQ_BITS	12

/*
 * The hardirq mask has to be large enough to have
 * space for potentially nestable IRQ sources in the system
 * to nest on a single CPU. On Alpha, interrupts are masked at the CPU
 * by IPL as well as at the system level. We only have 8 IPLs (UNIX PALcode)
 * so we really only have 8 nestable IRQs, but allow some overhead
 */
#if (1 << HARDIRQ_BITS) < 16
#error HARDIRQ_BITS is too low!
#endif

#endif /* _ALPHA_HARDIRQ_H */
