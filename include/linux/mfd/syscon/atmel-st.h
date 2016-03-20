/*
 * Copyright (C) 2005 Ivan Kokshaysky
 * Copyright (C) SAN People
 *
 * System Timer (ST) - System peripherals registers.
 * Based on AT91RM9200 datasheet revision E.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef _LINUX_MFD_SYSCON_ATMEL_ST_H
#define _LINUX_MFD_SYSCON_ATMEL_ST_H

#include <linux/bitops.h>

#define AT91_ST_CR	0x00	/* Control Register */
#define		AT91_ST_WDRST	BIT(0)	/* Watchdog Timer Restart */

#define AT91_ST_PIMR	0x04	/* Period Interval Mode Register */
#define		AT91_ST_PIV	0xffff	/* Period Interval Value */

#define AT91_ST_WDMR	0x08	/* Watchdog Mode Register */
#define		AT91_ST_WDV	0xffff	/* Watchdog Counter Value */
#define		AT91_ST_RSTEN	BIT(16)	/* Reset Enable */
#define		AT91_ST_EXTEN	BIT(17)	/* External Signal Assertion Enable */

#define AT91_ST_RTMR	0x0c	/* Real-time Mode Register */
#define		AT91_ST_RTPRES	0xffff	/* Real-time Prescalar Value */

#define AT91_ST_SR	0x10	/* Status Register */
#define		AT91_ST_PITS	BIT(0)	/* Period Interval Timer Status */
#define		AT91_ST_WDOVF	BIT(1)	/* Watchdog Overflow */
#define		AT91_ST_RTTINC	BIT(2)	/* Real-time Timer Increment */
#define		AT91_ST_ALMS	BIT(3)	/* Alarm Status */

#define AT91_ST_IER	0x14	/* Interrupt Enable Register */
#define AT91_ST_IDR	0x18	/* Interrupt Disable Register */
#define AT91_ST_IMR	0x1c	/* Interrupt Mask Register */

#define AT91_ST_RTAR	0x20	/* Real-time Alarm Register */
#define		AT91_ST_ALMV	0xfffff	/* Alarm Value */

#define AT91_ST_CRTR	0x24	/* Current Real-time Register */
#define		AT91_ST_CRTV	0xfffff	/* Current Real-Time Value */

#endif /* _LINUX_MFD_SYSCON_ATMEL_ST_H */
