/*
 * include/asm-arm/arch-ns9xxx/system.h
 *
 * Copyright (C) 2006 by Digi International Inc.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */
#ifndef __ASM_ARCH_SYSTEM_H
#define __ASM_ARCH_SYSTEM_H

#include <asm/proc-fns.h>
#include <asm/arch-ns9xxx/regs-sys.h>
#include <asm/mach-types.h>

static inline void arch_idle(void)
{
	cpu_do_idle();
}

static inline void arch_reset(char mode)
{
	u32 reg;

	reg = __raw_readl(SYS_PLL) >> 16;
	REGSET(reg, SYS_PLL, SWC, YES);
	__raw_writel(reg, SYS_PLL);

	BUG();
}

#endif /* ifndef __ASM_ARCH_SYSTEM_H */
