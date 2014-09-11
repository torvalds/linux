/*
 *  include/linux/rtc/rt5036-rtc.h
 *  Include header file for Richtek RT5036 RTC Driver
 *
 *  Copyright (C) 2014 Richtek Technology Corp.
 *  cy_huang <cy_huang@richtek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 */

#ifndef _LINUX_RTC_RT5036_RTC_H
#define _LINUX_RTC_RT5036_RTC_H

enum {
	RTCEVENT_CAIRQ,
	RTCEVENT_CDIRQ,
	RTCEVENT_MAX,
};

#define RT5036_RTC_SECMASK	0x3F
#define RT5036_RTC_MINMASK	0x3F
#define RT5036_RTC_HOURMASK	0x1F
#define RT5036_RTC_YEARMASK	0x7F
#define RT5036_RTC_MONMASK	0x0F
#define RT5036_RTC_DAYMASK	0x1F
#define RT5036_RTC_WEEKMASK	0xE0
#define RT5036_RTC_WEEKSHIFT	5

#define RT5036_STBCTL_MASK	0x01
#define RT5036_RTCCDEN_MASK	0x02
#define RT5036_RTCAEN_MASK	0x04

#define RT5036_RTCCDIRQ_MASK	0x02
#define RT5036_RTCAIRQ_MASK	0x01

#endif /* #ifndef _LINUX_RTC_RT5036_RTC_H */
