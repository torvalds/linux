/* linux/include/asm-arm/plat-s3c24xx/regs-s3c2412-iis.h
 *
 * Copyright 2007 Simtec Electronics <linux@simtec.co.uk>
 *	http://armlinux.simtec.co.uk/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * S3C2412 IIS register definition
*/

#ifndef __ASM_ARCH_REGS_S3C2412_IIS_H
#define __ASM_ARCH_REGS_S3C2412_IIS_H

#define S3C2412_IISCON			(0x00)
#define S3C2412_IISMOD			(0x04)
#define S3C2412_IISFIC			(0x08)
#define S3C2412_IISPSR			(0x0C)
#define S3C2412_IISTXD			(0x10)
#define S3C2412_IISRXD			(0x14)

#define S3C2412_IISCON_LRINDEX		(1 << 11)
#define S3C2412_IISCON_TXFIFO_EMPTY	(1 << 10)
#define S3C2412_IISCON_RXFIFO_EMPTY	(1 << 9)
#define S3C2412_IISCON_TXFIFO_FULL	(1 << 8)
#define S3C2412_IISCON_RXFIFO_FULL	(1 << 7)
#define S3C2412_IISCON_TXDMA_PAUSE	(1 << 6)
#define S3C2412_IISCON_RXDMA_PAUSE	(1 << 5)
#define S3C2412_IISCON_TXCH_PAUSE	(1 << 4)
#define S3C2412_IISCON_RXCH_PAUSE	(1 << 3)
#define S3C2412_IISCON_TXDMA_ACTIVE	(1 << 2)
#define S3C2412_IISCON_RXDMA_ACTIVE	(1 << 1)
#define S3C2412_IISCON_IIS_ACTIVE	(1 << 0)

#define S3C2412_IISMOD_MASTER_INTERNAL	(0 << 10)
#define S3C2412_IISMOD_MASTER_EXTERNAL	(1 << 10)
#define S3C2412_IISMOD_SLAVE		(2 << 10)
#define S3C2412_IISMOD_MASTER_MASK	(3 << 10)
#define S3C2412_IISMOD_MODE_TXONLY	(0 << 8)
#define S3C2412_IISMOD_MODE_RXONLY	(1 << 8)
#define S3C2412_IISMOD_MODE_TXRX	(2 << 8)
#define S3C2412_IISMOD_MODE_MASK	(3 << 8)
#define S3C2412_IISMOD_LR_LLOW		(0 << 7)
#define S3C2412_IISMOD_LR_RLOW		(1 << 7)
#define S3C2412_IISMOD_SDF_IIS		(0 << 5)
#define S3C2412_IISMOD_SDF_MSB		(0 << 5)
#define S3C2412_IISMOD_SDF_LSB		(0 << 5)
#define S3C2412_IISMOD_SDF_MASK		(3 << 5)
#define S3C2412_IISMOD_RCLK_256FS	(0 << 3)
#define S3C2412_IISMOD_RCLK_512FS	(1 << 3)
#define S3C2412_IISMOD_RCLK_384FS	(2 << 3)
#define S3C2412_IISMOD_RCLK_768FS	(3 << 3)
#define S3C2412_IISMOD_RCLK_MASK 	(3 << 3)
#define S3C2412_IISMOD_BCLK_32FS	(0 << 1)
#define S3C2412_IISMOD_BCLK_48FS	(1 << 1)
#define S3C2412_IISMOD_BCLK_16FS	(2 << 1)
#define S3C2412_IISMOD_BCLK_24FS	(3 << 1)
#define S3C2412_IISMOD_BCLK_MASK	(3 << 1)
#define S3C2412_IISMOD_8BIT		(1 << 0)

#define S3C2412_IISPSR_PSREN		(1 << 15)

#define S3C2412_IISFIC_TXFLUSH		(1 << 15)
#define S3C2412_IISFIC_RXFLUSH		(1 << 7)
#define S3C2412_IISFIC_TXCOUNT(x)	(((x) >>  8) & 0xf)
#define S3C2412_IISFIC_RXCOUNT(x)	(((x) >>  0) & 0xf)



#endif /* __ASM_ARCH_REGS_S3C2412_IIS_H */

