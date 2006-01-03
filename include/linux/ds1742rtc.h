/*
 * ds1742rtc.h - register definitions for the Real-Time-Clock / CMOS RAM
 *
 * Copyright (C) 1999-2001 Toshiba Corporation
 * Copyright (C) 2003 Ralf Baechle (ralf@linux-mips.org)
 *
 * Permission is hereby granted to copy, modify and redistribute this code
 * in terms of the GNU Library General Public License, Version 2 or later,
 * at your option.
 */
#ifndef __LINUX_DS1742RTC_H
#define __LINUX_DS1742RTC_H

#include <asm/ds1742.h>

#define RTC_BRAM_SIZE		0x800
#define RTC_OFFSET		0x7f8

/*
 * Register summary
 */
#define RTC_CONTROL		(RTC_OFFSET + 0)
#define RTC_CENTURY		(RTC_OFFSET + 0)
#define RTC_SECONDS		(RTC_OFFSET + 1)
#define RTC_MINUTES		(RTC_OFFSET + 2)
#define RTC_HOURS		(RTC_OFFSET + 3)
#define RTC_DAY			(RTC_OFFSET + 4)
#define RTC_DATE		(RTC_OFFSET + 5)
#define RTC_MONTH		(RTC_OFFSET + 6)
#define RTC_YEAR		(RTC_OFFSET + 7)

#define RTC_CENTURY_MASK	0x3f
#define RTC_SECONDS_MASK	0x7f
#define RTC_DAY_MASK		0x07

/*
 * Bits in the Control/Century register
 */
#define RTC_WRITE		0x80
#define RTC_READ		0x40

/*
 * Bits in the Seconds register
 */
#define RTC_STOP		0x80

/*
 * Bits in the Day register
 */
#define RTC_BATT_FLAG		0x80
#define RTC_FREQ_TEST		0x40

#endif /* __LINUX_DS1742RTC_H */
