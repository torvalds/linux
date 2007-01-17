/*
 * linux/include/asm-arm/arch-iop13xx/system.h
 *
 *  Copyright (C) 2004 Intel Corp.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <asm/arch/iop13xx.h>
static inline void arch_idle(void)
{
	cpu_do_idle();
}

/* WDTCR CP6 R7 Page 9 */
static inline u32 read_wdtcr(void)
{
	u32 val;
	asm volatile("mrc p6, 0, %0, c7, c9, 0":"=r" (val));
	return val;
}
static inline void write_wdtcr(u32 val)
{
	asm volatile("mcr p6, 0, %0, c7, c9, 0"::"r" (val));
}

/* WDTSR CP6 R8 Page 9 */
static inline u32 read_wdtsr(void)
{
	u32 val;
	asm volatile("mrc p6, 0, %0, c8, c9, 0":"=r" (val));
	return val;
}
static inline void write_wdtsr(u32 val)
{
	asm volatile("mcr p6, 0, %0, c8, c9, 0"::"r" (val));
}

#define IOP13XX_WDTCR_EN_ARM	0x1e1e1e1e
#define IOP13XX_WDTCR_EN	0xe1e1e1e1
#define IOP13XX_WDTCR_DIS_ARM	0x1f1f1f1f
#define IOP13XX_WDTCR_DIS	0xf1f1f1f1
#define IOP13XX_WDTSR_WRITE_EN	(1 << 31)
#define IOP13XX_WDTCR_IB_RESET	(1 << 0)
static inline void arch_reset(char mode)
{
	/*
	 * Reset the internal bus (warning both cores are reset)
	 */
	u32 cp_flags = iop13xx_cp6_save();
	write_wdtcr(IOP13XX_WDTCR_EN_ARM);
	write_wdtcr(IOP13XX_WDTCR_EN);
	write_wdtsr(IOP13XX_WDTSR_WRITE_EN | IOP13XX_WDTCR_IB_RESET);
	write_wdtcr(0x1000);
	iop13xx_cp6_restore(cp_flags);

	for(;;);
}
