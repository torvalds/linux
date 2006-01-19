#ifndef __M68K_HARDIRQ_H
#define __M68K_HARDIRQ_H

#include <linux/config.h>
#include <linux/threads.h>
#include <linux/cache.h>

/* entry.S is sensitive to the offsets of these fields */
typedef struct {
	unsigned int __softirq_pending;
} ____cacheline_aligned irq_cpustat_t;

#include <linux/irq_cpustat.h>	/* Standard mappings for irq_cpustat_t above */

#define HARDIRQ_BITS	8

#endif
