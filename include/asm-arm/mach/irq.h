/*
 *  linux/include/asm-arm/mach/irq.h
 *
 *  Copyright (C) 1995-2000 Russell King.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __ASM_ARM_MACH_IRQ_H
#define __ASM_ARM_MACH_IRQ_H

#include <linux/irq.h>

struct seq_file;

/*
 * This is internal.  Do not use it.
 */
extern void (*init_arch_irq)(void);
extern void init_FIQ(void);
extern int show_fiq_list(struct seq_file *, void *);

/*
 * Function wrappers
 */
#define set_irq_chipdata(irq, d)	set_irq_chip_data(irq, d)
#define get_irq_chipdata(irq)		get_irq_chip_data(irq)

/*
 * Obsolete inline function for calling irq descriptor handlers.
 */
static inline void desc_handle_irq(unsigned int irq, struct irq_desc *desc)
{
	desc->handle_irq(irq, desc);
}

void set_irq_flags(unsigned int irq, unsigned int flags);

#define IRQF_VALID	(1 << 0)
#define IRQF_PROBE	(1 << 1)
#define IRQF_NOAUTOEN	(1 << 2)

/*
 * This is for easy migration, but should be changed in the source
 */
#define do_level_IRQ	handle_level_irq
#define do_edge_IRQ	handle_edge_irq
#define do_simple_IRQ	handle_simple_irq
#define irqdesc		irq_desc
#define irqchip		irq_chip

#define do_bad_IRQ(irq,desc)				\
do {							\
	spin_lock(&desc->lock);				\
	handle_bad_irq(irq, desc);			\
	spin_unlock(&desc->lock);			\
} while(0)

extern unsigned long irq_err_count;
static inline void ack_bad_irq(int irq)
{
	irq_err_count++;
}

#endif
