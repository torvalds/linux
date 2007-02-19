/*
 * include/asm-arm/arch-at91/at91_shdwc.h
 *
 * Shutdown Controller (SHDWC) - System peripherals regsters.
 * Based on AT91SAM9261 datasheet revision D.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef AT91_SHDWC_H
#define AT91_SHDWC_H

#define AT91_SHDW_CR		(AT91_SHDWC + 0x00)	/* Shut Down Control Register */
#define		AT91_SHDW_SHDW		(1    << 0)		/* Processor Reset */
#define		AT91_SHDW_KEY		(0xff << 24)		/* KEY Password */

#define AT91_SHDW_MR		(AT91_SHDWC + 0x04)	/* Shut Down Mode Register */
#define		AT91_SHDW_WKMODE0	(3 << 0)		/* Wake-up 0 Mode Selection */
#define			AT91_SHDW_WKMODE0_NONE		0
#define			AT91_SHDW_WKMODE0_HIGH		1
#define			AT91_SHDW_WKMODE0_LOW		2
#define			AT91_SHDW_WKMODE0_ANYLEVEL	3
#define		AT91_SHDW_CPTWK0	(0xf << 4)		/* Counter On Wake Up 0 */
#define		AT91_SHDW_RTTWKEN	(1   << 16)		/* Real Time Timer Wake-up Enable */

#define AT91_SHDW_SR		(AT91_SHDWC + 0x08)	/* Shut Down Status Register */
#define		AT91_SHDW_WAKEUP0	(1 <<  0)		/* Wake-up 0 Status */
#define		AT91_SHDW_RTTWK		(1 << 16)		/* Real-time Timer Wake-up */

#endif
