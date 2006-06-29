/*
 *  linux/include/asm-mips/tx3927/irq.h
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001 Toshiba Corporation
 */
#ifndef __ASM_TX3927_IRQ_H
#define __ASM_TX3927_IRQ_H

#ifndef __ASSEMBLY__

#include <asm/irq.h>

struct tb_irq_space {
	struct tb_irq_space* next;
	int start_irqno;
	int nr_irqs;
	void (*mask_func)(int irq_nr, int space_id);
	void (*unmask_func)(int irq_no, int space_id);
	const char *name;
	int space_id;
	int can_share;
};
extern struct tb_irq_space* tb_irq_spaces;

static __inline__ void add_tb_irq_space(struct tb_irq_space* sp)
{
	sp->next = tb_irq_spaces;
	tb_irq_spaces = sp;
}


struct pt_regs;
extern void
toshibaboards_spurious(struct pt_regs *regs, int irq);
extern void
toshibaboards_irqdispatch(struct pt_regs *regs, int irq);

extern struct irqaction *
toshibaboards_get_irq_action(int irq);
extern int
toshibaboards_setup_irq(int irq, struct irqaction * new);


#ifdef CONFIG_TX_BRANCH_LIKELY_BUG_WORKAROUND
extern void tx_branch_likely_bug_fixup(struct pt_regs *regs);
#endif

extern int (*toshibaboards_gen_iack)(void);

#endif /* !__ASSEMBLY__ */

#define NR_ISA_IRQS 16
#define TB_IRQ_IS_ISA(irq)	\
	(0 <= (irq) && (irq) < NR_ISA_IRQS)
#define TB_IRQ_TO_ISA_IRQ(irq)	(irq)

#endif /* __ASM_TX3927_IRQ_H */
