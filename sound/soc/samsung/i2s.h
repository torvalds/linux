/* sound/soc/samsung/i2s.h
 *
 * ALSA SoC Audio Layer - Samsung I2S Controller driver
 *
 * Copyright (c) 2010 Samsung Electronics Co. Ltd.
 *	Jaswinder Singh <jassi.brar@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __SND_SOC_SAMSUNG_I2S_H
#define __SND_SOC_SAMSUNG_I2S_H

/*
 * Maximum number of I2S blocks that any SoC can have.
 * The secondary interface of a CPU dai(if there exists any),
 * is indexed at [cpu-dai's ID + SAMSUNG_I2S_SECOFF]
 */
#define SAMSUNG_I2S_SECOFF	4

#define SAMSUNG_I2S_DIV_BCLK	1

#define SAMSUNG_I2S_RCLKSRC_0	0
#define SAMSUNG_I2S_RCLKSRC_1	1
#define SAMSUNG_I2S_CDCLK	2
#define SAMSUNG_I2S_OPCLK	3

#define I2SCON		0x0
#define I2SMOD		0x4
#define I2SFIC		0x8
#define I2SPSR		0xc
#define I2STXD		0x10
#define I2SRXD		0x14
#define I2SFICS		0x18
#define I2STXDS		0x1c

#define CON_RSTCLR		(1 << 31)
#define CON_FRXOFSTATUS		(1 << 26)	/* ver5: i2s0 */
#define CON_FRXOFINTEN		(1 << 25)	/* ver5: i2s0 */
#define CON_FRXOFSTATUS_L	(1 << 19)	/* ver2: i2s1, i2s2 */
#define CON_FRXOFINTEN_L	(1 << 18)	/* ver2: i2s1, i2s2 */

/* Secondary TX FIFO only */
#define CON_FTXSURSTAT		(1 << 24)
#define CON_FTXSURINTEN		(1 << 23)
#define CON_TXSFIFO_EMPTY       (1 << 22)
#define CON_TXSFIFO_FULL        (1 << 21)
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

#define MOD_BLCS_SHIFT	26
#define MOD_BLCS_16BIT	(0 << MOD_BLCS_SHIFT)
#define MOD_BLCS_8BIT	(1 << MOD_BLCS_SHIFT)
#define MOD_BLCS_24BIT	(2 << MOD_BLCS_SHIFT)
#define MOD_BLCS_MASK	(3 << MOD_BLCS_SHIFT)
#define MOD_BLCP_SHIFT	24
#define MOD_BLCP_16BIT	(0 << MOD_BLCP_SHIFT)
#define MOD_BLCP_8BIT	(1 << MOD_BLCP_SHIFT)
#define MOD_BLCP_24BIT	(2 << MOD_BLCP_SHIFT)
#define MOD_BLCP_MASK	(3 << MOD_BLCP_SHIFT)

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

#define MOD_RCLK_I2SCLK	(1<<10)
#define MOD_RCLK_PCLK		(0<<10)
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

#define MOD_CDCLK_IN		(1<<12)
#define MOD_CDCLK_OUT		(0<<12)
#define MOD_CDCLKCON		(1 << 12)

#define PSR_PSREN		(1 << 15)

#define FIC_TX2COUNT(x)		(((x) >>  24) & 0xf)
#define FIC_TX1COUNT(x)		(((x) >>  16) & 0xf)

#define FIC_TXFLUSH		(1 << 15)
#define FIC_RXFLUSH		(1 << 7)
#define FIC_TXCOUNT(x)		(((x) >>  8) & 0xf)
#define FIC_RXCOUNT(x)		(((x) >>  0) & 0xf)
#define FICS_TXCOUNT(x)		(((x) >>  8) & 0x7f)

#define msecs_to_loops(t) (loops_per_jiffy / 1000 * HZ * t)

#endif /* __SND_SOC_SAMSUNG_I2S_H */
