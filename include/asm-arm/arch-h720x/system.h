/*
 * linux/arch/arm/mach-h720x/system.h
 *
 * Copyright (C) 2001-2002 Jungjun Kim, Hynix Semiconductor Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 * linux/include/asm-arm/arch-h720x/system.h
 *
 */

#ifndef __ASM_ARCH_SYSTEM_H
#define __ASM_ARCH_SYSTEM_H
#include <asm/hardware.h>

static void arch_idle(void)
{
	CPU_REG (PMU_BASE, PMU_MODE) = PMU_MODE_IDLE;
	__asm__ __volatile__(
	"mov	r0, r0\n\t"
	"mov	r0, r0");
}


static __inline__ void arch_reset(char mode)
{
	CPU_REG (PMU_BASE, PMU_STAT) |= PMU_WARMRESET;
}

#endif
