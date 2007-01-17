/*
 * include/asm-v850/irq.h -- Machine interrupt handling
 *
 *  Copyright (C) 2001,02,04  NEC Electronics Corporation
 *  Copyright (C) 2001,02,04  Miles Bader <miles@gnu.org>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 *
 * Written by Miles Bader <miles@gnu.org>
 */

#ifndef __V850_IRQ_H__
#define __V850_IRQ_H__

#include <asm/machdep.h>

/* Default NUM_MACH_IRQS.  */
#ifndef NUM_MACH_IRQS
#define NUM_MACH_IRQS	NUM_CPU_IRQS
#endif

/* NMIs have IRQ numbers from FIRST_NMI to FIRST_NMI+NUM_NMIS-1.  */
#define FIRST_NMI	NUM_MACH_IRQS
#define IRQ_NMI(n)	(FIRST_NMI + (n))
/* v850 processors have 3 non-maskable interrupts.  */
#define NUM_NMIS	3

/* Includes both maskable and non-maskable irqs.  */
#define NR_IRQS		(NUM_MACH_IRQS + NUM_NMIS)


#ifndef __ASSEMBLY__

struct pt_regs;
struct hw_interrupt_type;
struct irqaction;

#define irq_canonicalize(irq)	(irq)

/* Initialize irq handling for IRQs.
   BASE_IRQ, BASE_IRQ+INTERVAL, ..., BASE_IRQ+NUM*INTERVAL
   to IRQ_TYPE.  An IRQ_TYPE of 0 means to use a generic interrupt type.  */
extern void
init_irq_handlers (int base_irq, int num, int interval,
		   struct hw_interrupt_type *irq_type);

/* Handle interrupt IRQ.  REGS are the registers at the time of ther
   interrupt.  */
extern unsigned int handle_irq (int irq, struct pt_regs *regs);


/* Enable interrupt handling on an irq.  */
extern void enable_irq(unsigned int irq);

/* Disable an irq and wait for completion.  */
extern void disable_irq (unsigned int irq);

/* Disable an irq without waiting. */
extern void disable_irq_nosync (unsigned int irq);

#endif /* !__ASSEMBLY__ */

#endif /* __V850_IRQ_H__ */
