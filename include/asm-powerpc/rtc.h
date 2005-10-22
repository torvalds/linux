/*
 * Real-time clock definitions and interfaces
 *
 * Author: Tom Rini <trini@mvista.com>
 *
 * 2002 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 *
 * Based on:
 * include/asm-m68k/rtc.h
 *
 * Copyright Richard Zidlicky
 * implementation details for genrtc/q40rtc driver
 *
 * And the old drivers/macintosh/rtc.c which was heavily based on:
 * Linux/SPARC Real Time Clock Driver
 * Copyright (C) 1996 Thomas K. Dyas (tdyas@eden.rutgers.edu)
 *
 * With additional work by Paul Mackerras and Franz Sirl.
 */

#ifndef __ASM_POWERPC_RTC_H__
#define __ASM_POWERPC_RTC_H__

#ifdef __KERNEL__

#include <linux/rtc.h>

#include <asm/machdep.h>
#include <asm/time.h>

#define RTC_PIE 0x40		/* periodic interrupt enable */
#define RTC_AIE 0x20		/* alarm interrupt enable */
#define RTC_UIE 0x10		/* update-finished interrupt enable */

/* some dummy definitions */
#define RTC_BATT_BAD 0x100	/* battery bad */
#define RTC_SQWE 0x08		/* enable square-wave output */
#define RTC_DM_BINARY 0x04	/* all time/date values are BCD if clear */
#define RTC_24H 0x02		/* 24 hour mode - else hours bit 7 means pm */
#define RTC_DST_EN 0x01	        /* auto switch DST - works f. USA only */

static inline unsigned int get_rtc_time(struct rtc_time *time)
{
	if (ppc_md.get_rtc_time)
		ppc_md.get_rtc_time(time);
	return RTC_24H;
}

/* Set the current date and time in the real time clock. */
static inline int set_rtc_time(struct rtc_time *time)
{
	if (ppc_md.set_rtc_time)
		return ppc_md.set_rtc_time(time);
	return -EINVAL;
}

static inline unsigned int get_rtc_ss(void)
{
	struct rtc_time h;

	get_rtc_time(&h);
	return h.tm_sec;
}

static inline int get_rtc_pll(struct rtc_pll_info *pll)
{
	return -EINVAL;
}
static inline int set_rtc_pll(struct rtc_pll_info *pll)
{
	return -EINVAL;
}

#endif /* __KERNEL__ */
#endif /* __ASM_POWERPC_RTC_H__ */
