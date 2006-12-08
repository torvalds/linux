/*
 *	include/asm-mips/i8259.h
 *
 *	i8259A interrupt definitions.
 *
 *	Copyright (C) 2003  Maciej W. Rozycki
 *	Copyright (C) 2003  Ralf Baechle <ralf@linux-mips.org>
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 */
#ifndef _ASM_I8259_H
#define _ASM_I8259_H

#include <linux/compiler.h>
#include <linux/spinlock.h>

#include <asm/io.h>

/* i8259A PIC registers */
#define PIC_MASTER_CMD		0x20
#define PIC_MASTER_IMR		0x21
#define PIC_MASTER_ISR		PIC_MASTER_CMD
#define PIC_MASTER_POLL		PIC_MASTER_ISR
#define PIC_MASTER_OCW3		PIC_MASTER_ISR
#define PIC_SLAVE_CMD		0xa0
#define PIC_SLAVE_IMR		0xa1

/* i8259A PIC related value */
#define PIC_CASCADE_IR		2
#define MASTER_ICW4_DEFAULT	0x01
#define SLAVE_ICW4_DEFAULT	0x01
#define PIC_ICW4_AEOI		2

extern spinlock_t i8259A_lock;

extern void init_8259A(int auto_eoi);
extern void enable_8259A_irq(unsigned int irq);
extern void disable_8259A_irq(unsigned int irq);

extern void init_i8259_irqs(void);

#define I8259A_IRQ_BASE	0

/*
 * Do the traditional i8259 interrupt polling thing.  This is for the few
 * cases where no better interrupt acknowledge method is available and we
 * absolutely must touch the i8259.
 */
static inline int i8259_irq(void)
{
	int irq;

	spin_lock(&i8259A_lock);

	/* Perform an interrupt acknowledge cycle on controller 1. */
	outb(0x0C, PIC_MASTER_CMD);		/* prepare for poll */
	irq = inb(PIC_MASTER_CMD) & 7;
	if (irq == PIC_CASCADE_IR) {
		/*
		 * Interrupt is cascaded so perform interrupt
		 * acknowledge on controller 2.
		 */
		outb(0x0C, PIC_SLAVE_CMD);		/* prepare for poll */
		irq = (inb(PIC_SLAVE_CMD) & 7) + 8;
	}

	if (unlikely(irq == 7)) {
		/*
		 * This may be a spurious interrupt.
		 *
		 * Read the interrupt status register (ISR). If the most
		 * significant bit is not set then there is no valid
		 * interrupt.
		 */
		outb(0x0B, PIC_MASTER_ISR);		/* ISR register */
		if(~inb(PIC_MASTER_ISR) & 0x80)
			irq = -1;
	}

	spin_unlock(&i8259A_lock);

	return likely(irq >= 0) ? irq + I8259A_IRQ_BASE : irq;
}

#endif /* _ASM_I8259_H */
