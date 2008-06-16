/*
 * rtc.h: Definitions for access to the Mostek real time clock
 *
 * Copyright (C) 1996 Thomas K. Dyas (tdyas@eden.rutgers.edu)
 */

#ifndef _RTC_H
#define _RTC_H

#include <linux/ioctl.h>

struct rtc_time
{
	int	sec;	/* Seconds (0-59) */
	int	min;	/* Minutes (0-59) */
	int	hour;	/* Hour (0-23) */
	int	dow;	/* Day of the week (1-7) */
	int	dom;	/* Day of the month (1-31) */
	int	month;	/* Month of year (1-12) */
	int	year;	/* Year (0-99) */
};

#define RTCGET _IOR('p', 20, struct rtc_time)
#define RTCSET _IOW('p', 21, struct rtc_time)

#endif
