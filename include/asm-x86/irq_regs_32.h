/*
 * Per-cpu current frame pointer - the location of the last exception frame on
 * the stack, stored in the per-cpu area.
 *
 * Jeremy Fitzhardinge <jeremy@goop.org>
 */
#ifndef ASM_X86__IRQ_REGS_32_H
#define ASM_X86__IRQ_REGS_32_H

#include <asm/percpu.h>

DECLARE_PER_CPU(struct pt_regs *, irq_regs);

static inline struct pt_regs *get_irq_regs(void)
{
	return x86_read_percpu(irq_regs);
}

static inline struct pt_regs *set_irq_regs(struct pt_regs *new_regs)
{
	struct pt_regs *old_regs;

	old_regs = get_irq_regs();
	x86_write_percpu(irq_regs, new_regs);

	return old_regs;
}

#endif /* ASM_X86__IRQ_REGS_32_H */
