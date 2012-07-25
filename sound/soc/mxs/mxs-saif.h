/*
 * Copyright (C) 2011 Freescale Semiconductor, Inc. All Rights Reserved.
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
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */


#ifndef _MXS_SAIF_H
#define _MXS_SAIF_H

#define SAIF_CTRL	0x0
#define SAIF_STAT	0x10
#define SAIF_DATA	0x20
#define SAIF_VERSION	0X30

/* SAIF_CTRL */
#define BM_SAIF_CTRL_SFTRST		0x80000000
#define BM_SAIF_CTRL_CLKGATE		0x40000000
#define BP_SAIF_CTRL_BITCLK_MULT_RATE	27
#define BM_SAIF_CTRL_BITCLK_MULT_RATE	0x38000000
#define BF_SAIF_CTRL_BITCLK_MULT_RATE(v) \
		(((v) << 27) & BM_SAIF_CTRL_BITCLK_MULT_RATE)
#define BM_SAIF_CTRL_BITCLK_BASE_RATE	0x04000000
#define BM_SAIF_CTRL_FIFO_ERROR_IRQ_EN	0x02000000
#define BM_SAIF_CTRL_FIFO_SERVICE_IRQ_EN	0x01000000
#define BP_SAIF_CTRL_RSRVD2		21
#define BM_SAIF_CTRL_RSRVD2		0x00E00000

#define BP_SAIF_CTRL_DMAWAIT_COUNT	16
#define BM_SAIF_CTRL_DMAWAIT_COUNT	0x001F0000
#define BF_SAIF_CTRL_DMAWAIT_COUNT(v) \
		(((v) << 16) & BM_SAIF_CTRL_DMAWAIT_COUNT)
#define BP_SAIF_CTRL_CHANNEL_NUM_SELECT 14
#define BM_SAIF_CTRL_CHANNEL_NUM_SELECT 0x0000C000
#define BF_SAIF_CTRL_CHANNEL_NUM_SELECT(v) \
		(((v) << 14) & BM_SAIF_CTRL_CHANNEL_NUM_SELECT)
#define BM_SAIF_CTRL_LRCLK_PULSE	0x00002000
#define BM_SAIF_CTRL_BIT_ORDER		0x00001000
#define BM_SAIF_CTRL_DELAY		0x00000800
#define BM_SAIF_CTRL_JUSTIFY		0x00000400
#define BM_SAIF_CTRL_LRCLK_POLARITY	0x00000200
#define BM_SAIF_CTRL_BITCLK_EDGE	0x00000100
#define BP_SAIF_CTRL_WORD_LENGTH	4
#define BM_SAIF_CTRL_WORD_LENGTH	0x000000F0
#define BF_SAIF_CTRL_WORD_LENGTH(v) \
		(((v) << 4) & BM_SAIF_CTRL_WORD_LENGTH)
#define BM_SAIF_CTRL_BITCLK_48XFS_ENABLE	0x00000008
#define BM_SAIF_CTRL_SLAVE_MODE		0x00000004
#define BM_SAIF_CTRL_READ_MODE		0x00000002
#define BM_SAIF_CTRL_RUN		0x00000001

/* SAIF_STAT */
#define BM_SAIF_STAT_PRESENT		0x80000000
#define BP_SAIF_STAT_RSRVD2		17
#define BM_SAIF_STAT_RSRVD2		0x7FFE0000
#define BF_SAIF_STAT_RSRVD2(v) \
		(((v) << 17) & BM_SAIF_STAT_RSRVD2)
#define BM_SAIF_STAT_DMA_PREQ		0x00010000
#define BP_SAIF_STAT_RSRVD1		7
#define BM_SAIF_STAT_RSRVD1		0x0000FF80
#define BF_SAIF_STAT_RSRVD1(v) \
		(((v) << 7) & BM_SAIF_STAT_RSRVD1)

#define BM_SAIF_STAT_FIFO_UNDERFLOW_IRQ 0x00000040
#define BM_SAIF_STAT_FIFO_OVERFLOW_IRQ	0x00000020
#define BM_SAIF_STAT_FIFO_SERVICE_IRQ	0x00000010
#define BP_SAIF_STAT_RSRVD0		1
#define BM_SAIF_STAT_RSRVD0		0x0000000E
#define BF_SAIF_STAT_RSRVD0(v) \
		(((v) << 1) & BM_SAIF_STAT_RSRVD0)
#define BM_SAIF_STAT_BUSY		0x00000001

/* SAFI_DATA */
#define BP_SAIF_DATA_PCM_RIGHT		16
#define BM_SAIF_DATA_PCM_RIGHT		0xFFFF0000
#define BF_SAIF_DATA_PCM_RIGHT(v) \
		(((v) << 16) & BM_SAIF_DATA_PCM_RIGHT)
#define BP_SAIF_DATA_PCM_LEFT		0
#define BM_SAIF_DATA_PCM_LEFT		0x0000FFFF
#define BF_SAIF_DATA_PCM_LEFT(v)	\
		(((v) << 0) & BM_SAIF_DATA_PCM_LEFT)

/* SAIF_VERSION */
#define BP_SAIF_VERSION_MAJOR		24
#define BM_SAIF_VERSION_MAJOR		0xFF000000
#define BF_SAIF_VERSION_MAJOR(v) \
		(((v) << 24) & BM_SAIF_VERSION_MAJOR)
#define BP_SAIF_VERSION_MINOR		16
#define BM_SAIF_VERSION_MINOR		0x00FF0000
#define BF_SAIF_VERSION_MINOR(v) \
		(((v) << 16) & BM_SAIF_VERSION_MINOR)
#define BP_SAIF_VERSION_STEP		0
#define BM_SAIF_VERSION_STEP		0x0000FFFF
#define BF_SAIF_VERSION_STEP(v) \
		(((v) << 0) & BM_SAIF_VERSION_STEP)

#define MXS_SAIF_MCLK		0

#include "mxs-pcm.h"

struct mxs_saif {
	struct device *dev;
	struct clk *clk;
	unsigned int mclk;
	unsigned int mclk_in_use;
	void __iomem *base;
	int irq;
	struct mxs_pcm_dma_params dma_param;
	unsigned int id;
	unsigned int master_id;
	unsigned int cur_rate;
	unsigned int ongoing;

	u32 fifo_underrun;
	u32 fifo_overrun;
};

extern int mxs_saif_put_mclk(unsigned int saif_id);
extern int mxs_saif_get_mclk(unsigned int saif_id, unsigned int mclk,
					unsigned int rate);
#endif
