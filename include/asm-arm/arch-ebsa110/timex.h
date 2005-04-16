/*
 *  linux/include/asm-arm/arch-ebsa110/timex.h
 *
 *  Copyright (C) 1997, 1998 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  EBSA110 architecture timex specifications
 */

/*
 * On the EBSA, the clock ticks at weird rates.
 * This is therefore not used to calculate the
 * divisor.
 */
#define CLOCK_TICK_RATE		47894000

