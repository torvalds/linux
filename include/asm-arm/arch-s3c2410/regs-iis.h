/* linux/include/asm/arch-s3c2410/regs-iis.h
 *
 * Copyright (c) 2003 Simtec Electronics <linux@simtec.co.uk>
 *		      http://www.simtec.co.uk/products/SWLINUX/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * S3C2410 IIS register definition
 *
 *  Changelog:
 *    19-06-2003     BJD     Created file
 *    26-06-2003     BJD     Finished off definitions for register addresses
 *    12-03-2004     BJD     Updated include protection
 *    07-03-2005     BJD     Added FIFO size flags and S3C2440 MPLL
 *    05-04-2005     LCVR    Added IISFCON definitions for the S3C2400
 */

#ifndef __ASM_ARCH_REGS_IIS_H
#define __ASM_ARCH_REGS_IIS_H

#define S3C2410_IISCON	 (0x00)

#define S3C2440_IISCON_MPLL	  (1<<9)
#define S3C2410_IISCON_LRINDEX	  (1<<8)
#define S3C2410_IISCON_TXFIFORDY  (1<<7)
#define S3C2410_IISCON_RXFIFORDY  (1<<6)
#define S3C2410_IISCON_TXDMAEN	  (1<<5)
#define S3C2410_IISCON_RXDMAEN	  (1<<4)
#define S3C2410_IISCON_TXIDLE	  (1<<3)
#define S3C2410_IISCON_RXIDLE	  (1<<2)
#define S3C2410_IISCON_IISEN	  (1<<0)

#define S3C2410_IISMOD	 (0x04)

#define S3C2410_IISMOD_SLAVE	  (1<<8)
#define S3C2410_IISMOD_NOXFER	  (0<<6)
#define S3C2410_IISMOD_RXMODE	  (1<<6)
#define S3C2410_IISMOD_TXMODE	  (2<<6)
#define S3C2410_IISMOD_TXRXMODE	  (3<<6)
#define S3C2410_IISMOD_LR_LLOW	  (0<<5)
#define S3C2410_IISMOD_LR_RLOW	  (1<<5)
#define S3C2410_IISMOD_IIS	  (0<<4)
#define S3C2410_IISMOD_MSB	  (1<<4)
#define S3C2410_IISMOD_8BIT	  (0<<3)
#define S3C2410_IISMOD_16BIT	  (1<<3)
#define S3C2410_IISMOD_BITMASK	  (1<<3)
#define S3C2410_IISMOD_256FS	  (0<<1)
#define S3C2410_IISMOD_384FS	  (1<<1)
#define S3C2410_IISMOD_16FS	  (0<<0)
#define S3C2410_IISMOD_32FS	  (1<<0)
#define S3C2410_IISMOD_48FS	  (2<<0)

#define S3C2410_IISPSR		(0x08)
#define S3C2410_IISPSR_INTMASK	(31<<5)
#define S3C2410_IISPSR_INTSHIFT	(5)
#define S3C2410_IISPSR_EXTMASK	(31<<0)
#define S3C2410_IISPSR_EXTSHFIT	(0)

#define S3C2410_IISFCON  (0x0c)

#define S3C2410_IISFCON_TXDMA	  (1<<15)
#define S3C2410_IISFCON_RXDMA	  (1<<14)
#define S3C2410_IISFCON_TXENABLE  (1<<13)
#define S3C2410_IISFCON_RXENABLE  (1<<12)
#define S3C2410_IISFCON_TXMASK	  (0x3f << 6)
#define S3C2410_IISFCON_TXSHIFT	  (6)
#define S3C2410_IISFCON_RXMASK	  (0x3f)
#define S3C2410_IISFCON_RXSHIFT	  (0)

#define S3C2400_IISFCON_TXDMA     (1<<11)
#define S3C2400_IISFCON_RXDMA     (1<<10)
#define S3C2400_IISFCON_TXENABLE  (1<<9)
#define S3C2400_IISFCON_RXENABLE  (1<<8)
#define S3C2400_IISFCON_TXMASK	  (0x07 << 4)
#define S3C2400_IISFCON_TXSHIFT	  (4)
#define S3C2400_IISFCON_RXMASK	  (0x07)
#define S3C2400_IISFCON_RXSHIFT	  (0)

#define S3C2410_IISFIFO  (0x10)
#endif /* __ASM_ARCH_REGS_IIS_H */
