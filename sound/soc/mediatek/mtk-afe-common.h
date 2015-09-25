/*
 * mtk_afe_common.h  --  Mediatek audio driver common definitions
 *
 * Copyright (c) 2015 MediaTek Inc.
 * Author: Koro Chen <koro.chen@mediatek.com>
 *             Sascha Hauer <s.hauer@pengutronix.de>
 *             Hidalgo Huang <hidalgo.huang@mediatek.com>
 *             Ir Lian <ir.lian@mediatek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef _MTK_AFE_COMMON_H_
#define _MTK_AFE_COMMON_H_

#include <linux/clk.h>
#include <linux/regmap.h>

enum {
	MTK_AFE_MEMIF_DL1,
	MTK_AFE_MEMIF_DL2,
	MTK_AFE_MEMIF_VUL,
	MTK_AFE_MEMIF_DAI,
	MTK_AFE_MEMIF_AWB,
	MTK_AFE_MEMIF_MOD_DAI,
	MTK_AFE_MEMIF_HDMI,
	MTK_AFE_MEMIF_NUM,
	MTK_AFE_IO_MOD_PCM1 = MTK_AFE_MEMIF_NUM,
	MTK_AFE_IO_MOD_PCM2,
	MTK_AFE_IO_PMIC,
	MTK_AFE_IO_I2S,
	MTK_AFE_IO_2ND_I2S,
	MTK_AFE_IO_HW_GAIN1,
	MTK_AFE_IO_HW_GAIN2,
	MTK_AFE_IO_MRG_O,
	MTK_AFE_IO_MRG_I,
	MTK_AFE_IO_DAIBT,
	MTK_AFE_IO_HDMI,
};

enum {
	MTK_AFE_IRQ_1,
	MTK_AFE_IRQ_2,
	MTK_AFE_IRQ_3,
	MTK_AFE_IRQ_4,
	MTK_AFE_IRQ_5,
	MTK_AFE_IRQ_6,
	MTK_AFE_IRQ_7,
	MTK_AFE_IRQ_8,
	MTK_AFE_IRQ_NUM,
};

enum {
	MTK_CLK_INFRASYS_AUD,
	MTK_CLK_TOP_PDN_AUD,
	MTK_CLK_TOP_PDN_AUD_BUS,
	MTK_CLK_I2S0_M,
	MTK_CLK_I2S1_M,
	MTK_CLK_I2S2_M,
	MTK_CLK_I2S3_M,
	MTK_CLK_I2S3_B,
	MTK_CLK_BCK0,
	MTK_CLK_BCK1,
	MTK_CLK_NUM
};

struct mtk_afe;
struct snd_pcm_substream;

struct mtk_afe_memif_data {
	int id;
	const char *name;
	int reg_ofs_base;
	int reg_ofs_cur;
	int fs_shift;
	int mono_shift;
	int enable_shift;
	int irq_reg_cnt;
	int irq_cnt_shift;
	int irq_en_shift;
	int irq_fs_shift;
	int irq_clr_shift;
};

struct mtk_afe_memif {
	unsigned int phys_buf_addr;
	int buffer_size;
	unsigned int hw_ptr;		/* Previous IRQ's HW ptr */
	struct snd_pcm_substream *substream;
	const struct mtk_afe_memif_data *data;
	const struct mtk_afe_irq_data *irqdata;
};

#endif
