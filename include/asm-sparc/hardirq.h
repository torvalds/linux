/* hardirq.h: 32-bit Sparc hard IRQ support.
 *
 * Copyright (C) 1997 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1998-2000 Anton Blanchard (anton@samba.org)
 */

#ifndef __SPARC_HARDIRQ_H
#define __SPARC_HARDIRQ_H

#include <linux/config.h>
#include <linux/threads.h>
#include <linux/spinlock.h>
#include <linux/cache.h>

/* entry.S is sensitive to the offsets of these fields */ /* XXX P3 Is it? */
typedef struct {
	unsigned int __softirq_pending;
} ____cacheline_aligned irq_cpustat_t;

#include <linux/irq_cpustat.h>	/* Standard mappings for irq_cpustat_t above */

#define HARDIRQ_BITS    8

#endif /* __SPARC_HARDIRQ_H */
