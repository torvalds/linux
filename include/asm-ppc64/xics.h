/* 
 * arch/ppc64/kernel/xics.h
 *
 * Copyright 2000 IBM Corporation.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 */

#ifndef _PPC64_KERNEL_XICS_H
#define _PPC64_KERNEL_XICS_H

#include <linux/cache.h>

void xics_init_IRQ(void);
int xics_get_irq(struct pt_regs *);
void xics_setup_cpu(void);
void xics_cause_IPI(int cpu);
void xics_request_IPIs(void);
void xics_migrate_irqs_away(void);

/* first argument is ignored for now*/
void pSeriesLP_cppr_info(int n_cpu, u8 value);

struct xics_ipi_struct {
	volatile unsigned long value;
} ____cacheline_aligned;

extern struct xics_ipi_struct xics_ipi_message[NR_CPUS] __cacheline_aligned;

extern unsigned int default_distrib_server;
extern unsigned int interrupt_server_size;

#endif /* _PPC64_KERNEL_XICS_H */
