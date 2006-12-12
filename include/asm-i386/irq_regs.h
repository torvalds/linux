/*
 * Per-cpu current frame pointer - the location of the last exception frame on
 * the stack, stored in the PDA.
 *
 * Jeremy Fitzhardinge <jeremy@goop.org>
 */
#ifndef _ASM_I386_IRQ_REGS_H
#define _ASM_I386_IRQ_REGS_H

#include <asm/pda.h>

static inline struct pt_regs *get_irq_regs(void)
{
	return read_pda(irq_regs);
}

static inline struct pt_regs *set_irq_regs(struct pt_regs *new_regs)
{
	struct pt_regs *old_regs;

	old_regs = read_pda(irq_regs);
	write_pda(irq_regs, new_regs);

	return old_regs;
}

#endif /* _ASM_I386_IRQ_REGS_H */
