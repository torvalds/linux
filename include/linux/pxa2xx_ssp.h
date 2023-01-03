/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2003 Russell King, All Rights Reserved.
 *
 * This driver supports the following PXA CPU/SSP ports:-
 *
 *       PXA250     SSP
 *       PXA255     SSP, NSSP
 *       PXA26x     SSP, NSSP, ASSP
 *       PXA27x     SSP1, SSP2, SSP3
 *       PXA3xx     SSP1, SSP2, SSP3, SSP4
 */

#ifndef __LINUX_PXA2XX_SSP_H
#define __LINUX_PXA2XX_SSP_H

#include <linux/bits.h>
#include <linux/compiler_types.h>
#include <linux/io.h>
#include <linux/kconfig.h>
#include <linux/list.h>
#include <linux/types.h>

struct clk;
struct device;
struct device_node;

/*
 * SSP Serial Port Registers
 * PXA250, PXA255, PXA26x and PXA27x SSP controllers are all slightly different.
 * PXA255, PXA26x and PXA27x have extra ports, registers and bits.
 */

#define SSCR0		(0x00)  /* SSP Control Register 0 */
#define SSCR1		(0x04)  /* SSP Control Register 1 */
#define SSSR		(0x08)  /* SSP Status Register */
#define SSITR		(0x0C)  /* SSP Interrupt Test Register */
#define SSDR		(0x10)  /* SSP Data Write/Data Read Register */

#define SSTO		(0x28)  /* SSP Time Out Register */
#define SSPSP		(0x2C)  /* SSP Programmable Serial Protocol */
#define SSTSA		(0x30)  /* SSP Tx Timeslot Active */
#define SSRSA		(0x34)  /* SSP Rx Timeslot Active */
#define SSTSS		(0x38)  /* SSP Timeslot Status */
#define SSACD		(0x3C)  /* SSP Audio Clock Divider */
#define SSACDD		(0x40)	/* SSP Audio Clock Dither Divider */

/* Common PXA2xx bits first */
#define SSCR0_DSS	GENMASK(3, 0)	/* Data Size Select (mask) */
#define SSCR0_DataSize(x)  ((x) - 1)	/* Data Size Select [4..16] */
#define SSCR0_FRF	GENMASK(5, 4)	/* FRame Format (mask) */
#define SSCR0_Motorola	(0x0 << 4)	/* Motorola's Serial Peripheral Interface (SPI) */
#define SSCR0_TI	(0x1 << 4)	/* Texas Instruments' Synchronous Serial Protocol (SSP) */
#define SSCR0_National	(0x2 << 4)	/* National Microwire */
#define SSCR0_ECS	BIT(6)		/* External clock select */
#define SSCR0_SSE	BIT(7)		/* Synchronous Serial Port Enable */
#define SSCR0_SCR(x)	((x) << 8)	/* Serial Clock Rate (mask) */

/* PXA27x, PXA3xx */
#define SSCR0_EDSS	BIT(20)		/* Extended data size select */
#define SSCR0_NCS	BIT(21)		/* Network clock select */
#define SSCR0_RIM	BIT(22)		/* Receive FIFO overrun interrupt mask */
#define SSCR0_TUM	BIT(23)		/* Transmit FIFO underrun interrupt mask */
#define SSCR0_FRDC	GENMASK(26, 24)	/* Frame rate divider control (mask) */
#define SSCR0_SlotsPerFrm(x) (((x) - 1) << 24)	/* Time slots per frame [1..8] */
#define SSCR0_FPCKE	BIT(29)		/* FIFO packing enable */
#define SSCR0_ACS	BIT(30)		/* Audio clock select */
#define SSCR0_MOD	BIT(31)		/* Mode (normal or network) */

#define SSCR1_RIE	BIT(0)		/* Receive FIFO Interrupt Enable */
#define SSCR1_TIE	BIT(1)		/* Transmit FIFO Interrupt Enable */
#define SSCR1_LBM	BIT(2)		/* Loop-Back Mode */
#define SSCR1_SPO	BIT(3)		/* Motorola SPI SSPSCLK polarity setting */
#define SSCR1_SPH	BIT(4)		/* Motorola SPI SSPSCLK phase setting */
#define SSCR1_MWDS	BIT(5)		/* Microwire Transmit Data Size */

