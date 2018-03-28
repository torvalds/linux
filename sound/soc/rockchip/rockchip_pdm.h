/*
 * Rockchip PDM ALSA SoC Digital Audio Interface(DAI)  driver
 *
 * Copyright (C) 2017 Fuzhou Rockchip Electronics Co., Ltd
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef _ROCKCHIP_PDM_H
#define _ROCKCHIP_PDM_H

/* PDM REGS */
#define PDM_SYSCONFIG	(0x0000)
#define PDM_CTRL0	(0x0004)
#define PDM_CTRL1	(0x0008)
#define PDM_CLK_CTRL	(0x000c)
#define PDM_HPF_CTRL	(0x0010)
#define PDM_FIFO_CTRL	(0x0014)
#define PDM_DMA_CTRL	(0x0018)
#define PDM_INT_EN	(0x001c)
#define PDM_INT_CLR	(0x0020)
#define PDM_INT_ST	(0x0024)
#define PDM_RXFIFO_DATA	(0x0030)
#define PDM_DATA_VALID	(0x0054)
#define PDM_VERSION	(0x0058)

/* PDM_SYSCONFIG */
#define PDM_RX_MASK		(0x1 << 2)
#define PDM_RX_START		(0x1 << 2)
#define PDM_RX_STOP		(0x0 << 2)
#define PDM_RX_CLR_MASK		(0x1 << 0)
#define PDM_RX_CLR_WR		(0x1 << 0)
#define PDM_RX_CLR_DONE		(0x0 << 0)

/* PDM CTRL0 */
#define PDM_PATH_MSK		(0xf << 27)
#define PDM_MODE_MSK		BIT(31)
#define PDM_MODE_RJ		0
#define PDM_MODE_LJ		BIT(31)
#define PDM_PATH3_EN		BIT(30)
#define PDM_PATH2_EN		BIT(29)
#define PDM_PATH1_EN		BIT(28)
#define PDM_PATH0_EN		BIT(27)
#define PDM_HWT_EN		BIT(26)
#define PDM_VDW_MSK		(0x1f << 0)
#define PDM_VDW(X)		((X - 1) << 0)

/* PDM CLK CTRL */
#define PDM_CLK_MSK		BIT(5)
#define PDM_CLK_EN		BIT(5)
#define PDM_CLK_DIS		(0x0 << 5)
#define PDM_CKP_MSK		BIT(3)
#define PDM_CKP_NORMAL		(0x0 << 3)
#define PDM_CKP_INVERTED	BIT(3)
#define PDM_DS_RATIO_MSK	(0x7 << 0)
#define PDM_CLK_320FS		(0x0 << 0)
#define PDM_CLK_640FS		(0x1 << 0)
#define PDM_CLK_1280FS		(0x2 << 0)
#define PDM_CLK_2560FS		(0x3 << 0)
#define PDM_CLK_5120FS		(0x4 << 0)

/* PDM HPF CTRL */
#define PDM_HPF_LE		BIT(3)
#define PDM_HPF_RE		BIT(2)
#define PDM_HPF_CF_MSK		(0x3 << 0)
#define PDM_HPF_3P79HZ		(0x0 << 0)
#define PDM_HPF_60HZ		(0x1 << 0)
#define PDM_HPF_243HZ		(0x2 << 0)
#define PDM_HPF_493HZ		(0x3 << 0)

/* PDM DMA CTRL */
#define PDM_DMA_RD_MSK		BIT(8)
#define PDM_DMA_RD_EN		BIT(8)
#define PDM_DMA_RD_DIS		(0x0 << 8)
#define PDM_DMA_RDL_MSK		(0x7f << 0)
#define PDM_DMA_RDL(X)		((X - 1) << 0)

#endif /* _ROCKCHIP_PDM_H */
