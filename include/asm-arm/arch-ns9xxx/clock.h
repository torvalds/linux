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

static inline u32 ns9xxx_systemclock(void) __attribute__((const));
static inline u32 ns9xxx_systemclock(void)
{
	/*
	 * This should be a multiple of HZ * TIMERCLOCKSELECT (in time.c)
	 */
	return 353894400;
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
