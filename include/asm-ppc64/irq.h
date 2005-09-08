#ifdef __KERNEL__
#ifndef _ASM_IRQ_H
#define _ASM_IRQ_H

/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/config.h>
#include <linux/threads.h>

/*
 * Maximum number of interrupt sources that we can handle.
 */
#define NR_IRQS		512

/* this number is used when no interrupt has been assigned */
#define NO_IRQ			(-1)

/*
 * These constants are used for passing information about interrupt
 * signal polarity and level/edge sensing to the low-level PIC chip
 * drivers.
 */
#define IRQ_SENSE_MASK		0x1
#define IRQ_SENSE_LEVEL		0x1	/* interrupt on active level */
#define IRQ_SENSE_EDGE		0x0	/* interrupt triggered by edge */

#define IRQ_POLARITY_MASK	0x2
#define IRQ_POLARITY_POSITIVE	0x2	/* high level or low->high edge */
#define IRQ_POLARITY_NEGATIVE	0x0	/* low level or high->low edge */

/*
 * IRQ line status macro IRQ_PER_CPU is used
 */
#define ARCH_HAS_IRQ_PER_CPU

#define get_irq_desc(irq) (&irq_desc[(irq)])

/* Define a way to iterate across irqs. */
#define for_each_irq(i) \
	for ((i) = 0; (i) < NR_IRQS; ++(i))

/* Interrupt numbers are virtual in case they are sparsely
 * distributed by the hardware.
 */
extern unsigned int virt_irq_to_real_map[NR_IRQS];

/* Create a mapping for a real_irq if it doesn't already exist.
 * Return the virtual irq as a convenience.
 */
int virt_irq_create_mapping(unsigned int real_irq);
void virt_irq_init(void);

static inline unsigned int virt_irq_to_real(unsigned int virt_irq)
{
	return virt_irq_to_real_map[virt_irq];
}

extern unsigned int real_irq_to_virt_slowpath(unsigned int real_irq);

/*
 * Because many systems have two overlapping names spaces for
 * interrupts (ISA and XICS for example), and the ISA interrupts
 * have historically not been easy to renumber, we allow ISA
 * interrupts to take values 0 - 15, and shift up the remaining
 * interrupts by 0x10.
 */
#define NUM_ISA_INTERRUPTS	0x10
extern int __irq_offset_value;

static inline int irq_offset_up(int irq)
{
	return(irq + __irq_offset_value);
}

static inline int irq_offset_down(int irq)
{
	return(irq - __irq_offset_value);
}

static inline int irq_offset_value(void)
{
	return __irq_offset_value;
}

static __inline__ int irq_canonicalize(int irq)
{
	return irq;
}

extern int distribute_irqs;

struct irqaction;
struct pt_regs;

#ifdef CONFIG_IRQSTACKS
/*
 * Per-cpu stacks for handling hard and soft interrupts.
 */
extern struct thread_info *hardirq_ctx[NR_CPUS];
extern struct thread_info *softirq_ctx[NR_CPUS];

extern void irq_ctx_init(void);
extern void call_do_softirq(struct thread_info *tp);
extern int call_handle_IRQ_event(int irq, struct pt_regs *regs,
			struct irqaction *action, struct thread_info *tp);

#define __ARCH_HAS_DO_SOFTIRQ

#else
#define irq_ctx_init()

#endif /* CONFIG_IRQSTACKS */

#endif /* _ASM_IRQ_H */
#endif /* __KERNEL__ */
