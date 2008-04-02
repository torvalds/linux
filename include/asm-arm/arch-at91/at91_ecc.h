/*
 * include/asm-arm/arch-at91/at91_ecc.h
 *
 * Error Corrected Code Controller (ECC) - System peripherals regsters.
 * Based on AT91SAM9260 datasheet revision B.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#ifndef AT91_ECC_H
#define AT91_ECC_H

#define AT91_ECC_CR		0x00			/* Control register */
#define		AT91_ECC_RST		(1 << 0)		/* Reset parity */

#define AT91_ECC_MR		0x04			/* Mode register */
#define		AT91_ECC_PAGESIZE	(3 << 0)		/* Page Size */
#define			AT91_ECC_PAGESIZE_528		(0)
#define			AT91_ECC_PAGESIZE_1056		(1)
#define			AT91_ECC_PAGESIZE_2112		(2)
#define			AT91_ECC_PAGESIZE_4224		(3)

#define AT91_ECC_SR		0x08			/* Status register */
#define		AT91_ECC_RECERR		(1 << 0)		/* Recoverable Error */
#define		AT91_ECC_ECCERR		(1 << 1)		/* ECC Single Bit Error */
#define		AT91_ECC_MULERR		(1 << 2)		/* Multiple Errors */

#define AT91_ECC_PR		0x0c			/* Parity register */
#define		AT91_ECC_BITADDR	(0xf << 0)		/* Bit Error Address */
#define		AT91_ECC_WORDADDR	(0xfff << 4)		/* Word Error Address */

#define AT91_ECC_NPR		0x10			/* NParity register */
#define		AT91_ECC_NPARITY	(0xffff << 0)		/* NParity */

#endif
