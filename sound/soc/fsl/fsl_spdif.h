/*
 * fsl_spdif.h - ALSA S/PDIF interface for the Freescale i.MX SoC
 *
 * Copyright (C) 2013 Freescale Semiconductor, Inc.
 *
 * Author: Nicolin Chen <b42378@freescale.com>
 *
 * Based on fsl_ssi.h
 * Author: Timur Tabi <timur@freescale.com>
 * Copyright 2007-2008 Freescale Semiconductor, Inc.
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2.  This program  is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

#ifndef _FSL_SPDIF_DAI_H
#define _FSL_SPDIF_DAI_H

/* S/PDIF Register Map */
#define REG_SPDIF_SCR 			0x0	/* SPDIF Configuration Register */
#define REG_SPDIF_SRCD		 	0x4	/* CDText Control Register */
#define REG_SPDIF_SRPC			0x8	/* PhaseConfig Register */
#define REG_SPDIF_SIE			0xc	/* InterruptEn Register */
#define REG_SPDIF_SIS			0x10	/* InterruptStat Register */
#define REG_SPDIF_SIC			0x10	/* InterruptClear Register */
#define REG_SPDIF_SRL			0x14	/* SPDIFRxLeft Register */
#define REG_SPDIF_SRR			0x18	/* SPDIFRxRight Register */
#define REG_SPDIF_SRCSH			0x1c	/* SPDIFRxCChannel_h Register */
#define REG_SPDIF_SRCSL			0x20	/* SPDIFRxCChannel_l Register */
#define REG_SPDIF_SRU			0x24	/* UchannelRx Register */
#define REG_SPDIF_SRQ			0x28	/* QchannelRx Register */
#define REG_SPDIF_STL			0x2C	/* SPDIFTxLeft Register */
#define REG_SPDIF_STR			0x30	/* SPDIFTxRight Register */
#define REG_SPDIF_STCSCH		0x34	/* SPDIFTxCChannelCons_h Register */
#define REG_SPDIF_STCSCL		0x38	/* SPDIFTxCChannelCons_l Register */
#define REG_SPDIF_SRFM			0x44	/* FreqMeas Register */
#define REG_SPDIF_STC			0x50	/* SPDIFTxClk Register */