#define SSSR_ALT_FRM_MASK	GENMASK(1, 0)	/* Masks the SFRM signal number */
#define SSSR_TNF		BIT(2)		/* Transmit FIFO Not Full */
#define SSSR_RNE		BIT(3)		/* Receive FIFO Not Empty */
#define SSSR_BSY		BIT(4)		/* SSP Busy */
#define SSSR_TFS		BIT(5)		/* Transmit FIFO Service Request */
#define SSSR_RFS		BIT(6)		/* Receive FIFO Service Request */
#define SSSR_ROR		BIT(7)		/* Receive FIFO Overrun */

#define RX_THRESH_DFLT	8
#define TX_THRESH_DFLT	8

#define SSSR_TFL_MASK	GENMASK(11, 8)	/* Transmit FIFO Level mask */
#define SSSR_RFL_MASK	GENMASK(15, 12)	/* Receive FIFO Level mask */

#define SSCR1_TFT	GENMASK(9, 6)	/* Transmit FIFO Threshold (mask) */
#define SSCR1_TxTresh(x) (((x) - 1) << 6)	/* level [1..16] */
#define SSCR1_RFT	GENMASK(13, 10)	/* Receive FIFO Threshold (mask) */
#define SSCR1_RxTresh(x) (((x) - 1) << 10)	/* level [1..16] */

#define RX_THRESH_CE4100_DFLT	2
#define TX_THRESH_CE4100_DFLT	2

#define CE4100_SSSR_TFL_MASK	GENMASK(9, 8)	/* Transmit FIFO Level mask */
#define CE4100_SSSR_RFL_MASK	GENMASK(13, 12)	/* Receive FIFO Level mask */

#define CE4100_SSCR1_TFT	GENMASK(7, 6)	/* Transmit FIFO Threshold (mask) */
#define CE4100_SSCR1_TxTresh(x) (((x) - 1) << 6)	/* level [1..4] */
#define CE4100_SSCR1_RFT	GENMASK(11, 10)	/* Receive FIFO Threshold (mask) */
#define CE4100_SSCR1_RxTresh(x) (((x) - 1) << 10)	/* level [1..4] */

/* Intel Quark X1000 */
#define DDS_RATE		0x28		 /* SSP DDS Clock Rate Register */

/* QUARK_X1000 SSCR0 bit definition */
#define QUARK_X1000_SSCR0_DSS		GENMASK(4, 0)	/* Data Size Select (mask) */
#define QUARK_X1000_SSCR0_DataSize(x)	((x) - 1)	/* Data Size Select [4..32] */
#define QUARK_X1000_SSCR0_FRF		GENMASK(6, 5)	/* FRame Format (mask) */
#define QUARK_X1000_SSCR0_Motorola	(0x0 << 5)	/* Motorola's Serial Peripheral Interface (SPI) */

#define RX_THRESH_QUARK_X1000_DFLT	1
#define TX_THRESH_QUARK_X1000_DFLT	16

#define QUARK_X1000_SSSR_TFL_MASK	GENMASK(12, 8)	/* Transmit FIFO Level mask */
#define QUARK_X1000_SSSR_RFL_MASK	GENMASK(17, 13)	/* Receive FIFO Level mask */

#define QUARK_X1000_SSCR1_TFT	GENMASK(10, 6)	/* Transmit FIFO Threshold (mask) */
#define QUARK_X1000_SSCR1_TxTresh(x) (((x) - 1) << 6)	/* level [1..32] */
#define QUARK_X1000_SSCR1_RFT	GENMASK(15, 11)	/* Receive FIFO Threshold (mask) */
#define QUARK_X1000_SSCR1_RxTresh(x) (((x) - 1) << 11)	/* level [1..32] */
#define QUARK_X1000_SSCR1_EFWR	BIT(16)		/* Enable FIFO Write/Read */
#define QUARK_X1000_SSCR1_STRF	BIT(17)		/* Select FIFO or EFWR */

/* Extra bits in PXA255, PXA26x and PXA27x SSP ports */
#define SSCR0_TISSP		(1 << 4)	/* TI Sync Serial Protocol */
#define SSCR0_PSP		(3 << 4)	/* PSP - Programmable Serial Protocol */

