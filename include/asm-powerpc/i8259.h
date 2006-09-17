#ifndef _ASM_POWERPC_I8259_H
#define _ASM_POWERPC_I8259_H
#ifdef __KERNEL__

#include <linux/irq.h>

#ifdef CONFIG_PPC_MERGE
extern void i8259_init(struct device_node *node, unsigned long intack_addr);
extern unsigned int i8259_irq(struct pt_regs *regs);
#else
extern void i8259_init(unsigned long intack_addr, int offset);
extern int i8259_irq(struct pt_regs *regs);
#endif

#endif /* __KERNEL__ */
#endif /* _ASM_POWERPC_I8259_H */
