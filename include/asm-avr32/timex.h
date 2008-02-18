/*
 * Copyright (C) 2004-2006 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __ASM_AVR32_TIMEX_H
#define __ASM_AVR32_TIMEX_H

/*
 * This is the frequency of the timer used for Linux's timer interrupt.
 * The value should be defined as accurate as possible or under certain
 * circumstances Linux timekeeping might become inaccurate or fail.
 *
 * For many system the exact clockrate of the timer isn't known but due to
 * the way this value is used we can get away with a wrong value as long
 * as this value is:
 *
 *  - a multiple of HZ
 *  - a divisor of the actual rate
 *
 * 500000 is a good such cheat value.
 *
 * The obscure number 1193182 is the same as used by the original i8254
 * time in legacy PC hardware; the chip is never found in AVR32 systems.
 */
#define CLOCK_TICK_RATE		500000	/* Underlying HZ */

typedef unsigned long cycles_t;

static inline cycles_t get_cycles (void)
{
	return 0;
}

#define ARCH_HAS_READ_CURRENT_TIMER

#endif /* __ASM_AVR32_TIMEX_H */
