/*
 * s5m-rtc.h
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd
 *              http://www.samsung.com
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#ifndef __LINUX_MFD_S5M_RTC_H
#define __LINUX_MFD_S5M_RTC_H

enum s5m87xx_rtc_reg {
	S5M87XX_RTC_SEC,
	S5M87XX_RTC_MIN,
	S5M87XX_RTC_HOUR,
	S5M87XX_RTC_WEEKDAY,
	S5M87XX_RTC_DATE,
	S5M87XX_RTC_MONTH,
	S5M87XX_RTC_YEAR1,
	S5M87XX_RTC_YEAR2,
	S5M87XX_ALARM0_SEC,
	S5M87XX_ALARM0_MIN,
	S5M87XX_ALARM0_HOUR,
	S5M87XX_ALARM0_WEEKDAY,
	S5M87XX_ALARM0_DATE,
	S5M87XX_ALARM0_MONTH,
	S5M87XX_ALARM0_YEAR1,
	S5M87XX_ALARM0_YEAR2,
	S5M87XX_ALARM1_SEC,
	S5M87XX_ALARM1_MIN,
	S5M87XX_ALARM1_HOUR,
	S5M87XX_ALARM1_WEEKDAY,
	S5M87XX_ALARM1_DATE,
	S5M87XX_ALARM1_MONTH,
	S5M87XX_ALARM1_YEAR1,
	S5M87XX_ALARM1_YEAR2,
	S5M87XX_ALARM0_CONF,
	S5M87XX_ALARM1_CONF,
	S5M87XX_RTC_STATUS,
	S5M87XX_WTSR_SMPL_CNTL,
	S5M87XX_RTC_UDR_CON,
};

#define RTC_I2C_ADDR		(0x0C >> 1)

#define HOUR_12			(1 << 7)
#define HOUR_AMPM		(1 << 6)
#define HOUR_PM			(1 << 5)
#define ALARM0_STATUS		(1 << 1)
#define ALARM1_STATUS		(1 << 2)
#define UPDATE_AD		(1 << 0)

/* RTC Control Register */
#define BCD_EN_SHIFT		0
#define BCD_EN_MASK		(1 << BCD_EN_SHIFT)
#define MODEL24_SHIFT		1
#define MODEL24_MASK		(1 << MODEL24_SHIFT)
/* RTC Update Register1 */
#define RTC_UDR_SHIFT		0
#define RTC_UDR_MASK		(1 << RTC_UDR_SHIFT)
#define RTC_TCON_SHIFT		1
#define RTC_TCON_MASK		(1 << RTC_TCON_SHIFT)
#define RTC_TIME_EN_SHIFT	3
#define RTC_TIME_EN_MASK	(1 << RTC_TIME_EN_SHIFT)

/* RTC Hour register */
#define HOUR_PM_SHIFT		6
#define HOUR_PM_MASK		(1 << HOUR_PM_SHIFT)
/* RTC Alarm Enable */
#define ALARM_ENABLE_SHIFT	7
#define ALARM_ENABLE_MASK	(1 << ALARM_ENABLE_SHIFT)

#define SMPL_ENABLE_SHIFT	7
#define SMPL_ENABLE_MASK	(1 << SMPL_ENABLE_SHIFT)

#define WTSR_ENABLE_SHIFT	6
#define WTSR_ENABLE_MASK	(1 << WTSR_ENABLE_SHIFT)

enum {
	RTC_SEC = 0,
	RTC_MIN,
	RTC_HOUR,
	RTC_WEEKDAY,
	RTC_DATE,
	RTC_MONTH,
	RTC_YEAR1,
	RTC_YEAR2,
};

#endif /*  __LINUX_MFD_S5M_RTC_H */
