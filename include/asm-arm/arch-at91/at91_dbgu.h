/*
 * include/asm-arm/arch-at91/at91_dbgu.h
 *
 * Copyright (C) 2005 Ivan Kokshaysky
 * Copyright (C) SAN People
 *
 * Debug Unit (DBGU) - System peripherals registers.
 * Based on AT91RM9200 datasheet revision E.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef AT91_DBGU_H
#define AT91_DBGU_H

#ifdef AT91_DBGU
#define AT91_DBGU_CR		(AT91_DBGU + 0x00)	/* Control Register */
#define AT91_DBGU_MR		(AT91_DBGU + 0x04)	/* Mode Register */
#define AT91_DBGU_IER		(AT91_DBGU + 0x08)	/* Interrupt Enable Register */
#define		AT91_DBGU_TXRDY		(1 << 1)		/* Transmitter Ready */
#define		AT91_DBGU_TXEMPTY	(1 << 9)		/* Transmitter Empty */
#define AT91_DBGU_IDR		(AT91_DBGU + 0x0c)	/* Interrupt Disable Register */
#define AT91_DBGU_IMR		(AT91_DBGU + 0x10)	/* Interrupt Mask Register */
#define AT91_DBGU_SR		(AT91_DBGU + 0x14)	/* Status Register */
#define AT91_DBGU_RHR		(AT91_DBGU + 0x18)	/* Receiver Holding Register */
#define AT91_DBGU_THR		(AT91_DBGU + 0x1c)	/* Transmitter Holding Register */
#define AT91_DBGU_BRGR		(AT91_DBGU + 0x20)	/* Baud Rate Generator Register */

#define AT91_DBGU_CIDR		(AT91_DBGU + 0x40)	/* Chip ID Register */
#define AT91_DBGU_EXID		(AT91_DBGU + 0x44)	/* Chip ID Extension Register */
#define AT91_DBGU_FNR		(AT91_DBGU + 0x48)	/* Force NTRST Register [SAM9 only] */
#define		AT91_DBGU_FNTRST	(1 << 0)		/* Force NTRST */

#endif /* AT91_DBGU */

/*
 * Some AT91 parts that don't have full DEBUG units still support the ID
 * and extensions register.
 */
#define		AT91_CIDR_VERSION	(0x1f << 0)		/* Version of the Device */
#define		AT91_CIDR_EPROC		(7    << 5)		/* Embedded Processor */
#define		AT91_CIDR_NVPSIZ	(0xf  << 8)		/* Nonvolatile Program Memory Size */
#define		AT91_CIDR_NVPSIZ2	(0xf  << 12)		/* Second Nonvolatile Program Memory Size */
#define		AT91_CIDR_SRAMSIZ	(0xf  << 16)		/* Internal SRAM Size */
#define			AT91_CIDR_SRAMSIZ_1K	(1 << 16)
#define			AT91_CIDR_SRAMSIZ_2K	(2 << 16)
#define			AT91_CIDR_SRAMSIZ_112K	(4 << 16)
#define			AT91_CIDR_SRAMSIZ_4K	(5 << 16)
#define			AT91_CIDR_SRAMSIZ_80K	(6 << 16)
#define			AT91_CIDR_SRAMSIZ_160K	(7 << 16)
#define			AT91_CIDR_SRAMSIZ_8K	(8 << 16)
#define			AT91_CIDR_SRAMSIZ_16K	(9 << 16)
#define			AT91_CIDR_SRAMSIZ_32K	(10 << 16)
#define			AT91_CIDR_SRAMSIZ_64K	(11 << 16)
#define			AT91_CIDR_SRAMSIZ_128K	(12 << 16)
#define			AT91_CIDR_SRAMSIZ_256K	(13 << 16)
#define			AT91_CIDR_SRAMSIZ_96K	(14 << 16)
#define			AT91_CIDR_SRAMSIZ_512K	(15 << 16)
#define		AT91_CIDR_ARCH		(0xff << 20)		/* Architecture Identifier */
#define		AT91_CIDR_NVPTYP	(7    << 28)		/* Nonvolatile Program Memory Type */
#define		AT91_CIDR_EXT		(1    << 31)		/* Extension Flag */

#endif
