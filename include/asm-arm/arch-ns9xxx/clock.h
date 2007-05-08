/*
 * include/asm-arm/arch-ns9xxx/clock.h
 *
 * Copyright (C) 2007 by Digi International Inc.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */
#ifndef __ASM_ARCH_CLOCK_H
#define __ASM_ARCH_CLOCK_H

#include <asm/arch-ns9xxx/regs-sys.h>

#define CRYSTAL 29491200 /* Hz */

/* The HRM calls this value f_vco */
static inline u32 ns9xxx_systemclock(void) __attribute__((const));
static inline u32 ns9xxx_systemclock(void)
{
	u32 pll = SYS_PLL;

	/*
	 * The system clock should be a multiple of HZ * TIMERCLOCKSELECT (in
	 * time.c).
	 *
	 * The following values are given:
	 *   - TIMERCLOCKSELECT == 2^i for an i in {0 .. 6}
	 *   - CRYSTAL == 29491200 == 2^17 * 3^2 * 5^2
	 *   - ND in {0 .. 31}
	 *   - FS in {0 .. 3}
	 *
	 * Assuming the worst, we consider:
	 *   - TIMERCLOCKSELECT == 64
	 *   - ND == 0
	 *   - FS == 3
	 *
	 * So HZ should be a divisor of:
	 *      (CRYSTAL * (ND + 1) >> FS) / TIMERCLOCKSELECT
	 *   == (2^17 * 3^2 * 5^2 * 1 >> 3) / 64
	 *   == 2^8 * 3^2 * 5^2
	 *   == 57600
	 *
	 * Currently HZ is defined to be 100 for this platform.
	 *
	 * Fine.
	 */
	return CRYSTAL * (REGGET(pll, SYS_PLL, ND) + 1)
		>> REGGET(pll, SYS_PLL, FS);
}

static inline u32 ns9xxx_cpuclock(void) __attribute__((const));
static inline u32 ns9xxx_cpuclock(void)
{
	return ns9xxx_systemclock() / 2;
}

static inline u32 ns9xxx_ahbclock(void) __attribute__((const));
static inline u32 ns9xxx_ahbclock(void)
{
	return ns9xxx_systemclock() / 4;
}

static inline u32 ns9xxx_bbusclock(void) __attribute__((const));
static inline u32 ns9xxx_bbusclock(void)
{
	return ns9xxx_systemclock() / 8;
}

#endif /* ifndef __ASM_ARCH_CLOCK_H */
