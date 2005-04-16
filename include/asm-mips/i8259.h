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

extern spinlock_t i8259A_lock;

extern void init_i8259_irqs(void);

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
	outb(0x0C, 0x20);		/* prepare for poll */
	irq = inb(0x20) & 7;
	if (irq == 2) {
		/*
		 * Interrupt is cascaded so perform interrupt
		 * acknowledge on controller 2.
		 */
		outb(0x0C, 0xA0);		/* prepare for poll */
		irq = (inb(0xA0) & 7) + 8;
	}

	if (unlikely(irq == 7)) {
		/*
		 * This may be a spurious interrupt.
		 *
		 * Read the interrupt status register (ISR). If the most
		 * significant bit is not set then there is no valid
		 * interrupt.
		 */
		outb(0x0B, 0x20);		/* ISR register */
		if(~inb(0x20) & 0x80)
			irq = -1;
	}

	spin_unlock(&i8259A_lock);

	return irq;
}

#endif /* _ASM_I8259_H */
