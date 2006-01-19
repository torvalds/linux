/*
 * Copied from linux/include/asm-arm/arch-sa1100/system.h
 * Copyright (c) 1999 Nicolas Pitre <nico@cam.org>
 */
#ifndef __ASM_ARCH_SYSTEM_H
#define __ASM_ARCH_SYSTEM_H
#include <linux/config.h>
#include <linux/clk.h>

#include <asm/mach-types.h>
#include <asm/hardware.h>
#include <asm/arch/prcm.h>

#ifndef CONFIG_MACH_VOICEBLUE
#define voiceblue_reset()		do {} while (0)
#endif

static inline void arch_idle(void)
{
	cpu_do_idle();
}

static inline void omap1_arch_reset(char mode)
{
	/*
	 * Workaround for 5912/1611b bug mentioned in sprz209d.pdf p. 28
	 * "Global Software Reset Affects Traffic Controller Frequency".
	 */
	if (cpu_is_omap5912()) {
		omap_writew(omap_readw(DPLL_CTL) & ~(1 << 4),
				 DPLL_CTL);
		omap_writew(0x8, ARM_RSTCT1);
	}

	if (machine_is_voiceblue())
		voiceblue_reset();
	else
		omap_writew(1, ARM_RSTCT1);
}

static inline void omap2_arch_reset(char mode)
{
	u32 rate;
	struct clk *vclk, *sclk;

	vclk = clk_get(NULL, "virt_prcm_set");
	sclk = clk_get(NULL, "sys_ck");
	rate = clk_get_rate(sclk);
	clk_set_rate(vclk, rate);	/* go to bypass for OMAP limitation */
	RM_RSTCTRL_WKUP |= 2;
}

static inline void arch_reset(char mode)
{
	if (!cpu_is_omap24xx())
		omap1_arch_reset(mode);
	else
		omap2_arch_reset(mode);
}

#endif
