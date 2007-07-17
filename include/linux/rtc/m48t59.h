/*
 * include/linux/rtc/m48t59.h
 *
 * Definitions for the platform data of m48t59 RTC chip driver.
 *
 * Copyright (c) 2007 Wind River Systems, Inc.
 *
 * Mark Zhan <rongkai.zhan@windriver.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _LINUX_RTC_M48T59_H_
#define _LINUX_RTC_M48T59_H_

/*
 * M48T59 Register Offset
 */
#define M48T59_YEAR		0x1fff
#define M48T59_MONTH		0x1ffe
#define M48T59_MDAY		0x1ffd	/* Day of Month */
#define M48T59_WDAY		0x1ffc	/* Day of Week */
#define M48T59_WDAY_CB			0x20	/* Century Bit */
#define M48T59_WDAY_CEB			0x10	/* Century Enable Bit */
#define M48T59_HOUR		0x1ffb
#define M48T59_MIN		0x1ffa
#define M48T59_SEC		0x1ff9
#define M48T59_CNTL		0x1ff8
#define M48T59_CNTL_READ		0x40
#define M48T59_CNTL_WRITE		0x80
#define M48T59_WATCHDOG		0x1ff7
#define M48T59_INTR		0x1ff6
#define M48T59_INTR_AFE			0x80	/* Alarm Interrupt Enable */
#define M48T59_INTR_ABE			0x20
#define M48T59_ALARM_DATE	0x1ff5
#define M48T59_ALARM_HOUR	0x1ff4
#define M48T59_ALARM_MIN	0x1ff3
#define M48T59_ALARM_SEC	0x1ff2
#define M48T59_UNUSED		0x1ff1
#define M48T59_FLAGS		0x1ff0
#define M48T59_FLAGS_WDT		0x80	/* watchdog timer expired */
#define M48T59_FLAGS_AF			0x40	/* alarm */
#define M48T59_FLAGS_BF			0x10	/* low battery */

#define M48T59_NVRAM_SIZE	0x1ff0

struct m48t59_plat_data {
	/* The method to access M48T59 registers,
	 * NOTE: The 'ofs' should be 0x00~0x1fff
	 */
	void (*write_byte)(struct device *dev, u32 ofs, u8 val);
	unsigned char (*read_byte)(struct device *dev, u32 ofs);
};

#endif /* _LINUX_RTC_M48T59_H_ */
