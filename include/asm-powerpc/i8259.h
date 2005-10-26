#ifndef _ASM_POWERPC_I8259_H
#define _ASM_POWERPC_I8259_H

#include <linux/irq.h>

extern struct hw_interrupt_type i8259_pic;

extern void i8259_init(unsigned long intack_addr, int offset);
extern int i8259_irq(struct pt_regs *regs);
extern int i8259_irq_cascade(struct pt_regs *regs, void *unused);

#endif /* _ASM_POWERPC_I8259_H */
