/*
 * include/linux/rtc/rtc-ricoh619.h
 *
 * Real time clock driver for RICOH R5T619 power management chip.
 *
 * Copyright (C) 2012-2013 RICOH COMPANY,LTD
 *
 * Based on code
 *  Copyright (C) 2011 NVIDIA Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * you should have received a copy of the gnu general public license
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */
#ifndef __LINUX_RTC_RICOH619_H_
#define __LINUX_RTC_RICOH619_H_

#include <linux/rtc.h>

#define rtc_ctrl1		0xAE
#define rtc_ctrl2		0xAF
#define rtc_seconds_reg		0xA0
#define rtc_alarm_y_sec		0xA8
#define rtc_adjust		0xA7
#define rtc_month_reg		0xA5
#define rtc_mday_reg		0xA4
#define rtc_dal_month_reg	0xAC


/*
linux rtc driver refers 1900 as base year in many calculations.
(e.g. refer drivers/rtc/rtc-lib.c)
*/
#define os_ref_year 1900

/*
	pmu rtc have only 2 nibbles to store year information, so using an
	offset of 100 to set the base year as 2000 for our driver.
*/



struct ricoh619_rtc_platform_data {
	int irq;
	struct rtc_time time;
};


#endif
