/*
 * include/linux/spi/mxs-spi.h
 *
 * Freescale i.MX233/i.MX28 SPI controller register definition
 *
 * Copyright 2008 Embedded Alley Solutions, Inc.
 * Copyright 2009-2011 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __LINUX_SPI_MXS_SPI_H__
#define __LINUX_SPI_MXS_SPI_H__

#include <linux/dmaengine.h>

#define ssp_is_old(host)	((host)->devid == IMX23_SSP)

/* SSP registers */
#define HW_SSP_CTRL0				0x000
#define  BM_SSP_CTRL0_RUN			(1 << 29)
#define  BM_SSP_CTRL0_SDIO_IRQ_CHECK		(1 << 28)
#define  BM_SSP_CTRL0_LOCK_CS			(1 << 27)
#define  BM_SSP_CTRL0_IGNORE_CRC		(1 << 26)
#define  BM_SSP_CTRL0_READ			(1 << 25)
#define  BM_SSP_CTRL0_DATA_XFER			(1 << 24)
#define  BP_SSP_CTRL0_BUS_WIDTH			22
#define  BM_SSP_CTRL0_BUS_WIDTH			(0x3 << 22)
#define  BM_SSP_CTRL0_WAIT_FOR_IRQ		(1 << 21)
#define  BM_SSP_CTRL0_WAIT_FOR_CMD		(1 << 20)
#define  BM_SSP_CTRL0_LONG_RESP			(1 << 19)
#define  BM_SSP_CTRL0_GET_RESP			(1 << 17)
#define  BM_SSP_CTRL0_ENABLE			(1 << 16)
#define  BP_SSP_CTRL0_XFER_COUNT		0
#define  BM_SSP_CTRL0_XFER_COUNT		0xffff
#define HW_SSP_CMD0				0x010
#define  BM_SSP_CMD0_DBL_DATA_RATE_EN		(1 << 25)
#define  BM_SSP_CMD0_SLOW_CLKING_EN		(1 << 22)
#define  BM_SSP_CMD0_CONT_CLKING_EN		(1 << 21)
#define  BM_SSP_CMD0_APPEND_8CYC		(1 << 20)
#define  BP_SSP_CMD0_BLOCK_SIZE			16
#define  BM_SSP_CMD0_BLOCK_SIZE			(0xf << 16)
#define  BP_SSP_CMD0_BLOCK_COUNT		8
#define  BM_SSP_CMD0_BLOCK_COUNT		(0xff << 8)
#define  BP_SSP_CMD0_CMD			0
#define  BM_SSP_CMD0_CMD			0xff
#define HW_SSP_CMD1				0x020
#define HW_SSP_XFER_SIZE			0x030
#define HW_SSP_BLOCK_SIZE			0x040
#define  BP_SSP_BLOCK_SIZE_BLOCK_COUNT		4
#define  BM_SSP_BLOCK_SIZE_BLOCK_COUNT		(0xffffff << 4)
#define  BP_SSP_BLOCK_SIZE_BLOCK_SIZE		0
#define  BM_SSP_BLOCK_SIZE_BLOCK_SIZE		0xf
#define HW_SSP_TIMING(h)			(ssp_is_old(h) ? 0x050 : 0x070)
#define  BP_SSP_TIMING_TIMEOUT			16
#define  BM_SSP_TIMING_TIMEOUT			(0xffff << 16)
#define  BP_SSP_TIMING_CLOCK_DIVIDE		8
#define  BM_SSP_TIMING_CLOCK_DIVIDE		(0xff << 8)
#define  BF_SSP_TIMING_CLOCK_DIVIDE(v)		\
			(((v) << 8) & BM_SSP_TIMING_CLOCK_DIVIDE)
#define  BP_SSP_TIMING_CLOCK_RATE		0
#define  BM_SSP_TIMING_CLOCK_RATE		0xff
#define BF_SSP_TIMING_CLOCK_RATE(v)		\
			(((v) << 0) & BM_SSP_TIMING_CLOCK_RATE)
