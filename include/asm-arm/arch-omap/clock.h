/*
 *  linux/include/asm-arm/arch-omap/clock.h
 *
 *  Copyright (C) 2004 - 2005 Nokia corporation
 *  Written by Tuukka Tikkanen <tuukka.tikkanen@elektrobit.com>
 *  Based on clocks.h by Tony Lindgren, Gordon McNutt and RidgeRun, Inc
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ARCH_ARM_OMAP_CLOCK_H
#define __ARCH_ARM_OMAP_CLOCK_H

struct module;

struct clk {
	struct list_head	node;
	struct module		*owner;
	const char		*name;
	struct clk		*parent;
	unsigned long		rate;
	__u32			flags;
	void __iomem		*enable_reg;
	__u8			enable_bit;
	__u8			rate_offset;
	__u8			src_offset;
	__s8			usecount;
	void			(*recalc)(struct clk *);
	int			(*set_rate)(struct clk *, unsigned long);
	long			(*round_rate)(struct clk *, unsigned long);
	void			(*init)(struct clk *);
	int			(*enable)(struct clk *);
	void			(*disable)(struct clk *);
};

struct clk_functions {
	int		(*clk_enable)(struct clk *clk);
	void		(*clk_disable)(struct clk *clk);
	long		(*clk_round_rate)(struct clk *clk, unsigned long rate);
	int		(*clk_set_rate)(struct clk *clk, unsigned long rate);
	int		(*clk_set_parent)(struct clk *clk, struct clk *parent);
	struct clk *	(*clk_get_parent)(struct clk *clk);
	void		(*clk_allow_idle)(struct clk *clk);
	void		(*clk_deny_idle)(struct clk *clk);
};

extern unsigned int mpurate;
extern struct list_head clocks;
extern spinlock_t clockfw_lock;

extern int clk_init(struct clk_functions * custom_clocks);
extern int clk_register(struct clk *clk);
extern void clk_unregister(struct clk *clk);
extern void propagate_rate(struct clk *clk);
extern void followparent_recalc(struct clk * clk);
extern void clk_allow_idle(struct clk *clk);
extern void clk_deny_idle(struct clk *clk);

/* Clock flags */
#define RATE_CKCTL		(1 << 0)	/* Main fixed ratio clocks */
#define RATE_FIXED		(1 << 1)	/* Fixed clock rate */
#define RATE_PROPAGATES		(1 << 2)	/* Program children too */
#define VIRTUAL_CLOCK		(1 << 3)	/* Composite clock from table */
#define ALWAYS_ENABLED		(1 << 4)	/* Clock cannot be disabled */
#define ENABLE_REG_32BIT	(1 << 5)	/* Use 32-bit access */
#define VIRTUAL_IO_ADDRESS	(1 << 6)	/* Clock in virtual address */
#define CLOCK_IDLE_CONTROL	(1 << 7)
#define CLOCK_NO_IDLE_PARENT	(1 << 8)
#define DELAYED_APP		(1 << 9)	/* Delay application of clock */
#define CONFIG_PARTICIPANT	(1 << 10)	/* Fundamental clock */
#define CM_MPU_SEL1		(1 << 11)	/* Domain divider/source */
#define CM_DSP_SEL1		(1 << 12)
#define CM_GFX_SEL1		(1 << 13)
#define CM_MODEM_SEL1		(1 << 14)
#define CM_CORE_SEL1		(1 << 15)	/* Sets divider for many */
#define CM_CORE_SEL2		(1 << 16)	/* sets parent for GPT */
#define CM_WKUP_SEL1		(1 << 17)
#define CM_PLL_SEL1		(1 << 18)
#define CM_PLL_SEL2		(1 << 19)
#define CM_SYSCLKOUT_SEL1	(1 << 20)
#define CLOCK_IN_OMAP730	(1 << 21)
#define CLOCK_IN_OMAP1510	(1 << 22)
#define CLOCK_IN_OMAP16XX	(1 << 23)
#define CLOCK_IN_OMAP242X	(1 << 24)
#define CLOCK_IN_OMAP243X	(1 << 25)

#endif
