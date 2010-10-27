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

#define S5PC1XX_IISFICS		0x18
#define S5PC1XX_IISTXDS		0x1C

#define S5PC1XX_IISCON_SW_RST		(1 << 31)
#define S5PC1XX_IISCON_FRXOFSTATUS	(1 << 26)
#define S5PC1XX_IISCON_FRXORINTEN	(1 << 25)
#define S5PC1XX_IISCON_FTXSURSTAT	(1 << 24)
#define S5PC1XX_IISCON_FTXSURINTEN	(1 << 23)
#define S5PC1XX_IISCON_TXSDMAPAUSE	(1 << 20)
#define S5PC1XX_IISCON_TXSDMACTIVE	(1 << 18)

#define S3C64XX_IISCON_FTXURSTATUS	(1 << 17)
#define S3C64XX_IISCON_FTXURINTEN	(1 << 16)
#define S3C64XX_IISCON_TXFIFO2_EMPTY	(1 << 15)
#define S3C64XX_IISCON_TXFIFO1_EMPTY	(1 << 14)
#define S3C64XX_IISCON_TXFIFO2_FULL	(1 << 13)
#define S3C64XX_IISCON_TXFIFO1_FULL	(1 << 12)

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

#define S5PC1XX_IISMOD_OPCLK_CDCLK_OUT	(0 << 30)
#define S5PC1XX_IISMOD_OPCLK_CDCLK_IN	(1 << 30)
#define S5PC1XX_IISMOD_OPCLK_BCLK_OUT	(2 << 30)
#define S5PC1XX_IISMOD_OPCLK_PCLK	(3 << 30)
#define S5PC1XX_IISMOD_OPCLK_MASK	(3 << 30)
#define S5PC1XX_IISMOD_TXS_IDMA		(1 << 28) /* Sec_TXFIFO use I-DMA */
#define S5PC1XX_IISMOD_BLCS_MASK	0x3
#define S5PC1XX_IISMOD_BLCS_SHIFT	26
#define S5PC1XX_IISMOD_BLCP_MASK	0x3
#define S5PC1XX_IISMOD_BLCP_SHIFT	24

#define S3C64XX_IISMOD_C2DD_HHALF	(1 << 21) /* Discard Higher-half */
#define S3C64XX_IISMOD_C2DD_LHALF	(1 << 20) /* Discard Lower-half */
#define S3C64XX_IISMOD_C1DD_HHALF	(1 << 19)
#define S3C64XX_IISMOD_C1DD_LHALF	(1 << 18)
#define S3C64XX_IISMOD_DC2_EN		(1 << 17)
#define S3C64XX_IISMOD_DC1_EN		(1 << 16)
#define S3C64XX_IISMOD_BLC_16BIT	(0 << 13)
#define S3C64XX_IISMOD_BLC_8BIT		(1 << 13)
#define S3C64XX_IISMOD_BLC_24BIT	(2 << 13)
#define S3C64XX_IISMOD_BLC_MASK		(3 << 13)

#define S3C2412_IISMOD_IMS_SYSMUX	(1 << 10)
#define S3C2412_IISMOD_SLAVE		(1 << 11)
#define S3C2412_IISMOD_MODE_TXONLY	(0 << 8)
#define S3C2412_IISMOD_MODE_RXONLY	(1 << 8)
#define S3C2412_IISMOD_MODE_TXRX	(2 << 8)
#define S3C2412_IISMOD_MODE_MASK	(3 << 8)
#define S3C2412_IISMOD_LR_LLOW		(0 << 7)
#define S3C2412_IISMOD_LR_RLOW		(1 << 7)
#define S3C2412_IISMOD_SDF_IIS		(0 << 5)
#define S3C2412_IISMOD_SDF_MSB		(1 << 5)
#define S3C2412_IISMOD_SDF_LSB		(2 << 5)
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

#define S3C64XX_IISMOD_CDCLKCON		(1 << 12)

#define S3C2412_IISPSR_PSREN		(1 << 15)

#define S3C64XX_IISFIC_TX2COUNT(x)	(((x) >>  24) & 0xf)
#define S3C64XX_IISFIC_TX1COUNT(x)	(((x) >>  16) & 0xf)

#define S3C2412_IISFIC_TXFLUSH		(1 << 15)
#define S3C2412_IISFIC_RXFLUSH		(1 << 7)
#define S3C2412_IISFIC_TXCOUNT(x)	(((x) >>  8) & 0xf)
#define S3C2412_IISFIC_RXCOUNT(x)	(((x) >>  0) & 0xf)

#define S5PC1XX_IISFICS_TXFLUSH		(1 << 15)
#define S5PC1XX_IISFICS_TXCOUNT(x)	(((x) >>  8) & 0x7f)

#endif /* __ASM_ARCH_REGS_S3C2412_IIS_H */