#define HW_SSP_CTRL1(h)				(ssp_is_old(h) ? 0x060 : 0x080)
#define  BM_SSP_CTRL1_SDIO_IRQ			(1 << 31)
#define  BM_SSP_CTRL1_SDIO_IRQ_EN		(1 << 30)
#define  BM_SSP_CTRL1_RESP_ERR_IRQ		(1 << 29)
#define  BM_SSP_CTRL1_RESP_ERR_IRQ_EN		(1 << 28)
#define  BM_SSP_CTRL1_RESP_TIMEOUT_IRQ		(1 << 27)
#define  BM_SSP_CTRL1_RESP_TIMEOUT_IRQ_EN	(1 << 26)
#define  BM_SSP_CTRL1_DATA_TIMEOUT_IRQ		(1 << 25)
#define  BM_SSP_CTRL1_DATA_TIMEOUT_IRQ_EN	(1 << 24)
#define  BM_SSP_CTRL1_DATA_CRC_IRQ		(1 << 23)
#define  BM_SSP_CTRL1_DATA_CRC_IRQ_EN		(1 << 22)
#define  BM_SSP_CTRL1_FIFO_UNDERRUN_IRQ		(1 << 21)
#define  BM_SSP_CTRL1_FIFO_UNDERRUN_IRQ_EN	(1 << 20)
#define  BM_SSP_CTRL1_RECV_TIMEOUT_IRQ		(1 << 17)
#define  BM_SSP_CTRL1_RECV_TIMEOUT_IRQ_EN	(1 << 16)
#define  BM_SSP_CTRL1_FIFO_OVERRUN_IRQ		(1 << 15)
#define  BM_SSP_CTRL1_FIFO_OVERRUN_IRQ_EN	(1 << 14)
#define  BM_SSP_CTRL1_DMA_ENABLE		(1 << 13)
#define  BM_SSP_CTRL1_PHASE			(1 << 10)
#define  BM_SSP_CTRL1_POLARITY			(1 << 9)
#define  BP_SSP_CTRL1_WORD_LENGTH		4
#define  BM_SSP_CTRL1_WORD_LENGTH		(0xf << 4)
#define  BF_SSP_CTRL1_WORD_LENGTH(v)		\
			(((v) << 4) & BM_SSP_CTRL1_WORD_LENGTH)
#define  BV_SSP_CTRL1_WORD_LENGTH__FOUR_BITS	0x3
#define  BV_SSP_CTRL1_WORD_LENGTH__EIGHT_BITS	0x7
#define  BV_SSP_CTRL1_WORD_LENGTH__SIXTEEN_BITS	0xF
#define  BP_SSP_CTRL1_SSP_MODE			0
#define  BM_SSP_CTRL1_SSP_MODE			0xf
#define  BF_SSP_CTRL1_SSP_MODE(v)		\
			(((v) << 0) & BM_SSP_CTRL1_SSP_MODE)
#define  BV_SSP_CTRL1_SSP_MODE__SPI		0x0
#define  BV_SSP_CTRL1_SSP_MODE__SSI		0x1
#define  BV_SSP_CTRL1_SSP_MODE__SD_MMC		0x3
#define  BV_SSP_CTRL1_SSP_MODE__MS		0x4

#define HW_SSP_DATA(h)				(ssp_is_old(h) ? 0x070 : 0x090)

#define HW_SSP_SDRESP0(h)			(ssp_is_old(h) ? 0x080 : 0x0a0)
#define HW_SSP_SDRESP1(h)			(ssp_is_old(h) ? 0x090 : 0x0b0)
#define HW_SSP_SDRESP2(h)			(ssp_is_old(h) ? 0x0a0 : 0x0c0)
#define HW_SSP_SDRESP3(h)			(ssp_is_old(h) ? 0x0b0 : 0x0d0)
#define HW_SSP_STATUS(h)			(ssp_is_old(h) ? 0x0c0 : 0x100)
#define  BM_SSP_STATUS_CARD_DETECT		(1 << 28)
#define  BM_SSP_STATUS_SDIO_IRQ			(1 << 17)
#define  BM_SSP_STATUS_FIFO_EMPTY		(1 << 5)

#define BF_SSP(value, field)	(((value) << BP_SSP_##field) & BM_SSP_##field)

#define SSP_PIO_NUM	3

enum mxs_ssp_id {
	IMX23_SSP,
	IMX28_SSP,
};

struct mxs_ssp {
	struct device			*dev;
	void __iomem			*base;
	struct clk			*clk;
	unsigned int			clk_rate;
	enum mxs_ssp_id			devid;

	struct dma_chan			*dmach;
	unsigned int			dma_dir;
	enum dma_transfer_direction	slave_dirn;
	u32				ssp_pio_words[SSP_PIO_NUM];
};

void mxs_ssp_set_clk_rate(struct mxs_ssp *ssp, unsigned int rate);

#endif	/* __LINUX_SPI_MXS_SPI_H__ */
