/*
 *  Registers for the SGS-Thomson M48T37 Timekeeper RAM chip
 */
#ifndef _ASM_M48T37_H
#define _ASM_M48T37_H

#include <linux/spinlock.h>

extern spinlock_t rtc_lock;

struct m48t37_rtc {
	volatile u8	pad[0x7ff0];    /* NVRAM */
	volatile u8	flags;
	volatile u8	century;
	volatile u8	alarm_sec;
	volatile u8	alarm_min;
	volatile u8	alarm_hour;
	volatile u8	alarm_data;
	volatile u8	interrupts;
	volatile u8	watchdog;
	volatile u8	control;
	volatile u8	sec;
	volatile u8	min;
	volatile u8	hour;
	volatile u8	day;
	volatile u8	date;
	volatile u8	month;
	volatile u8	year;
};

#define M48T37_RTC_SET		0x80
#define M48T37_RTC_STOPPED	0x80
#define M48T37_RTC_READ		0x40

#endif /* _ASM_M48T37_H */
