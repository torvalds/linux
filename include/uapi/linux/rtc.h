/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Generic RTC interface.
 * This version contains the part of the user interface to the Real Time Clock
 * service. It is used with both the legacy mc146818 and also  EFI
 * Struct rtc_time and first 12 ioctl by Paul Gortmaker, 1996 - separated out
 * from <linux/mc146818rtc.h> to this file for 2.4 kernels.
 *
 * Copyright (C) 1999 Hewlett-Packard Co.
 * Copyright (C) 1999 Stephane Eranian <eranian@hpl.hp.com>
 */
#ifndef _UAPI_LINUX_RTC_H_
#define _UAPI_LINUX_RTC_H_

#include <linux/const.h>
#include <linux/ioctl.h>

/*
 * The struct used to pass data via the following ioctl. Similar to the
 * struct tm in <time.h>, but it needs to be here so that the kernel
 * source is self contained, allowing cross-compiles, etc. etc.
 */

struct rtc_time {
	int tm_sec;
	int tm_min;
	int tm_hour;
	int tm_mday;
	int tm_mon;
	int tm_year;
	int tm_wday;
	int tm_yday;
	int tm_isdst;
};

/*
 * This data structure is inspired by the EFI (v0.92) wakeup
 * alarm API.
 */
struct rtc_wkalrm {
	unsigned char enabled;	/* 0 = alarm disabled, 1 = alarm enabled */
	unsigned char pending;  /* 0 = alarm not pending, 1 = alarm pending */
	struct rtc_time time;	/* time the alarm is set to */
};

/*
 * Data structure to control PLL correction some better RTC feature
 * pll_value is used to get or set current value of correction,
 * the rest of the struct is used to query HW capabilities.
 * This is modeled after the RTC used in Q40/Q60 computers but
 * should be sufficiently flexible for other devices
 *
 * +ve pll_value means clock will run faster by
 *   pll_value*pll_posmult/pll_clock
 * -ve pll_value means clock will run slower by
 *   pll_value*pll_negmult/pll_clock
 */

struct rtc_pll_info {
	int pll_ctrl;       /* placeholder for fancier control */
	int pll_value;      /* get/set correction value */
	int pll_max;        /* max +ve (faster) adjustment value */
	int pll_min;        /* max -ve (slower) adjustment value */
	int pll_posmult;    /* factor for +ve correction */
	int pll_negmult;    /* factor for -ve correction */
	long pll_clock;     /* base PLL frequency */
};

/*
 * ioctl calls that are permitted to the /dev/rtc interface, if
 * any of the RTC drivers are enabled.
 */

#define RTC_AIE_ON	_IO('p', 0x01)	/* Alarm int. enable on		*/
#define RTC_AIE_OFF	_IO('p', 0x02)	/* ... off			*/
#define RTC_UIE_ON	_IO('p', 0x03)	/* Update int. enable on	*/
#define RTC_UIE_OFF	_IO('p', 0x04)	/* ... off			*/
#define RTC_PIE_ON	_IO('p', 0x05)	/* Periodic int. enable on	*/
#define RTC_PIE_OFF	_IO('p', 0x06)	/* ... off			*/
#define RTC_WIE_ON	_IO('p', 0x0f)  /* Watchdog int. enable on	*/
#define RTC_WIE_OFF	_IO('p', 0x10)  /* ... off			*/

#define RTC_ALM_SET	_IOW('p', 0x07, struct rtc_time) /* Set alarm time  */
#define RTC_ALM_READ	_IOR('p', 0x08, struct rtc_time) /* Read alarm time */
#define RTC_RD_TIME	_IOR('p', 0x09, struct rtc_time) /* Read RTC time   */
#define RTC_SET_TIME	_IOW('p', 0x0a, struct rtc_time) /* Set RTC time    */
#define RTC_IRQP_READ	_IOR('p', 0x0b, unsigned long)	 /* Read IRQ rate   */
#define RTC_IRQP_SET	_IOW('p', 0x0c, unsigned long)	 /* Set IRQ rate    */
#define RTC_EPOCH_READ	_IOR('p', 0x0d, unsigned long)	 /* Read epoch      */
#define RTC_EPOCH_SET	_IOW('p', 0x0e, unsigned long)	 /* Set epoch       */

#define RTC_WKALM_SET	_IOW('p', 0x0f, struct rtc_wkalrm)/* Set wakeup alarm*/
#define RTC_WKALM_RD	_IOR('p', 0x10, struct rtc_wkalrm)/* Get wakeup alarm*/

#define RTC_PLL_GET	_IOR('p', 0x11, struct rtc_pll_info)  /* Get PLL correction */
#define RTC_PLL_SET	_IOW('p', 0x12, struct rtc_pll_info)  /* Set PLL correction */

#define RTC_VL_DATA_INVALID	_BITUL(0) /* Voltage too low, RTC data is invalid */
#define RTC_VL_BACKUP_LOW	_BITUL(1) /* Backup voltage is low */
#define RTC_VL_BACKUP_EMPTY	_BITUL(2) /* Backup empty or not present */
#define RTC_VL_ACCURACY_LOW	_BITUL(3) /* Voltage is low, RTC accuracy is reduced */
#define RTC_VL_BACKUP_SWITCH	_BITUL(4) /* Backup switchover happened */

#define RTC_VL_READ	_IOR('p', 0x13, unsigned int)	/* Voltage low detection */
#define RTC_VL_CLR	_IO('p', 0x14)		/* Clear voltage low information */

/* interrupt flags */
#define RTC_IRQF 0x80	/* Any of the following is active */
#define RTC_PF 0x40	/* Periodic interrupt */
#define RTC_AF 0x20	/* Alarm interrupt */
#define RTC_UF 0x10	/* Update interrupt for 1Hz RTC */


#define RTC_MAX_FREQ	8192


#endif /* _UAPI_LINUX_RTC_H_ */
