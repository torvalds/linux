/* 
 * include/asm-parisc/rtc.h
 *
 * Copyright 2002 Randolph CHung <tausq@debian.org>
 *
 * Based on: include/asm-ppc/rtc.h and the genrtc driver in the
 * 2.4 parisc linux tree
 */

#ifndef __ASM_RTC_H__
#define __ASM_RTC_H__

#ifdef __KERNEL__

#include <linux/rtc.h>

#include <asm/pdc.h>

#define SECS_PER_HOUR   (60 * 60)
#define SECS_PER_DAY    (SECS_PER_HOUR * 24)


#define RTC_PIE 0x40		/* periodic interrupt enable */
#define RTC_AIE 0x20		/* alarm interrupt enable */
#define RTC_UIE 0x10		/* update-finished interrupt enable */

#define RTC_BATT_BAD 0x100	/* battery bad */

/* some dummy definitions */
#define RTC_SQWE 0x08		/* enable square-wave output */
#define RTC_DM_BINARY 0x04	/* all time/date values are BCD if clear */
#define RTC_24H 0x02		/* 24 hour mode - else hours bit 7 means pm */
#define RTC_DST_EN 0x01	        /* auto switch DST - works f. USA only */

# define __isleap(year) \
  ((year) % 4 == 0 && ((year) % 100 != 0 || (year) % 400 == 0))

/* How many days come before each month (0-12).  */
static const unsigned short int __mon_yday[2][13] =
{
	/* Normal years.  */
	{ 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334, 365 },
	/* Leap years.  */
	{ 0, 31, 60, 91, 121, 152, 182, 213, 244, 274, 305, 335, 366 }
};

static inline unsigned int get_rtc_time(struct rtc_time *wtime)
{
	struct pdc_tod tod_data;
	long int days, rem, y;
	const unsigned short int *ip;

	if(pdc_tod_read(&tod_data) < 0)
		return RTC_24H | RTC_BATT_BAD;


	// most of the remainder of this function is:
//	Copyright (C) 1991, 1993, 1997, 1998 Free Software Foundation, Inc.
//	This was originally a part of the GNU C Library.
//      It is distributed under the GPL, and was swiped from offtime.c


	days = tod_data.tod_sec / SECS_PER_DAY;
	rem = tod_data.tod_sec % SECS_PER_DAY;

	wtime->tm_hour = rem / SECS_PER_HOUR;
	rem %= SECS_PER_HOUR;
	wtime->tm_min = rem / 60;
	wtime->tm_sec = rem % 60;

	y = 1970;

#define DIV(a, b) ((a) / (b) - ((a) % (b) < 0))
#define LEAPS_THRU_END_OF(y) (DIV (y, 4) - DIV (y, 100) + DIV (y, 400))

	while (days < 0 || days >= (__isleap (y) ? 366 : 365))
	{
		/* Guess a corrected year, assuming 365 days per year.  */
		long int yg = y + days / 365 - (days % 365 < 0);

		/* Adjust DAYS and Y to match the guessed year.  */
		days -= ((yg - y) * 365
			 + LEAPS_THRU_END_OF (yg - 1)
			 - LEAPS_THRU_END_OF (y - 1));
		y = yg;
	}
	wtime->tm_year = y - 1900;

	ip = __mon_yday[__isleap(y)];
	for (y = 11; days < (long int) ip[y]; --y)
		continue;
	days -= ip[y];
	wtime->tm_mon = y;
	wtime->tm_mday = days + 1;

	return RTC_24H;
}

static int set_rtc_time(struct rtc_time *wtime)
{
	u_int32_t secs;

	secs = mktime(wtime->tm_year + 1900, wtime->tm_mon + 1, wtime->tm_mday, 
		      wtime->tm_hour, wtime->tm_min, wtime->tm_sec);

	if(pdc_tod_set(secs, 0) < 0)
		return -1;
	else
		return 0;

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
#endif /* __ASM_RTC_H__ */
