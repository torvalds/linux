/* hardirq.h: 64-bit Sparc hard IRQ support.
 *
 * Copyright (C) 1997, 1998 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef __SPARC64_HARDIRQ_H
#define __SPARC64_HARDIRQ_H

#include <linux/config.h>
#include <linux/threads.h>
#include <linux/spinlock.h>
#include <linux/cache.h>

/* rtrap.S is sensitive to the offsets of these fields */
typedef struct {
	unsigned int __softirq_pending;
} ____cacheline_aligned irq_cpustat_t;

#include <linux/irq_cpustat.h>	/* Standard mappings for irq_cpustat_t above */

#define HARDIRQ_BITS	8

#endif /* !(__SPARC64_HARDIRQ_H) */
