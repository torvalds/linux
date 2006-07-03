#ifndef _ASM_POWERPC_I8259_H
#define _ASM_POWERPC_I8259_H
#ifdef __KERNEL__

#include <linux/irq.h>

extern void i8259_init(unsigned long intack_addr, int offset);
extern int i8259_irq(struct pt_regs *regs);

#endif /* __KERNEL__ */
#endif /* _ASM_POWERPC_I8259_H */
