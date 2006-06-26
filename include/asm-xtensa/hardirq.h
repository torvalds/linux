/*
 * include/asm-xtensa/hardirq.h
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file "COPYING" in the main directory of
 * this archive for more details.
 *
 * Copyright (C) 2002 - 2005 Tensilica Inc.
 */

#ifndef _XTENSA_HARDIRQ_H
#define _XTENSA_HARDIRQ_H

#include <linux/cache.h>
#include <asm/irq.h>

/* headers.S is sensitive to the offsets of these fields */
typedef struct {
	unsigned int __softirq_pending;
	unsigned int __syscall_count;
	struct task_struct * __ksoftirqd_task; /* waitqueue is too large */
	unsigned int __nmi_count;	       /* arch dependent */
} ____cacheline_aligned irq_cpustat_t;

void ack_bad_irq(unsigned int irq);
#include <linux/irq_cpustat.h>	/* Standard mappings for irq_cpustat_t above */

#endif	/* _XTENSA_HARDIRQ_H */
