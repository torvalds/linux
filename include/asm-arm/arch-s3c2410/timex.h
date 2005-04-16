/* linux/include/asm-arm/arch-s3c2410/timex.h
 *
 * (c) 2003-2005 Simtec Electronics
 *  Ben Dooks <ben@simtec.co.uk>
 *
 * S3C2410 - time parameters
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Changelog:
 *  02-Sep-2003 BJD  Created file
 *  05-Jan-2004 BJD  Updated for Linux 2.6.0
 *  22-Nov-2004 BJD  Fixed CLOCK_TICK_RATE
 *  10-Jan-2004 BJD  Removed s3c2410_clock_tick_rate
*/

#ifndef __ASM_ARCH_TIMEX_H
#define __ASM_ARCH_TIMEX_H

/* CLOCK_TICK_RATE needs to be evaluatable by the cpp, so making it
 * a variable is useless. It seems as long as we make our timers an
 * exact multiple of HZ, any value that makes a 1->1 correspondence
 * for the time conversion functions to/from jiffies is acceptable.
*/


#define CLOCK_TICK_RATE 12000000


#endif /* __ASM_ARCH_TIMEX_H */
