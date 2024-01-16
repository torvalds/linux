/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2003 Simtec Electronics <linux@simtec.co.uk>
 *		      http://www.simtec.co.uk/products/SWLINUX/
 *
 * S3C2410 IIS register definition
 */

#ifndef __SAMSUNG_REGS_IIS_H__
#define __SAMSUNG_REGS_IIS_H__

#define S3C2410_IISCON			(0x00)

#define S3C2410_IISCON_LRINDEX		(1 << 8)
#define S3C2410_IISCON_TXFIFORDY	(1 << 7)
#define S3C2410_IISCON_RXFIFORDY	(1 << 6)
#define S3C2410_IISCON_TXDMAEN		(1 << 5)
#define S3C2410_IISCON_RXDMAEN		(1 << 4)
#define S3C2410_IISCON_TXIDLE		(1 << 3)
#define S3C2410_IISCON_RXIDLE		(1 << 2)
#define S3C2410_IISCON_PSCEN		(1 << 1)
#define S3C2410_IISCON_IISEN		(1 << 0)

#define S3C2410_IISMOD			(0x04)

#define S3C2440_IISMOD_MPLL		(1 << 9)
#define S3C2410_IISMOD_SLAVE		(1 << 8)
#define S3C2410_IISMOD_NOXFER		(0 << 6)
#define S3C2410_IISMOD_RXMODE		(1 << 6)
#define S3C2410_IISMOD_TXMODE		(2 << 6)
#define S3C2410_IISMOD_TXRXMODE		(3 << 6)
#define S3C2410_IISMOD_LR_LLOW		(0 << 5)
#define S3C2410_IISMOD_LR_RLOW		(1 << 5)
#define S3C2410_IISMOD_IIS		(0 << 4)
#define S3C2410_IISMOD_MSB		(1 << 4)
#define S3C2410_IISMOD_8BIT		(0 << 3)
#define S3C2410_IISMOD_16BIT		(1 << 3)
#define S3C2410_IISMOD_BITMASK		(1 << 3)
#define S3C2410_IISMOD_256FS		(0 << 2)
#define S3C2410_IISMOD_384FS		(1 << 2)
#define S3C2410_IISMOD_16FS		(0 << 0)
#define S3C2410_IISMOD_32FS		(1 << 0)
#define S3C2410_IISMOD_48FS		(2 << 0)
#define S3C2410_IISMOD_FS_MASK		(3 << 0)

#define S3C2410_IISPSR			(0x08)

#define S3C2410_IISPSR_INTMASK		(31 << 5)
#define S3C2410_IISPSR_INTSHIFT		(5)
#define S3C2410_IISPSR_EXTMASK		(31 << 0)
#define S3C2410_IISPSR_EXTSHFIT		(0)

#define S3C2410_IISFCON			(0x0c)

#define S3C2410_IISFCON_TXDMA		(1 << 15)
#define S3C2410_IISFCON_RXDMA		(1 << 14)
#define S3C2410_IISFCON_TXENABLE	(1 << 13)
#define S3C2410_IISFCON_RXENABLE	(1 << 12)
#define S3C2410_IISFCON_TXMASK		(0x3f << 6)
#define S3C2410_IISFCON_TXSHIFT		(6)
#define S3C2410_IISFCON_RXMASK		(0x3f)
#define S3C2410_IISFCON_RXSHIFT		(0)

#define S3C2410_IISFIFO			(0x10)

#endif /* __SAMSUNG_REGS_IIS_H__ */
