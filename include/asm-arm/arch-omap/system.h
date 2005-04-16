/*
 * Copied from linux/include/asm-arm/arch-sa1100/system.h
 * Copyright (c) 1999 Nicolas Pitre <nico@cam.org>
 */
#ifndef __ASM_ARCH_SYSTEM_H
#define __ASM_ARCH_SYSTEM_H
#include <linux/config.h>
#include <asm/arch/hardware.h>

static inline void arch_idle(void)
{
	cpu_do_idle();
}

static inline void arch_reset(char mode)
{
	omap_writew(1, ARM_RSTCT1);
}

#endif
