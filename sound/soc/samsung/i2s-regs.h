/*
 * linux/sound/soc/samsung/i2s-regs.h
 *
 * Copyright (c) 2011 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Samsung I2S driver's register header
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifndef __SND_SOC_SAMSUNG_I2S_REGS_H
#define __SND_SOC_SAMSUNG_I2S_REGS_H

#define I2SCON		0x0
#define I2SMOD		0x4
#define I2SFIC		0x8
#define I2SPSR		0xc
#define I2STXD		0x10
#define I2SRXD		0x14
#define I2SFICS		0x18
#define I2STXDS		0x1c
#define I2SAHB		0x20
#define I2SSTR0		0x24
#define I2SSIZE		0x28
#define I2STRNCNT	0x2c
#define I2SLVL0ADDR	0x30
#define I2SLVL1ADDR	0x34
#define I2SLVL2ADDR	0x38
#define I2SLVL3ADDR	0x3c

#define CON_RSTCLR		(1 << 31)
#define CON_FRXOFSTATUS		(1 << 26)
#define CON_FRXORINTEN		(1 << 25)
#define CON_FTXSURSTAT		(1 << 24)
#define CON_FTXSURINTEN		(1 << 23)
#define CON_TXSDMA_PAUSE	(1 << 20)
#define CON_TXSDMA_ACTIVE	(1 << 18)

#define CON_FTXURSTATUS		(1 << 17)
#define CON_FTXURINTEN		(1 << 16)
#define CON_TXFIFO2_EMPTY	(1 << 15)
#define CON_TXFIFO1_EMPTY	(1 << 14)
#define CON_TXFIFO2_FULL	(1 << 13)
#define CON_TXFIFO1_FULL	(1 << 12)

#define CON_LRINDEX		(1 << 11)
#define CON_TXFIFO_EMPTY	(1 << 10)
#define CON_RXFIFO_EMPTY	(1 << 9)
#define CON_TXFIFO_FULL		(1 << 8)
#define CON_RXFIFO_FULL		(1 << 7)
#define CON_TXDMA_PAUSE		(1 << 6)
#define CON_RXDMA_PAUSE		(1 << 5)
#define CON_TXCH_PAUSE		(1 << 4)
#define CON_RXCH_PAUSE		(1 << 3)
#define CON_TXDMA_ACTIVE	(1 << 2)
#define CON_RXDMA_ACTIVE	(1 << 1)
#define CON_ACTIVE		(1 << 0)

#define MOD_OPCLK_CDCLK_OUT	(0 << 30)
#define MOD_OPCLK_CDCLK_IN	(1 << 30)
#define MOD_OPCLK_BCLK_OUT	(2 << 30)
#define MOD_OPCLK_PCLK		(3 << 30)
#define MOD_OPCLK_MASK		(3 << 30)
#define MOD_TXS_IDMA		(1 << 28) /* Sec_TXFIFO use I-DMA */

#define MOD_BLCS_SHIFT		26
#define MOD_BLCS_16BIT		(0 << MOD_BLCS_SHIFT)
#define MOD_BLCS_8BIT		(1 << MOD_BLCS_SHIFT)
#define MOD_BLCS_24BIT		(2 << MOD_BLCS_SHIFT)
#define MOD_BLCS_MASK		(3 << MOD_BLCS_SHIFT)
#define MOD_BLCP_SHIFT		24
#define MOD_BLCP_16BIT		(0 << MOD_BLCP_SHIFT)
#define MOD_BLCP_8BIT		(1 << MOD_BLCP_SHIFT)
#define MOD_BLCP_24BIT		(2 << MOD_BLCP_SHIFT)
#define MOD_BLCP_MASK		(3 << MOD_BLCP_SHIFT)

#define MOD_C2DD_HHALF		(1 << 21) /* Discard Higher-half */
#define MOD_C2DD_LHALF		(1 << 20) /* Discard Lower-half */
#define MOD_C1DD_HHALF		(1 << 19)
#define MOD_C1DD_LHALF		(1 << 18)
#define MOD_DC2_EN		(1 << 17)
#define MOD_DC1_EN		(1 << 16)
#define MOD_BLC_16BIT		(0 << 13)
#define MOD_BLC_8BIT		(1 << 13)
#define MOD_BLC_24BIT		(2 << 13)
#define MOD_BLC_MASK		(3 << 13)

#define MOD_IMS_SYSMUX		(1 << 10)
#define MOD_SLAVE		(1 << 11)
#define MOD_TXONLY		(0 << 8)
#define MOD_RXONLY		(1 << 8)
#define MOD_TXRX		(2 << 8)
#define MOD_MASK		(3 << 8)
#define MOD_LR_LLOW		(0 << 7)
#define MOD_LR_RLOW		(1 << 7)
#define MOD_SDF_IIS		(0 << 5)
#define MOD_SDF_MSB		(1 << 5)
#define MOD_SDF_LSB		(2 << 5)
#define MOD_SDF_MASK		(3 << 5)
#define MOD_RCLK_256FS		(0 << 3)
#define MOD_RCLK_512FS		(1 << 3)
#define MOD_RCLK_384FS		(2 << 3)
#define MOD_RCLK_768FS		(3 << 3)
#define MOD_RCLK_MASK		(3 << 3)
#define MOD_BCLK_32FS		(0 << 1)
#define MOD_BCLK_48FS		(1 << 1)
#define MOD_BCLK_16FS		(2 << 1)
#define MOD_BCLK_24FS		(3 << 1)
#define MOD_BCLK_MASK		(3 << 1)
#define MOD_8BIT		(1 << 0)

#define MOD_CDCLKCON		(1 << 12)

#define PSR_PSREN		(1 << 15)

#define FIC_TX2COUNT(x)		(((x) >>  24) & 0xf)
#define FIC_TX1COUNT(x)		(((x) >>  16) & 0xf)

#define FIC_TXFLUSH		(1 << 15)
#define FIC_RXFLUSH		(1 << 7)

#define FIC_TXCOUNT(x)		(((x) >>  8) & 0xf)
#define FIC_RXCOUNT(x)		(((x) >>  0) & 0xf)
#define FICS_TXCOUNT(x)		(((x) >>  8) & 0x7f)

#define AHB_INTENLVL0		(1 << 24)
#define AHB_LVL0INT		(1 << 20)
#define AHB_CLRLVL0INT		(1 << 16)
#define AHB_DMARLD		(1 << 5)
#define AHB_INTMASK		(1 << 3)
#define AHB_DMAEN		(1 << 0)
#define AHB_LVLINTMASK		(0xf << 20)

#define I2SSIZE_TRNMSK		(0xffff)
#define I2SSIZE_SHIFT		(16)

#endif /* __SND_SOC_SAMSUNG_I2S_REGS_H */


