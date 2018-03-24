
// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2014-2018 MediaTek Inc.
 *
 * Author: Tianping.Fang <tianping.fang@mediatek.com>
 *	   Sean Wang <sean.wang@mediatek.com>
 */

#ifndef _LINUX_RTC_MT6397_H_
#define _LINUX_RTC_MT6397_H_

#include <linux/jiffies.h>
#include <linux/mutex.h>
#include <linux/regmap.h>
#include <linux/rtc.h>

#define RTC_BBPU		0x0000
#define RTC_BBPU_CBUSY		BIT(6)
#define RTC_BBPU_KEY            (0x43 << 8)

#define RTC_WRTGR		0x003c

#define RTC_IRQ_STA		0x0002
#define RTC_IRQ_STA_AL		BIT(0)
#define RTC_IRQ_STA_LP		BIT(3)

#define RTC_IRQ_EN		0x0004
#define RTC_IRQ_EN_AL		BIT(0)
#define RTC_IRQ_EN_ONESHOT	BIT(2)
#define RTC_IRQ_EN_LP		BIT(3)
#define RTC_IRQ_EN_ONESHOT_AL	(RTC_IRQ_EN_ONESHOT | RTC_IRQ_EN_AL)

#define RTC_AL_MASK		0x0008
#define RTC_AL_MASK_DOW		BIT(4)

#define RTC_TC_SEC		0x000a
/* Min, Hour, Dom... register offset to RTC_TC_SEC */
#define RTC_OFFSET_SEC		0
#define RTC_OFFSET_MIN		1
#define RTC_OFFSET_HOUR		2
#define RTC_OFFSET_DOM		3
#define RTC_OFFSET_DOW		4
#define RTC_OFFSET_MTH		5
#define RTC_OFFSET_YEAR		6
#define RTC_OFFSET_COUNT	7

#define RTC_AL_SEC		0x0018

#define RTC_PDN2		0x002e
#define RTC_PDN2_PWRON_ALARM	BIT(4)

#define RTC_MIN_YEAR		1968
#define RTC_BASE_YEAR		1900
#define RTC_NUM_YEARS		128
#define RTC_MIN_YEAR_OFFSET	(RTC_MIN_YEAR - RTC_BASE_YEAR)

#define MTK_RTC_POLL_DELAY_US	10
#define MTK_RTC_POLL_TIMEOUT	(jiffies_to_usecs(HZ))

struct mt6397_rtc {
	struct device		*dev;
	struct rtc_device	*rtc_dev;

	/* protect registers accessing */
	struct mutex		lock;
	struct regmap		*regmap;
	int			irq;
	u32			addr_base;
};

#endif /* _LINUX_RTC_MT6397_H_ */

