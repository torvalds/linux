/* rtc.h
 *
 * Copyright (c) 2011-2014 Samsung Electronics Co., Ltd
 *              http://www.samsung.com
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __LINUX_MFD_SEC_RTC_H
#define __LINUX_MFD_SEC_RTC_H

enum sec_rtc_reg {
	SEC_RTC_SEC,
	SEC_RTC_MIN,
	SEC_RTC_HOUR,
	SEC_RTC_WEEKDAY,
	SEC_RTC_DATE,
	SEC_RTC_MONTH,
	SEC_RTC_YEAR1,
	SEC_RTC_YEAR2,
	SEC_ALARM0_SEC,
	SEC_ALARM0_MIN,
	SEC_ALARM0_HOUR,
	SEC_ALARM0_WEEKDAY,
	SEC_ALARM0_DATE,
	SEC_ALARM0_MONTH,
	SEC_ALARM0_YEAR1,
	SEC_ALARM0_YEAR2,
	SEC_ALARM1_SEC,
	SEC_ALARM1_MIN,
	SEC_ALARM1_HOUR,
	SEC_ALARM1_WEEKDAY,
	SEC_ALARM1_DATE,
	SEC_ALARM1_MONTH,
	SEC_ALARM1_YEAR1,
	SEC_ALARM1_YEAR2,
	SEC_ALARM0_CONF,
	SEC_ALARM1_CONF,
	SEC_RTC_STATUS,
	SEC_WTSR_SMPL_CNTL,
	SEC_RTC_UDR_CON,

	SEC_RTC_REG_MAX,
};

enum s2mps_rtc_reg {
	S2MPS_RTC_CTRL,
	S2MPS_WTSR_SMPL_CNTL,
	S2MPS_RTC_UDR_CON,
	S2MPS_RSVD,
	S2MPS_RTC_SEC,
	S2MPS_RTC_MIN,
	S2MPS_RTC_HOUR,
	S2MPS_RTC_WEEKDAY,
	S2MPS_RTC_DATE,
	S2MPS_RTC_MONTH,
	S2MPS_RTC_YEAR,
	S2MPS_ALARM0_SEC,
	S2MPS_ALARM0_MIN,
	S2MPS_ALARM0_HOUR,
	S2MPS_ALARM0_WEEKDAY,
	S2MPS_ALARM0_DATE,
	S2MPS_ALARM0_MONTH,
	S2MPS_ALARM0_YEAR,
	S2MPS_ALARM1_SEC,
	S2MPS_ALARM1_MIN,
	S2MPS_ALARM1_HOUR,
	S2MPS_ALARM1_WEEKDAY,
	S2MPS_ALARM1_DATE,
	S2MPS_ALARM1_MONTH,
	S2MPS_ALARM1_YEAR,
	S2MPS_OFFSRC,

	S2MPS_RTC_REG_MAX,
};

#define RTC_I2C_ADDR		(0x0C >> 1)

#define HOUR_12			(1 << 7)
#define HOUR_AMPM		(1 << 6)
#define HOUR_PM			(1 << 5)
#define ALARM0_STATUS		(1 << 1)
#define ALARM1_STATUS		(1 << 2)
#define UPDATE_AD		(1 << 0)

#define S2MPS_ALARM0_STATUS	(1 << 2)
#define S2MPS_ALARM1_STATUS	(1 << 1)

/* RTC Control Register */
#define BCD_EN_SHIFT		0
#define BCD_EN_MASK		(1 << BCD_EN_SHIFT)
#define MODEL24_SHIFT		1
#define MODEL24_MASK		(1 << MODEL24_SHIFT)
/* RTC Update Register1 */
#define RTC_UDR_SHIFT		0
#define RTC_UDR_MASK		(1 << RTC_UDR_SHIFT)
#define S2MPS_RTC_WUDR_SHIFT	4
#define S2MPS_RTC_WUDR_MASK	(1 << S2MPS_RTC_WUDR_SHIFT)
#define S2MPS_RTC_RUDR_SHIFT	0
#define S2MPS_RTC_RUDR_MASK	(1 << S2MPS_RTC_RUDR_SHIFT)
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

#endif /*  __LINUX_MFD_SEC_RTC_H */