#define SSCR1_EFWR		BIT(14)		/* Enable FIFO Write/Read */
#define SSCR1_STRF		BIT(15)		/* Select FIFO or EFWR */
#define SSCR1_IFS		BIT(16)		/* Invert Frame Signal */
#define SSCR1_PINTE		BIT(18)		/* Peripheral Trailing Byte Interrupt Enable */
#define SSCR1_TINTE		BIT(19)		/* Receiver Time-out Interrupt enable */
#define SSCR1_RSRE		BIT(20)		/* Receive Service Request Enable */
#define SSCR1_TSRE		BIT(21)		/* Transmit Service Request Enable */
#define SSCR1_TRAIL		BIT(22)		/* Trailing Byte */
#define SSCR1_RWOT		BIT(23)		/* Receive Without Transmit */
#define SSCR1_SFRMDIR		BIT(24)		/* Frame Direction */
#define SSCR1_SCLKDIR		BIT(25)		/* Serial Bit Rate Clock Direction */
#define SSCR1_ECRB		BIT(26)		/* Enable Clock request B */
#define SSCR1_ECRA		BIT(27)		/* Enable Clock Request A */
#define SSCR1_SCFR		BIT(28)		/* Slave Clock free Running */
#define SSCR1_EBCEI		BIT(29)		/* Enable Bit Count Error interrupt */
#define SSCR1_TTE		BIT(30)		/* TXD Tristate Enable */
#define SSCR1_TTELP		BIT(31)		/* TXD Tristate Enable Last Phase */

#define SSSR_PINT		BIT(18)		/* Peripheral Trailing Byte Interrupt */
#define SSSR_TINT		BIT(19)		/* Receiver Time-out Interrupt */
#define SSSR_EOC		BIT(20)		/* End Of Chain */
#define SSSR_TUR		BIT(21)		/* Transmit FIFO Under Run */
#define SSSR_CSS		BIT(22)		/* Clock Synchronisation Status */
#define SSSR_BCE		BIT(23)		/* Bit Count Error */

#define SSPSP_SCMODE(x)		((x) << 0)	/* Serial Bit Rate Clock Mode */
#define SSPSP_SFRMP		BIT(2)		/* Serial Frame Polarity */
#define SSPSP_ETDS		BIT(3)		/* End of Transfer data State */
#define SSPSP_STRTDLY(x)	((x) << 4)	/* Start Delay */
#define SSPSP_DMYSTRT(x)	((x) << 7)	/* Dummy Start */
#define SSPSP_SFRMDLY(x)	((x) << 9)	/* Serial Frame Delay */
#define SSPSP_SFRMWDTH(x)	((x) << 16)	/* Serial Frame Width */
#define SSPSP_DMYSTOP(x)	((x) << 23)	/* Dummy Stop */
#define SSPSP_FSRT		BIT(25)		/* Frame Sync Relative Timing */

/* PXA3xx */
#define SSPSP_EDMYSTRT(x)	((x) << 26)     /* Extended Dummy Start */
#define SSPSP_EDMYSTOP(x)	((x) << 28)     /* Extended Dummy Stop */
#define SSPSP_TIMING_MASK	(0x7f8001f0)

#define SSACD_ACDS(x)		((x) << 0)	/* Audio clock divider select */
#define SSACD_ACDS_1		(0)
#define SSACD_ACDS_2		(1)
#define SSACD_ACDS_4		(2)
#define SSACD_ACDS_8		(3)
#define SSACD_ACDS_16		(4)
#define SSACD_ACDS_32		(5)
#define SSACD_SCDB		BIT(3)		/* SSPSYSCLK Divider Bypass */
#define SSACD_SCDB_4X		(0)
#define SSACD_SCDB_1X		(1)
#define SSACD_ACPS(x)		((x) << 4)	/* Audio clock PLL select */
#define SSACD_SCDX8		BIT(7)		/* SYSCLK division ratio select */

/* Intel Merrifield SSP */
#define SFIFOL			0x68		/* FIFO level */
#define SFIFOTT			0x6c		/* FIFO trigger threshold */

#define RX_THRESH_MRFLD_DFLT	16
#define TX_THRESH_MRFLD_DFLT	16

