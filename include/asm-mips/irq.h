/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1994 by Waldorf GMBH, written by Ralf Baechle
 * Copyright (C) 1995, 96, 97, 98, 99, 2000, 01, 02, 03 by Ralf Baechle
 */
#ifndef _ASM_IRQ_H
#define _ASM_IRQ_H

#include <linux/linkage.h>

#include <asm/mipsmtregs.h>

#include <irq.h>

#ifdef CONFIG_I8259
static inline int irq_canonicalize(int irq)
{
	return ((irq == 2) ? 9 : irq);
}
#else
#define irq_canonicalize(irq) (irq)	/* Sane hardware, sane code ... */
#endif

extern asmlinkage unsigned int do_IRQ(unsigned int irq);

#ifdef CONFIG_MIPS_MT_SMTC
/*
 * Clear interrupt mask handling "backstop" if irq_hwmask
 * entry so indicates. This implies that the ack() or end()
 * functions will take over re-enabling the low-level mask.
 * Otherwise it will be done on return from exception.
 */
#define __DO_IRQ_SMTC_HOOK()						\
do {									\
	if (irq_hwmask[irq] & 0x0000ff00)				\
		write_c0_tccontext(read_c0_tccontext() &		\
		                   ~(irq_hwmask[irq] & 0x0000ff00));	\
} while (0)
#else
#define __DO_IRQ_SMTC_HOOK() do { } while (0)
#endif

#ifdef CONFIG_PREEMPT

/*
 * do_IRQ handles all normal device IRQ's (the special
 * SMP cross-CPU interrupts have their own specific
 * handlers).
 *
 * Ideally there should be away to get this into kernel/irq/handle.c to
 * avoid the overhead of a call for just a tiny function ...
 */
#define do_IRQ(irq)							\
do {									\
	irq_enter();							\
	__DO_IRQ_SMTC_HOOK();						\
	__do_IRQ((irq));						\
	irq_exit();							\
} while (0)

#endif

extern void arch_init_irq(void);
extern void spurious_interrupt(void);

#ifdef CONFIG_MIPS_MT_SMTC
struct irqaction;

extern unsigned long irq_hwmask[];
extern int setup_irq_smtc(unsigned int irq, struct irqaction * new,
                          unsigned long hwmask);
#endif /* CONFIG_MIPS_MT_SMTC */

extern int allocate_irqno(void);
extern void alloc_legacy_irqno(void);
extern void free_irqno(unsigned int irq);

#endif /* _ASM_IRQ_H */
