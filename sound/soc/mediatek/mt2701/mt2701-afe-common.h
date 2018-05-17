/*
 * mt2701-afe-common.h  --  Mediatek 2701 audio driver definitions
 *
 * Copyright (c) 2016 MediaTek Inc.
 * Author: Garlic Tseng <garlic.tseng@mediatek.com>
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

#ifndef _MT_2701_AFE_COMMON_H_
#define _MT_2701_AFE_COMMON_H_

#include <sound/soc.h>
#include <linux/clk.h>
#include <linux/regmap.h>
#include "mt2701-reg.h"
#include "../common/mtk-base-afe.h"

#define MT2701_STREAM_DIR_NUM (SNDRV_PCM_STREAM_LAST + 1)
#define MT2701_PLL_DOMAIN_0_RATE	98304000
#define MT2701_PLL_DOMAIN_1_RATE	90316800
#define MT2701_I2S_NUM	4

enum {
	MT2701_MEMIF_DL1,
	MT2701_MEMIF_DL2,
	MT2701_MEMIF_DL3,
	MT2701_MEMIF_DL4,
	MT2701_MEMIF_DL5,
	MT2701_MEMIF_DL_SINGLE_NUM,
	MT2701_MEMIF_DLM = MT2701_MEMIF_DL_SINGLE_NUM,
	MT2701_MEMIF_UL1,
	MT2701_MEMIF_UL2,
	MT2701_MEMIF_UL3,
	MT2701_MEMIF_UL4,
	MT2701_MEMIF_UL5,
	MT2701_MEMIF_DLBT,
	MT2701_MEMIF_ULBT,
	MT2701_MEMIF_NUM,
	MT2701_IO_I2S = MT2701_MEMIF_NUM,
	MT2701_IO_2ND_I2S,
	MT2701_IO_3RD_I2S,
	MT2701_IO_4TH_I2S,
	MT2701_IO_5TH_I2S,
	MT2701_IO_6TH_I2S,
	MT2701_IO_MRG,
};

enum {
	MT2701_IRQ_ASYS_IRQ1,
	MT2701_IRQ_ASYS_IRQ2,
	MT2701_IRQ_ASYS_IRQ3,
	MT2701_IRQ_ASYS_END,
};

enum audio_base_clock {
	MT2701_INFRA_SYS_AUDIO,
	MT2701_TOP_AUD_MCLK_SRC0,
	MT2701_TOP_AUD_MCLK_SRC1,
	MT2701_TOP_AUD_A1SYS,
	MT2701_TOP_AUD_A2SYS,
	MT2701_AUDSYS_AFE,
	MT2701_AUDSYS_AFE_CONN,
	MT2701_AUDSYS_A1SYS,
	MT2701_AUDSYS_A2SYS,
	MT2701_BASE_CLK_NUM,
};

static const unsigned int mt2701_afe_backup_list[] = {
	AUDIO_TOP_CON0,
	AUDIO_TOP_CON4,
	AUDIO_TOP_CON5,
	ASYS_TOP_CON,
	AFE_CONN0,
	AFE_CONN1,
	AFE_CONN2,
	AFE_CONN3,
	AFE_CONN15,
	AFE_CONN16,
	AFE_CONN17,
	AFE_CONN18,
	AFE_CONN19,
	AFE_CONN20,
	AFE_CONN21,
	AFE_CONN22,
	AFE_DAC_CON0,
	AFE_MEMIF_PBUF_SIZE,
};

struct mt2701_i2s_data {
	int i2s_ctrl_reg;
	int i2s_asrc_fs_shift;
	int i2s_asrc_fs_mask;
};

enum mt2701_i2s_dir {
	I2S_OUT,
	I2S_IN,
	I2S_DIR_NUM,
};

struct mt2701_i2s_path {
	int dai_id;
	int mclk_rate;
	int on[I2S_DIR_NUM];
	int occupied[I2S_DIR_NUM];
	const struct mt2701_i2s_data *i2s_data[I2S_DIR_NUM];
	struct clk *hop_ck[I2S_DIR_NUM];
	struct clk *sel_ck;
	struct clk *div_ck;
	struct clk *mclk_ck;
	struct clk *asrco_ck;
};

struct mt2701_afe_private {
	struct mt2701_i2s_path i2s_path[MT2701_I2S_NUM];
	struct clk *base_ck[MT2701_BASE_CLK_NUM];
	struct clk *mrgif_ck;
	bool mrg_enable[MT2701_STREAM_DIR_NUM];
};

#endif
