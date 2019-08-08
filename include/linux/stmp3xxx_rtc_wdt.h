/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * stmp3xxx_rtc_wdt.h
 *
 * Copyright (C) 2011 Wolfram Sang, Pengutronix e.K.
 */
#ifndef __LINUX_STMP3XXX_RTC_WDT_H
#define __LINUX_STMP3XXX_RTC_WDT_H

struct stmp3xxx_wdt_pdata {
	void (*wdt_set_timeout)(struct device *dev, u32 timeout);
};

#endif /* __LINUX_STMP3XXX_RTC_WDT_H */
