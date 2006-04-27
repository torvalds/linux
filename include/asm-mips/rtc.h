/*
 * include/asm-mips/rtc.h
 *
 * (Really an interface for drivers/char/genrtc.c)
 *
 * Copyright (C) 2004 MontaVista Software Inc.
 * Author: Jun Sun, jsun@mvista.com or jsun@junsun.net
 *
 * Please read the COPYING file for all license details.
 */

#ifndef _MIPS_RTC_H
#define _MIPS_RTC_H

#ifdef __KERNEL__

#include <linux/rtc.h>
#include <asm/time.h>

#define RTC_PIE 0x40            /* periodic interrupt enable */
#define RTC_AIE 0x20            /* alarm interrupt enable */
#define RTC_UIE 0x10            /* update-finished interrupt enable */

/* some dummy definitions */
#define RTC_BATT_BAD 0x100      /* battery bad */
#define RTC_SQWE 0x08           /* enable square-wave output */
#define RTC_DM_BINARY 0x04      /* all time/date values are BCD if clear */
#define RTC_24H 0x02            /* 24 hour mode - else hours bit 7 means pm */
#define RTC_DST_EN 0x01         /* auto switch DST - works f. USA only */

static inline unsigned int get_rtc_time(struct rtc_time *time)
{
	unsigned long nowtime;

	nowtime = rtc_mips_get_time();
	to_tm(nowtime, time);
	time->tm_year -= 1900;

	return RTC_24H;
}

static inline int set_rtc_time(struct rtc_time *time)
{
	unsigned long nowtime;
	int ret;

	nowtime = mktime(time->tm_year+1900, time->tm_mon+1,
			time->tm_mday, time->tm_hour, time->tm_min,
			time->tm_sec);
	ret = rtc_mips_set_time(nowtime);

	return ret;
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
#endif
#endif
