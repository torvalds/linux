/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2000, 2001, 2002 by Ralf Baechle
 */
#ifndef __ASM_HW_IRQ_H
#define __ASM_HW_IRQ_H

#include <linux/profile.h>
#include <asm/atomic.h>

extern void disable_8259A_irq(unsigned int irq);
extern void enable_8259A_irq(unsigned int irq);
extern int i8259A_irq_pending(unsigned int irq);
extern void make_8259A_irq(unsigned int irq);
extern void init_8259A(int aeoi);

extern atomic_t irq_err_count;

/* This may not be apropriate for all machines, we'll see ...  */
static inline void hw_resend_irq(struct hw_interrupt_type *h, unsigned int i)
{
}

#endif /* __ASM_HW_IRQ_H */
