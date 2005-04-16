#ifndef _PPC_KERNEL_i8259_H
#define _PPC_KERNEL_i8259_H

#include <linux/irq.h>

extern struct hw_interrupt_type i8259_pic;

extern void i8259_init(long intack_addr);
extern int i8259_irq(struct pt_regs *regs);

#endif /* _PPC_KERNEL_i8259_H */
