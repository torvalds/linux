/*
 *  linux/include/asm-arm/rtc.h
 *
 *  Copyright (C) 2003 Deep Blue Solutions Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef ASMARM_RTC_H
#define ASMARM_RTC_H

struct module;

struct rtc_ops {
	struct module	*owner;
	int		(*open)(void);
	void		(*release)(void);
	int		(*ioctl)(unsigned int, unsigned long);

	int		(*read_time)(struct rtc_time *);
	int		(*set_time)(struct rtc_time *);
	int		(*read_alarm)(struct rtc_wkalrm *);
	int		(*set_alarm)(struct rtc_wkalrm *);
	int		(*proc)(char *buf);
};

void rtc_next_alarm_time(struct rtc_time *, struct rtc_time *, struct rtc_time *);
void rtc_update(unsigned long, unsigned long);
int register_rtc(struct rtc_ops *);
void unregister_rtc(struct rtc_ops *);

static inline int rtc_periodic_alarm(struct rtc_time *tm)
{
	return  (tm->tm_year == -1) ||
		((unsigned)tm->tm_mon >= 12) ||
		((unsigned)(tm->tm_mday - 1) >= 31) ||
		((unsigned)tm->tm_hour > 23) ||
		((unsigned)tm->tm_min > 59) ||
		((unsigned)tm->tm_sec > 59);
}

#endif
