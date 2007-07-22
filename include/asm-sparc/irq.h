/* $Id: irq.h,v 1.32 2000/08/26 02:42:28 anton Exp $
 * irq.h: IRQ registers on the Sparc.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef _SPARC_IRQ_H
#define _SPARC_IRQ_H

#include <linux/interrupt.h>

#define NR_IRQS    16

#define irq_canonicalize(irq)	(irq)

extern void disable_irq_nosync(unsigned int irq);
extern void disable_irq(unsigned int irq);
extern void enable_irq(unsigned int irq);

extern int request_fast_irq(unsigned int irq, irq_handler_t handler, unsigned long flags, __const__ char *devname);

#endif
