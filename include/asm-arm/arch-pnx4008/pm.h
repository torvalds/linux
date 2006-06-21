/*
 * include/asm-arm/arch-pnx4008/pm.h
 *
 * PNX4008 Power Management Routiness - header file
 *
 * Authors: Vitaly Wool, Dmitry Chigirev <source@mvista.com>
 *
 * 2005 (c) MontaVista Software, Inc. This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#ifndef __ASM_ARCH_PNX4008_PM_H
#define __ASM_ARCH_PNX4008_PM_H

#ifndef __ASSEMBLER__
#include "irq.h"
#include "irqs.h"
#include "clock.h"

extern void pnx4008_pm_idle(void);
extern void pnx4008_pm_suspend(void);
extern unsigned int pnx4008_cpu_suspend_sz;
extern void pnx4008_cpu_suspend(void);
extern unsigned int pnx4008_cpu_standby_sz;
extern void pnx4008_cpu_standby(void);

extern int pnx4008_startup_pll(struct clk *);
extern int pnx4008_shutdown_pll(struct clk *);

static inline void start_int_umask(u8 irq)
{
	__raw_writel(__raw_readl(START_INT_ER_REG(irq)) |
		     START_INT_REG_BIT(irq), START_INT_ER_REG(irq));
}

static inline void start_int_mask(u8 irq)
{
	__raw_writel(__raw_readl(START_INT_ER_REG(irq)) &
		     ~START_INT_REG_BIT(irq), START_INT_ER_REG(irq));
}

static inline void start_int_ack(u8 irq)
{
	__raw_writel(START_INT_REG_BIT(irq), START_INT_RSR_REG(irq));
}

static inline void start_int_set_falling_edge(u8 irq)
{
	__raw_writel(__raw_readl(START_INT_APR_REG(irq)) &
		     ~START_INT_REG_BIT(irq), START_INT_APR_REG(irq));
}

static inline void start_int_set_rising_edge(u8 irq)
{
	__raw_writel(__raw_readl(START_INT_APR_REG(irq)) |
		     START_INT_REG_BIT(irq), START_INT_APR_REG(irq));
}

#endif				/* ASSEMBLER */
#endif				/* __ASM_ARCH_PNX4008_PM_H */
