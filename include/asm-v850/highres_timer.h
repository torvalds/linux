/*
 * include/asm-v850/highres_timer.h -- High resolution timing routines
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

#ifndef __V850_HIGHRES_TIMER_H__
#define __V850_HIGHRES_TIMER_H__

#ifndef __ASSEMBLY__
#include <linux/time.h>
#endif

#include <asm/entry.h>


/* Frequency of the `slow ticks' (one tick each time the fast-tick
   counter overflows).  */
#define HIGHRES_TIMER_SLOW_TICK_RATE	25

/* Which timer in the V850E `Timer D' we use.  */
#define HIGHRES_TIMER_TIMER_D_UNIT	3


#ifndef __ASSEMBLY__

extern void highres_timer_start (void), highres_timer_stop (void);
extern void highres_timer_reset (void);
extern void highres_timer_read_ticks (u32 *slow_ticks, u32 *fast_ticks);
extern void highres_timer_ticks_to_timeval (u32 slow_ticks, u32 fast_ticks,
					    struct timeval *tv);
extern void highres_timer_read (struct timeval *tv);

#endif /* !__ASSEMBLY__ */


#endif /* __V850_HIGHRES_TIMER_H__ */