/* SPDIF Configuration register */
#define SCR_RXFIFO_CTL_OFFSET		23
#define SCR_RXFIFO_CTL_MASK		(1 << SCR_RXFIFO_CTL_OFFSET)
#define SCR_RXFIFO_CTL_ZERO		(1 << SCR_RXFIFO_CTL_OFFSET)
#define SCR_RXFIFO_OFF_OFFSET		22
#define SCR_RXFIFO_OFF_MASK		(1 << SCR_RXFIFO_OFF_OFFSET)
#define SCR_RXFIFO_OFF			(1 << SCR_RXFIFO_OFF_OFFSET)
#define SCR_RXFIFO_RST_OFFSET		21
#define SCR_RXFIFO_RST_MASK		(1 << SCR_RXFIFO_RST_OFFSET)
#define SCR_RXFIFO_RST			(1 << SCR_RXFIFO_RST_OFFSET)
#define SCR_RXFIFO_FSEL_OFFSET		19
#define SCR_RXFIFO_FSEL_MASK		(0x3 << SCR_RXFIFO_FSEL_OFFSET)
#define SCR_RXFIFO_FSEL_IF0		(0x0 << SCR_RXFIFO_FSEL_OFFSET)
#define SCR_RXFIFO_FSEL_IF4		(0x1 << SCR_RXFIFO_FSEL_OFFSET)
#define SCR_RXFIFO_FSEL_IF8		(0x2 << SCR_RXFIFO_FSEL_OFFSET)
#define SCR_RXFIFO_FSEL_IF12		(0x3 << SCR_RXFIFO_FSEL_OFFSET)
#define SCR_RXFIFO_AUTOSYNC_OFFSET	18
#define SCR_RXFIFO_AUTOSYNC_MASK	(1 << SCR_RXFIFO_AUTOSYNC_OFFSET)
#define SCR_RXFIFO_AUTOSYNC		(1 << SCR_RXFIFO_AUTOSYNC_OFFSET)
#define SCR_TXFIFO_AUTOSYNC_OFFSET	17
#define SCR_TXFIFO_AUTOSYNC_MASK	(1 << SCR_TXFIFO_AUTOSYNC_OFFSET)
#define SCR_TXFIFO_AUTOSYNC		(1 << SCR_TXFIFO_AUTOSYNC_OFFSET)
#define SCR_TXFIFO_FSEL_OFFSET		15
#define SCR_TXFIFO_FSEL_MASK		(0x3 << SCR_TXFIFO_FSEL_OFFSET)
#define SCR_TXFIFO_FSEL_IF0		(0x0 << SCR_TXFIFO_FSEL_OFFSET)
#define SCR_TXFIFO_FSEL_IF4		(0x1 << SCR_TXFIFO_FSEL_OFFSET)
#define SCR_TXFIFO_FSEL_IF8		(0x2 << SCR_TXFIFO_FSEL_OFFSET)
#define SCR_TXFIFO_FSEL_IF12		(0x3 << SCR_TXFIFO_FSEL_OFFSET)
#define SCR_LOW_POWER			(1 << 13)
#define SCR_SOFT_RESET			(1 << 12)
#define SCR_TXFIFO_CTRL_OFFSET		10
#define SCR_TXFIFO_CTRL_MASK		(0x3 << SCR_TXFIFO_CTRL_OFFSET)
#define SCR_TXFIFO_CTRL_ZERO		(0x0 << SCR_TXFIFO_CTRL_OFFSET)
#define SCR_TXFIFO_CTRL_NORMAL		(0x1 << SCR_TXFIFO_CTRL_OFFSET)
#define SCR_TXFIFO_CTRL_ONESAMPLE	(0x2 << SCR_TXFIFO_CTRL_OFFSET)
#define SCR_DMA_RX_EN_OFFSET		9
#define SCR_DMA_RX_EN_MASK		(1 << SCR_DMA_RX_EN_OFFSET)
#define SCR_DMA_RX_EN			(1 << SCR_DMA_RX_EN_OFFSET)
#define SCR_DMA_TX_EN_OFFSET		8
#define SCR_DMA_TX_EN_MASK		(1 << SCR_DMA_TX_EN_OFFSET)
#define SCR_DMA_TX_EN			(1 << SCR_DMA_TX_EN_OFFSET)
#define SCR_VAL_OFFSET			5
#define SCR_VAL_MASK			(1 << SCR_VAL_OFFSET)
#define SCR_VAL_CLEAR			(1 << SCR_VAL_OFFSET)
#define SCR_TXSEL_OFFSET		2
#define SCR_TXSEL_MASK			(0x7 << SCR_TXSEL_OFFSET)
#define SCR_TXSEL_OFF			(0 << SCR_TXSEL_OFFSET)
#define SCR_TXSEL_RX			(1 << SCR_TXSEL_OFFSET)
#define SCR_TXSEL_NORMAL		(0x5 << SCR_TXSEL_OFFSET)
#define SCR_USRC_SEL_OFFSET		0x0
#define SCR_USRC_SEL_MASK		(0x3 << SCR_USRC_SEL_OFFSET)
#define SCR_USRC_SEL_NONE		(0x0 << SCR_USRC_SEL_OFFSET)
#define SCR_USRC_SEL_RECV		(0x1 << SCR_USRC_SEL_OFFSET)
#define SCR_USRC_SEL_CHIP		(0x3 << SCR_USRC_SEL_OFFSET)

/* SPDIF CDText control */
#define SRCD_CD_USER_OFFSET		1
#define SRCD_CD_USER			(1 << SRCD_CD_USER_OFFSET)

/* SPDIF Phase Configuration register */
#define SRPC_DPLL_LOCKED		(1 << 6)
#define SRPC_CLKSRC_SEL_OFFSET		7
#define SRPC_CLKSRC_SEL_MASK		(0xf << SRPC_CLKSRC_SEL_OFFSET)
#define SRPC_CLKSRC_SEL_SET(x)		((x << SRPC_CLKSRC_SEL_OFFSET) & SRPC_CLKSRC_SEL_MASK)
#define SRPC_CLKSRC_SEL_LOCKED_OFFSET1	5
#define SRPC_CLKSRC_SEL_LOCKED_OFFSET2	2
#define SRPC_GAINSEL_OFFSET		3
#define SRPC_GAINSEL_MASK		(0x7 << SRPC_GAINSEL_OFFSET)
#define SRPC_GAINSEL_SET(x)		((x << SRPC_GAINSEL_OFFSET) & SRPC_GAINSEL_MASK)

#define SRPC_CLKSRC_MAX			16