#define SFIFOL_TFL_MASK		GENMASK(15, 0)	/* Transmit FIFO Level mask */
#define SFIFOL_RFL_MASK		GENMASK(31, 16)	/* Receive FIFO Level mask */

#define SFIFOTT_TFT		GENMASK(15, 0)	/* Transmit FIFO Threshold (mask) */
#define SFIFOTT_TxThresh(x)	(((x) - 1) << 0)	/* TX FIFO trigger threshold / level */
#define SFIFOTT_RFT		GENMASK(31, 16)	/* Receive FIFO Threshold (mask) */
#define SFIFOTT_RxThresh(x)	(((x) - 1) << 16)	/* RX FIFO trigger threshold / level */

/* LPSS SSP */
#define SSITF			0x44		/* TX FIFO trigger level */
#define SSITF_TxHiThresh(x)	(((x) - 1) << 0)
#define SSITF_TxLoThresh(x)	(((x) - 1) << 8)

#define SSIRF			0x48		/* RX FIFO trigger level */
#define SSIRF_RxThresh(x)	((x) - 1)

/* LPT/WPT SSP */
#define SSCR2		(0x40)	/* SSP Command / Status 2 */
#define SSPSP2		(0x44)	/* SSP Programmable Serial Protocol 2 */

enum pxa_ssp_type {
	SSP_UNDEFINED = 0,
	PXA25x_SSP,  /* pxa 210, 250, 255, 26x */
	PXA25x_NSSP, /* pxa 255, 26x (including ASSP) */
	PXA27x_SSP,
	PXA3xx_SSP,
	PXA168_SSP,
	MMP2_SSP,
	PXA910_SSP,
	CE4100_SSP,
	MRFLD_SSP,
	QUARK_X1000_SSP,
	/* Keep LPSS types sorted with lpss_platforms[] */
	LPSS_LPT_SSP,
	LPSS_BYT_SSP,
	LPSS_BSW_SSP,
	LPSS_SPT_SSP,
	LPSS_BXT_SSP,
	LPSS_CNL_SSP,
	SSP_MAX
};

struct ssp_device {
	struct device	*dev;
	struct list_head	node;

	struct clk	*clk;
	void __iomem	*mmio_base;
	unsigned long	phys_base;

	const char	*label;
	int		port_id;
	enum pxa_ssp_type type;
	int		use_count;
	int		irq;

	struct device_node	*of_node;
};

/**
 * pxa_ssp_write_reg - Write to a SSP register
 *
 * @dev: SSP device to access
 * @reg: Register to write to
 * @val: Value to be written.
 */
static inline void pxa_ssp_write_reg(struct ssp_device *dev, u32 reg, u32 val)
{
	__raw_writel(val, dev->mmio_base + reg);
}

/**
 * pxa_ssp_read_reg - Read from a SSP register
 *
 * @dev: SSP device to access
 * @reg: Register to read from
 */
static inline u32 pxa_ssp_read_reg(struct ssp_device *dev, u32 reg)
{
	return __raw_readl(dev->mmio_base + reg);
}

static inline void pxa_ssp_enable(struct ssp_device *ssp)
{
	u32 sscr0;

	sscr0 = pxa_ssp_read_reg(ssp, SSCR0) | SSCR0_SSE;
	pxa_ssp_write_reg(ssp, SSCR0, sscr0);
}

static inline void pxa_ssp_disable(struct ssp_device *ssp)
{
	u32 sscr0;

	sscr0 = pxa_ssp_read_reg(ssp, SSCR0) & ~SSCR0_SSE;
	pxa_ssp_write_reg(ssp, SSCR0, sscr0);
}

#if IS_ENABLED(CONFIG_PXA_SSP)
struct ssp_device *pxa_ssp_request(int port, const char *label);
void pxa_ssp_free(struct ssp_device *);
struct ssp_device *pxa_ssp_request_of(const struct device_node *of_node,
				      const char *label);
#else
static inline struct ssp_device *pxa_ssp_request(int port, const char *label)
{
	return NULL;
}
static inline struct ssp_device *pxa_ssp_request_of(const struct device_node *n,
						    const char *name)
{
	return NULL;
}
static inline void pxa_ssp_free(struct ssp_device *ssp) {}
#endif

#endif	/* __LINUX_PXA2XX_SSP_H */
