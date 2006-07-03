/* irq.h: FRV IRQ definitions
 *
 * Copyright (C) 2004 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef _ASM_IRQ_H_
#define _ASM_IRQ_H_


/*
 * the system has an on-CPU PIC and another PIC on the FPGA and other PICs on other peripherals,
 * so we do some routing in irq-routing.[ch] to reduce the number of false-positives seen by
 * drivers
 */

/* this number is used when no interrupt has been assigned */
#define NO_IRQ				(-1)

#define NR_IRQ_LOG2_ACTIONS_PER_GROUP	5
#define NR_IRQ_ACTIONS_PER_GROUP	(1 << NR_IRQ_LOG2_ACTIONS_PER_GROUP)
#define NR_IRQ_GROUPS			4
#define NR_IRQS				(NR_IRQ_ACTIONS_PER_GROUP * NR_IRQ_GROUPS)

/* probe returns a 32-bit IRQ mask:-/ */
#define MIN_PROBE_IRQ	(NR_IRQS - 32)

static inline int irq_canonicalize(int irq)
{
	return irq;
}

extern void disable_irq_nosync(unsigned int irq);
extern void disable_irq(unsigned int irq);
extern void enable_irq(unsigned int irq);


#endif /* _ASM_IRQ_H_ */
