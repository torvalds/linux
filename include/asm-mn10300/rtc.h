/* MN10300 Real time clock definitions
 *
 * Copyright (C) 2007 Matsushita Electric Industrial Co., Ltd.
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */
#ifndef _ASM_RTC_H
#define _ASM_RTC_H

#ifdef CONFIG_MN10300_RTC

#include <linux/init.h>

extern void check_rtc_time(void);
extern void __init calibrate_clock(void);
extern unsigned long __init get_initial_rtc_time(void);

#else /* !CONFIG_MN10300_RTC */

static inline void check_rtc_time(void)
{
}

static inline void calibrate_clock(void)
{
}

static inline unsigned long get_initial_rtc_time(void)
{
	return 0;
}

#endif /* !CONFIG_MN10300_RTC */

#include <asm-generic/rtc.h>

#endif /* _ASM_RTC_H */
