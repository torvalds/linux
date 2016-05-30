/* 
 * include/asm-generic/rtc.h
 *
 * Author: Tom Rini <trini@mvista.com>
 *
 * Based on:
 * drivers/char/rtc.c
 *
 * Please read the COPYING file for all license details.
 */

#ifndef __ASM_RTC_H__
#define __ASM_RTC_H__

#include <linux/rtc.h>

#ifndef get_rtc_time
#include <linux/mc146818rtc.h>
#define get_rtc_time mc146818_get_time
#define set_rtc_time mc146818_set_time
#endif

#define RTC_PIE 0x40		/* periodic interrupt enable */
#define RTC_AIE 0x20		/* alarm interrupt enable */
#define RTC_UIE 0x10		/* update-finished interrupt enable */

/* some dummy definitions */
#define RTC_BATT_BAD 0x100	/* battery bad */
#define RTC_SQWE 0x08		/* enable square-wave output */
#define RTC_DM_BINARY 0x04	/* all time/date values are BCD if clear */
#define RTC_24H 0x02		/* 24 hour mode - else hours bit 7 means pm */
#define RTC_DST_EN 0x01	        /* auto switch DST - works f. USA only */

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

#endif /* __ASM_RTC_H__ */
