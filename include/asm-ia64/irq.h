#ifndef _ASM_IA64_IRQ_H
#define _ASM_IA64_IRQ_H

/*
 * Copyright (C) 1999-2000, 2002 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 *	Stephane Eranian <eranian@hpl.hp.com>
 *
 * 11/24/98	S.Eranian 	updated TIMER_IRQ and irq_canonicalize
 * 01/20/99	S.Eranian	added keyboard interrupt
 * 02/29/00     D.Mosberger	moved most things into hw_irq.h
 */

#include <linux/types.h>
#include <linux/cpumask.h>

#define NR_VECTORS	256

#if (NR_VECTORS + 32 * NR_CPUS) < 1024
#define NR_IRQS (NR_VECTORS + 32 * NR_CPUS)
#else
#define NR_IRQS 1024
#endif

static __inline__ int
irq_canonicalize (int irq)
{
	/*
	 * We do the legacy thing here of pretending that irqs < 16
	 * are 8259 irqs.  This really shouldn't be necessary at all,
	 * but we keep it here as serial.c still uses it...
	 */
	return ((irq == 2) ? 9 : irq);
}

extern void disable_irq (unsigned int);
extern void disable_irq_nosync (unsigned int);
extern void enable_irq (unsigned int);
extern void set_irq_affinity_info (unsigned int irq, int dest, int redir);
bool is_affinity_mask_valid(cpumask_t cpumask);

#define is_affinity_mask_valid is_affinity_mask_valid

#endif /* _ASM_IA64_IRQ_H */
