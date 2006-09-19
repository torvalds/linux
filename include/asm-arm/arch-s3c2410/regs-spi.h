/* linux/include/asm-arm/arch-s3c2410/regs-spi.h
 *
 * Copyright (c) 2004 Fetron GmbH
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * S3C2410 SPI register definition
*/

#ifndef __ASM_ARCH_REGS_SPI_H
#define __ASM_ARCH_REGS_SPI_H


#define S3C2410_SPCON	(0x00)

#define S3C2410_SPCON_SMOD_DMA	  (2<<5)	/* DMA mode */
#define S3C2410_SPCON_SMOD_INT	  (1<<5)	/* interrupt mode */
#define S3C2410_SPCON_SMOD_POLL   (0<<5)	/* polling mode */
#define S3C2410_SPCON_ENSCK	  (1<<4)	/* Enable SCK */
#define S3C2410_SPCON_MSTR	  (1<<3)	/* Master/Slave select
						   0: slave, 1: master */
#define S3C2410_SPCON_CPOL_HIGH	  (1<<2)	/* Clock polarity select */
#define S3C2410_SPCON_CPOL_LOW	  (0<<2)	/* Clock polarity select */

#define S3C2410_SPCON_CPHA_FMTB	  (1<<1)	/* Clock Phase Select */
#define S3C2410_SPCON_CPHA_FMTA	  (0<<1)	/* Clock Phase Select */

#define S3C2410_SPCON_TAGD	  (1<<0)	/* Tx auto garbage data mode */


#define S3C2410_SPSTA	 (0x04)

#define S3C2410_SPSTA_DCOL	  (1<<2)	/* Data Collision Error */
#define S3C2410_SPSTA_MULD	  (1<<1)	/* Multi Master Error */
#define S3C2410_SPSTA_READY	  (1<<0)	/* Data Tx/Rx ready */


#define S3C2410_SPPIN	 (0x08)

#define S3C2410_SPPIN_ENMUL	  (1<<2)	/* Multi Master Error detect */
#define S3C2410_SPPIN_RESERVED	  (1<<1)
#define S3C2400_SPPIN_nCS     	  (1<<1)	/* SPI Card Select */
#define S3C2410_SPPIN_KEEP	  (1<<0)	/* Master Out keep */


#define S3C2410_SPPRE	 (0x0C)
#define S3C2410_SPTDAT	 (0x10)
#define S3C2410_SPRDAT	 (0x14)

#endif /* __ASM_ARCH_REGS_SPI_H */
