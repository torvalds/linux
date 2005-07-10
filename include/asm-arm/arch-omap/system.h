/*
 * Copied from linux/include/asm-arm/arch-sa1100/system.h
 * Copyright (c) 1999 Nicolas Pitre <nico@cam.org>
 */
#ifndef __ASM_ARCH_SYSTEM_H
#define __ASM_ARCH_SYSTEM_H
#include <linux/config.h>
#include <asm/mach-types.h>
#include <asm/arch/hardware.h>
#include <asm/mach-types.h>

static inline void arch_idle(void)
{
	cpu_do_idle();
}

static inline void arch_reset(char mode)
{

#ifdef CONFIG_ARCH_OMAP16XX
	/*
	 * Workaround for 5912/1611b bug mentioned in sprz209d.pdf p. 28
	 * "Global Software Reset Affects Traffic Controller Frequency".
	 */
	if (cpu_is_omap5912()) {
		omap_writew(omap_readw(DPLL_CTL) & ~(1 << 4),
				 DPLL_CTL);
		omap_writew(0x8, ARM_RSTCT1);
	}
#endif
#ifdef CONFIG_MACH_VOICEBLUE
	if (machine_is_voiceblue())
		voiceblue_reset();
	else
#endif
		omap_writew(1, ARM_RSTCT1);
}

#endif
