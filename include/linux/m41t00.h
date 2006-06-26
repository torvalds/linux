/*
 * Definitions for the ST M41T00 family of i2c rtc chips.
 *
 * Author: Mark A. Greer <mgreer@mvista.com>
 *
 * 2005, 2006 (c) MontaVista Software, Inc. This file is licensed under
 * the terms of the GNU General Public License version 2. This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#ifndef _M41T00_H
#define _M41T00_H

#define	M41T00_DRV_NAME		"m41t00"
#define	M41T00_I2C_ADDR		0x68

#define	M41T00_TYPE_M41T00	0
#define	M41T00_TYPE_M41T81	81
#define	M41T00_TYPE_M41T85	85

struct m41t00_platform_data {
	u8	type;
	u8	i2c_addr;
	u8	sqw_freq;
};

/* SQW output disabled, this is default value by power on */
#define M41T00_SQW_DISABLE	(0)

#define M41T00_SQW_32KHZ	(1<<4)		/* 32.768 KHz */
#define M41T00_SQW_8KHZ		(2<<4)		/* 8.192 KHz */
#define M41T00_SQW_4KHZ		(3<<4)		/* 4.096 KHz */
#define M41T00_SQW_2KHZ		(4<<4)		/* 2.048 KHz */
#define M41T00_SQW_1KHZ		(5<<4)		/* 1.024 KHz */
#define M41T00_SQW_512HZ	(6<<4)		/* 512 Hz */
#define M41T00_SQW_256HZ	(7<<4)		/* 256 Hz */
#define M41T00_SQW_128HZ	(8<<4)		/* 128 Hz */
#define M41T00_SQW_64HZ		(9<<4)		/* 64 Hz */
#define M41T00_SQW_32HZ		(10<<4)		/* 32 Hz */
#define M41T00_SQW_16HZ		(11<<4)		/* 16 Hz */
#define M41T00_SQW_8HZ		(12<<4)		/* 8 Hz */
#define M41T00_SQW_4HZ		(13<<4)		/* 4 Hz */
#define M41T00_SQW_2HZ		(14<<4)		/* 2 Hz */
#define M41T00_SQW_1HZ		(15<<4)		/* 1 Hz */

extern ulong m41t00_get_rtc_time(void);
extern int m41t00_set_rtc_time(ulong nowtime);

#endif /* _M41T00_H */
