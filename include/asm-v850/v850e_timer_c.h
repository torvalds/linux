/*
 * include/asm-v850/v850e_timer_c.h -- `Timer C' component often used
 *	with the V850E cpu core
 *
 *  Copyright (C) 2001,03  NEC Electronics Corporation
 *  Copyright (C) 2001,03  Miles Bader <miles@gnu.org>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 *
 * Written by Miles Bader <miles@gnu.org>
 */

/* NOTE: this include file currently contains only enough to allow us to
   use timer C as an interrupt pass-through.  */

#ifndef __V850_V850E_TIMER_C_H__
#define __V850_V850E_TIMER_C_H__

#include <asm/types.h>
#include <asm/machdep.h>	/* Pick up chip-specific defs.  */


/* Timer C (16-bit interval timers).  */

/* Control register 0 for timer C.  */
#define V850E_TIMER_C_TMCC0_ADDR(n) (V850E_TIMER_C_BASE_ADDR + 0x6 + 0x10 *(n))
#define V850E_TIMER_C_TMCC0(n)	  (*(volatile u8 *)V850E_TIMER_C_TMCC0_ADDR(n))
#define V850E_TIMER_C_TMCC0_CAE	  0x01 /* clock action enable */
#define V850E_TIMER_C_TMCC0_CE	  0x02 /* count enable */
/* ... */

/* Control register 1 for timer C.  */
#define V850E_TIMER_C_TMCC1_ADDR(n) (V850E_TIMER_C_BASE_ADDR + 0x8 + 0x10 *(n))
#define V850E_TIMER_C_TMCC1(n)	  (*(volatile u8 *)V850E_TIMER_C_TMCC1_ADDR(n))
#define V850E_TIMER_C_TMCC1_CMS0  0x01 /* capture/compare mode select (ccc0) */
#define V850E_TIMER_C_TMCC1_CMS1  0x02 /* capture/compare mode select (ccc1) */
/* ... */

/* Interrupt edge-sensitivity control for timer C.  */
#define V850E_TIMER_C_SESC_ADDR(n) (V850E_TIMER_C_BASE_ADDR + 0x9 + 0x10 *(n))
#define V850E_TIMER_C_SESC(n)	  (*(volatile u8 *)V850E_TIMER_C_SESC_ADDR(n))

/* ...etc... */


#endif /* __V850_V850E_TIMER_C_H__  */
