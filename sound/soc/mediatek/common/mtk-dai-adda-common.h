/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2021 MediaTek Inc.
 * Copyright (c) 2024 Collabora Ltd.
 *         AngeloGioacchino Del Regno <angelogioacchino.delregno@collabora.com>
 */

#ifndef _MTK_DAI_ADDA_COMMON_H_
#define _MTK_DAI_ADDA_COMMON_H_

struct mtk_base_afe;

enum adda_input_mode_rate {
	MTK_AFE_ADDA_DL_RATE_8K = 0,
	MTK_AFE_ADDA_DL_RATE_11K = 1,
	MTK_AFE_ADDA_DL_RATE_12K = 2,
	MTK_AFE_ADDA_DL_RATE_16K = 3,
	MTK_AFE_ADDA_DL_RATE_22K = 4,
	MTK_AFE_ADDA_DL_RATE_24K = 5,
	MTK_AFE_ADDA_DL_RATE_32K = 6,
	MTK_AFE_ADDA_DL_RATE_44K = 7,
	MTK_AFE_ADDA_DL_RATE_48K = 8,
	MTK_AFE_ADDA_DL_RATE_96K = 9,
	MTK_AFE_ADDA_DL_RATE_192K = 10,
};

enum adda_voice_mode_rate {
	MTK_AFE_ADDA_UL_RATE_8K = 0,
	MTK_AFE_ADDA_UL_RATE_16K = 1,
	MTK_AFE_ADDA_UL_RATE_32K = 2,
	MTK_AFE_ADDA_UL_RATE_48K = 3,
	MTK_AFE_ADDA_UL_RATE_96K = 4,
	MTK_AFE_ADDA_UL_RATE_192K = 5,
	MTK_AFE_ADDA_UL_RATE_48K_HD = 6,
};

enum adda_rxif_delay_data {
	DELAY_DATA_MISO1 = 0,
	DELAY_DATA_MISO0 = 1,
	DELAY_DATA_MISO2 = 1,
};

unsigned int mtk_adda_dl_rate_transform(struct mtk_base_afe *afe, u32 rate);
unsigned int mtk_adda_ul_rate_transform(struct mtk_base_afe *afe, u32 rate);
#endif
