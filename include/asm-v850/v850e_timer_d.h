/*
 * include/asm-v850/v850e_timer_d.h -- `Timer D' component often used
 *	with the V850E cpu core
 *
 *  Copyright (C) 2001,02,03  NEC Electronics Corporation
 *  Copyright (C) 2001,02,03  Miles Bader <miles@gnu.org>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 *
 * Written by Miles Bader <miles@gnu.org>
 */

#ifndef __V850_V850E_TIMER_D_H__
#define __V850_V850E_TIMER_D_H__

#include <asm/types.h>
#include <asm/machdep.h>	/* Pick up chip-specific defs.  */


/* Timer D (16-bit interval timers).  */

/* Count registers for timer D.  */
#define V850E_TIMER_D_TMD_ADDR(n) (V850E_TIMER_D_TMD_BASE_ADDR + 0x10 * (n))
#define V850E_TIMER_D_TMD(n)	  (*(volatile u16 *)V850E_TIMER_D_TMD_ADDR(n))

/* Count compare registers for timer D.  */
#define V850E_TIMER_D_CMD_ADDR(n) (V850E_TIMER_D_CMD_BASE_ADDR + 0x10 * (n))
#define V850E_TIMER_D_CMD(n)	  (*(volatile u16 *)V850E_TIMER_D_CMD_ADDR(n))

/* Control registers for timer D.  */
#define V850E_TIMER_D_TMCD_ADDR(n) (V850E_TIMER_D_TMCD_BASE_ADDR + 0x10 * (n))
#define V850E_TIMER_D_TMCD(n)	   (*(volatile u8 *)V850E_TIMER_D_TMCD_ADDR(n))
/* Control bits for timer D.  */
#define V850E_TIMER_D_TMCD_CE  	   0x2 /* count enable */
#define V850E_TIMER_D_TMCD_CAE	   0x1 /* clock action enable */
/* Clock divider setting (log2).  */
#define V850E_TIMER_D_TMCD_CS(divlog2) (((divlog2) - V850E_TIMER_D_TMCD_CS_MIN) << 4)
/* Minimum clock divider setting (log2).  */
#ifndef V850E_TIMER_D_TMCD_CS_MIN /* Can be overridden by mach-specific hdrs */
#define V850E_TIMER_D_TMCD_CS_MIN  2 /* Default is correct for the v850e/ma1 */
#endif
/* Maximum clock divider setting (log2).  */
#define V850E_TIMER_D_TMCD_CS_MAX  (V850E_TIMER_D_TMCD_CS_MIN + 7)

/* Return the clock-divider (log2) of timer D unit N.  */
#define V850E_TIMER_D_DIVLOG2(n) \
  (((V850E_TIMER_D_TMCD(n) >> 4) & 0x7) + V850E_TIMER_D_TMCD_CS_MIN)


#ifndef __ASSEMBLY__

/* Start interval timer TIMER (0-3).  The timer will issue the
   corresponding INTCMD interrupt RATE times per second.  This function
   does not enable the interrupt.  */
extern void v850e_timer_d_configure (unsigned timer, unsigned rate);

#endif /* !__ASSEMBLY__ */


#endif /* __V850_V850E_TIMER_D_H__  */