enum spdif_gainsel {
	GAINSEL_MULTI_24 = 0,
	GAINSEL_MULTI_16,
	GAINSEL_MULTI_12,
	GAINSEL_MULTI_8,
	GAINSEL_MULTI_6,
	GAINSEL_MULTI_4,
	GAINSEL_MULTI_3,
};
#define GAINSEL_MULTI_MAX		(GAINSEL_MULTI_3 + 1)
#define SPDIF_DEFAULT_GAINSEL		GAINSEL_MULTI_8

/* SPDIF interrupt mask define */
#define INT_DPLL_LOCKED			(1 << 20)
#define INT_TXFIFO_UNOV			(1 << 19)
#define INT_TXFIFO_RESYNC		(1 << 18)
#define INT_CNEW			(1 << 17)
#define INT_VAL_NOGOOD			(1 << 16)
#define INT_SYM_ERR			(1 << 15)
#define INT_BIT_ERR			(1 << 14)
#define INT_URX_FUL			(1 << 10)
#define INT_URX_OV			(1 << 9)
#define INT_QRX_FUL			(1 << 8)
#define INT_QRX_OV			(1 << 7)
#define INT_UQ_SYNC			(1 << 6)
#define INT_UQ_ERR			(1 << 5)
#define INT_RXFIFO_UNOV			(1 << 4)
#define INT_RXFIFO_RESYNC		(1 << 3)
#define INT_LOSS_LOCK			(1 << 2)
#define INT_TX_EM			(1 << 1)
#define INT_RXFIFO_FUL			(1 << 0)

/* SPDIF Clock register */
#define STC_SYSCLK_DIV_OFFSET		11
#define STC_SYSCLK_DIV_MASK		(0x1ff << STC_TXCLK_SRC_OFFSET)
#define STC_SYSCLK_DIV(x)		((((x) - 1) << STC_TXCLK_DIV_OFFSET) & STC_SYSCLK_DIV_MASK)
#define STC_TXCLK_SRC_OFFSET		8
#define STC_TXCLK_SRC_MASK		(0x7 << STC_TXCLK_SRC_OFFSET)
#define STC_TXCLK_SRC_SET(x)		((x << STC_TXCLK_SRC_OFFSET) & STC_TXCLK_SRC_MASK)
#define STC_TXCLK_ALL_EN_OFFSET		7
#define STC_TXCLK_ALL_EN_MASK		(1 << STC_TXCLK_ALL_EN_OFFSET)
#define STC_TXCLK_ALL_EN		(1 << STC_TXCLK_ALL_EN_OFFSET)
#define STC_TXCLK_DIV_OFFSET		0
#define STC_TXCLK_DIV_MASK		(0x7ff << STC_TXCLK_DIV_OFFSET)
#define STC_TXCLK_DIV(x)		((((x) - 1) << STC_TXCLK_DIV_OFFSET) & STC_TXCLK_DIV_MASK)
#define STC_TXCLK_SRC_MAX		8

/* SPDIF tx rate */
enum spdif_txrate {
	SPDIF_TXRATE_32000 = 0,
	SPDIF_TXRATE_44100,
	SPDIF_TXRATE_48000,
};
#define SPDIF_TXRATE_MAX		(SPDIF_TXRATE_48000 + 1)


#define SPDIF_CSTATUS_BYTE		6
#define SPDIF_UBITS_SIZE		96
#define SPDIF_QSUB_SIZE			(SPDIF_UBITS_SIZE / 8)


#define FSL_SPDIF_RATES_PLAYBACK	(SNDRV_PCM_RATE_32000 |	\
					 SNDRV_PCM_RATE_44100 |	\
					 SNDRV_PCM_RATE_48000)

#define FSL_SPDIF_RATES_CAPTURE		(SNDRV_PCM_RATE_16000 | \
					 SNDRV_PCM_RATE_32000 |	\
					 SNDRV_PCM_RATE_44100 | \
					 SNDRV_PCM_RATE_48000 |	\
					 SNDRV_PCM_RATE_64000 | \
					 SNDRV_PCM_RATE_96000)

#define FSL_SPDIF_FORMATS_PLAYBACK	(SNDRV_PCM_FMTBIT_S16_LE | \
					 SNDRV_PCM_FMTBIT_S20_3LE | \
					 SNDRV_PCM_FMTBIT_S24_LE)

#define FSL_SPDIF_FORMATS_CAPTURE	(SNDRV_PCM_FMTBIT_S24_LE)

#endif /* _FSL_SPDIF_DAI_H */
