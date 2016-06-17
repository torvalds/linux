/*
 * mt8173_afe_common.h  --  Mediatek 8173 audio driver common definitions
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

#ifndef _MT8173_AFE_COMMON_H_
#define _MT8173_AFE_COMMON_H_

#include <linux/clk.h>
#include <linux/regmap.h>

enum {
	MT8173_AFE_MEMIF_DL1,
	MT8173_AFE_MEMIF_DL2,
	MT8173_AFE_MEMIF_VUL,
	MT8173_AFE_MEMIF_DAI,
	MT8173_AFE_MEMIF_AWB,
	MT8173_AFE_MEMIF_MOD_DAI,
	MT8173_AFE_MEMIF_HDMI,
	MT8173_AFE_MEMIF_NUM,
	MT8173_AFE_IO_MOD_PCM1 = MT8173_AFE_MEMIF_NUM,
	MT8173_AFE_IO_MOD_PCM2,
	MT8173_AFE_IO_PMIC,
	MT8173_AFE_IO_I2S,
	MT8173_AFE_IO_2ND_I2S,
	MT8173_AFE_IO_HW_GAIN1,
	MT8173_AFE_IO_HW_GAIN2,
	MT8173_AFE_IO_MRG_O,
	MT8173_AFE_IO_MRG_I,
	MT8173_AFE_IO_DAIBT,
	MT8173_AFE_IO_HDMI,
};

enum {
	MT8173_AFE_IRQ_1,
	MT8173_AFE_IRQ_2,
	MT8173_AFE_IRQ_3,
	MT8173_AFE_IRQ_4,
	MT8173_AFE_IRQ_5,
	MT8173_AFE_IRQ_6,
	MT8173_AFE_IRQ_7,
	MT8173_AFE_IRQ_8,
	MT8173_AFE_IRQ_NUM,
};

enum {
	MT8173_CLK_INFRASYS_AUD,
	MT8173_CLK_TOP_PDN_AUD,
	MT8173_CLK_TOP_PDN_AUD_BUS,
	MT8173_CLK_I2S0_M,
	MT8173_CLK_I2S1_M,
	MT8173_CLK_I2S2_M,
	MT8173_CLK_I2S3_M,
	MT8173_CLK_I2S3_B,
	MT8173_CLK_BCK0,
	MT8173_CLK_BCK1,
	MT8173_CLK_NUM
};

struct mt8173_afe;
struct snd_pcm_substream;

struct mt8173_afe_memif_data {
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
	int msb_shift;
};

struct mt8173_afe_memif {
	unsigned int phys_buf_addr;
	int buffer_size;
	struct snd_pcm_substream *substream;
	const struct mt8173_afe_memif_data *data;
	const struct mt8173_afe_irq_data *irqdata;
};

#endif
