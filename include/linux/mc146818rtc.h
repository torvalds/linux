/* mc146818rtc.h - register definitions for the Real-Time-Clock / CMOS RAM
 * Copyright Torsten Duwe <duwe@informatik.uni-erlangen.de> 1993
 * derived from Data Sheet, Copyright Motorola 1984 (!).
 * It was written to be part of the Linux operating system.
 */
/* permission is hereby granted to copy, modify and redistribute this code
 * in terms of the GNU Library General Public License, Version 2 or later,
 * at your option.
 */

#ifndef _MC146818RTC_H
#define _MC146818RTC_H

#include <asm/io.h>
#include <linux/rtc.h>			/* get the user-level API */
#include <asm/mc146818rtc.h>		/* register access macros */

#ifdef __KERNEL__
#include <linux/spinlock.h>		/* spinlock_t */
extern spinlock_t rtc_lock;		/* serialize CMOS RAM access */

/* Some RTCs extend the mc146818 register set to support alarms of more
 * than 24 hours in the future; or dates that include a century code.
 * This platform_data structure can pass this information to the driver.
 */
struct cmos_rtc_board_info {
	u8	rtc_day_alarm;		/* zero, or register index */
	u8	rtc_mon_alarm;		/* zero, or register index */
	u8	rtc_century;		/* zero, or register index */
};
#endif

/**********************************************************************
 * register summary
 **********************************************************************/
#define RTC_SECONDS		0
#define RTC_SECONDS_ALARM	1
#define RTC_MINUTES		2
#define RTC_MINUTES_ALARM	3
#define RTC_HOURS		4
#define RTC_HOURS_ALARM		5
/* RTC_*_alarm is always true if 2 MSBs are set */
# define RTC_ALARM_DONT_CARE 	0xC0

#define RTC_DAY_OF_WEEK		6
#define RTC_DAY_OF_MONTH	7
#define RTC_MONTH		8
#define RTC_YEAR		9

/* control registers - Moto names
 */
#define RTC_REG_A		10
#define RTC_REG_B		11
#define RTC_REG_C		12
#define RTC_REG_D		13

/**********************************************************************
 * register details
 **********************************************************************/
#define RTC_FREQ_SELECT	RTC_REG_A

/* update-in-progress  - set to "1" 244 microsecs before RTC goes off the bus,
 * reset after update (may take 1.984ms @ 32768Hz RefClock) is complete,
 * totalling to a max high interval of 2.228 ms.
 */
# define RTC_UIP		0x80
# define RTC_DIV_CTL		0x70
   /* divider control: refclock values 4.194 / 1.049 MHz / 32.768 kHz */
#  define RTC_REF_CLCK_4MHZ	0x00
#  define RTC_REF_CLCK_1MHZ	0x10
#  define RTC_REF_CLCK_32KHZ	0x20
   /* 2 values for divider stage reset, others for "testing purposes only" */
#  define RTC_DIV_RESET1	0x60
#  define RTC_DIV_RESET2	0x70
  /* Periodic intr. / Square wave rate select. 0=none, 1=32.8kHz,... 15=2Hz */
# define RTC_RATE_SELECT 	0x0F

/**********************************************************************/
#define RTC_CONTROL	RTC_REG_B
# define RTC_SET 0x80		/* disable updates for clock setting */
# define RTC_PIE 0x40		/* periodic interrupt enable */
# define RTC_AIE 0x20		/* alarm interrupt enable */
# define RTC_UIE 0x10		/* update-finished interrupt enable */
# define RTC_SQWE 0x08		/* enable square-wave output */
# define RTC_DM_BINARY 0x04	/* all time/date values are BCD if clear */
# define RTC_24H 0x02		/* 24 hour mode - else hours bit 7 means pm */
# define RTC_DST_EN 0x01	/* auto switch DST - works f. USA only */

/**********************************************************************/
#define RTC_INTR_FLAGS	RTC_REG_C
/* caution - cleared by read */
# define RTC_IRQF 0x80		/* any of the following 3 is active */
# define RTC_PF 0x40
# define RTC_AF 0x20
# define RTC_UF 0x10

/**********************************************************************/
#define RTC_VALID	RTC_REG_D
# define RTC_VRT 0x80		/* valid RAM and time */
/**********************************************************************/

#ifndef ARCH_RTC_LOCATION	/* Override by <asm/mc146818rtc.h>? */

#define RTC_IO_EXTENT	0x8
#define RTC_IOMAPPED	1	/* Default to I/O mapping. */

#endif /* ARCH_RTC_LOCATION */

#endif /* _MC146818RTC_H */
