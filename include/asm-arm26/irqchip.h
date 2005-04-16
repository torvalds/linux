/*
 *  linux/include/asm-arm/mach/irq.h
 *
 *  Copyright (C) 1995-2000 Russell King.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __ASM_ARM_MACH_IRQ_H
#define __ASM_ARM_MACH_IRQ_H

struct irqdesc;
struct pt_regs;
struct seq_file;

typedef void (*irq_handler_t)(unsigned int, struct irqdesc *, struct pt_regs *);
typedef void (*irq_control_t)(unsigned int);

struct irqchip {
	/*
	 * Acknowledge the IRQ.
	 * If this is a level-based IRQ, then it is expected to mask the IRQ
	 * as well.
	 */
	void (*ack)(unsigned int);
	/*
	 * Mask the IRQ in hardware.
	 */
	void (*mask)(unsigned int);
	/*
	 * Unmask the IRQ in hardware.
	 */
	void (*unmask)(unsigned int);
	/*
	 * Re-run the IRQ
	 */
	void (*rerun)(unsigned int);
	/*
	 * Set the type of the IRQ.
	 */
	int (*type)(unsigned int, unsigned int);
};

struct irqdesc {
	irq_handler_t	handle;
	struct irqchip	*chip;
	struct irqaction *action;

	unsigned int	enabled  : 1;		/* IRQ is currently enabled   */
	unsigned int	triggered: 1;		/* IRQ has occurred	      */
	unsigned int	running  : 1;		/* IRQ is running             */
	unsigned int	pending  : 1;		/* IRQ is pending	      */
	unsigned int	probing  : 1;		/* IRQ in use for a probe     */
	unsigned int	probe_ok : 1;		/* IRQ can be used for probe  */
	unsigned int	valid    : 1;		/* IRQ claimable	      */
	unsigned int	noautoenable : 1;	/* don't automatically enable IRQ */
	unsigned int	unused   :23;
	unsigned int	depth;			/* disable depth	      */

	/*
	 * IRQ lock detection
	 */
	unsigned int	lck_cnt;
	unsigned int	lck_pc;
	unsigned int	lck_jif;
};

extern struct irqdesc irq_desc[];

/*
 * This is internal.  Do not use it.
 */
extern void (*init_arch_irq)(void);
extern void init_FIQ(void);
extern int show_fiq_list(struct seq_file *, void *);
void __set_irq_handler(unsigned int irq, irq_handler_t, int);

/*
 * External stuff.
 */
#define set_irq_handler(irq,handler)		__set_irq_handler(irq,handler,0)
#define set_irq_chained_handler(irq,handler)	__set_irq_handler(irq,handler,1)

void set_irq_chip(unsigned int irq, struct irqchip *);
void set_irq_flags(unsigned int irq, unsigned int flags);

#define IRQF_VALID	(1 << 0)
#define IRQF_PROBE	(1 << 1)
#define IRQF_NOAUTOEN	(1 << 2)

/*
 * Built-in IRQ handlers.
 */
void do_level_IRQ(unsigned int irq, struct irqdesc *desc, struct pt_regs *regs);
void do_edge_IRQ(unsigned int irq, struct irqdesc *desc, struct pt_regs *regs);
void do_simple_IRQ(unsigned int irq, struct irqdesc *desc, struct pt_regs *regs);
void do_bad_IRQ(unsigned int irq, struct irqdesc *desc, struct pt_regs *regs);
void dummy_mask_unmask_irq(unsigned int irq);

#endif
