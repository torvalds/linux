// SPDX-License-Identifier: GPL-2.0
/*
 * MediaTek ALSA SoC Audio DAI ADDA Common
 *
 * Copyright (c) 2021 MediaTek Inc.
 * Copyright (c) 2024 Collabora Ltd.
 *         AngeloGioacchino Del Regno <angelogioacchino.delregno@collabora.com>
 */

#include <linux/delay.h>
#include <linux/dev_printk.h>

#include "mtk-base-afe.h"
#include "mtk-dai-adda-common.h"

unsigned int mtk_adda_dl_rate_transform(struct mtk_base_afe *afe, u32 rate)
{
	switch (rate) {
	case 8000:
		return MTK_AFE_ADDA_DL_RATE_8K;
	case 11025:
		return MTK_AFE_ADDA_DL_RATE_11K;
	case 12000:
		return MTK_AFE_ADDA_DL_RATE_12K;
	case 16000:
		return MTK_AFE_ADDA_DL_RATE_16K;
	case 22050:
		return MTK_AFE_ADDA_DL_RATE_22K;
	case 24000:
		return MTK_AFE_ADDA_DL_RATE_24K;
	case 32000:
		return MTK_AFE_ADDA_DL_RATE_32K;
	case 44100:
		return MTK_AFE_ADDA_DL_RATE_44K;
	case 48000:
		return MTK_AFE_ADDA_DL_RATE_48K;
	case 96000:
		return MTK_AFE_ADDA_DL_RATE_96K;
	case 192000:
		return MTK_AFE_ADDA_DL_RATE_192K;
	default:
		dev_info(afe->dev, "%s(), rate %d invalid, use 48kHz!!!\n",
			 __func__, rate);
		return MTK_AFE_ADDA_DL_RATE_48K;
	}
}
EXPORT_SYMBOL_GPL(mtk_adda_dl_rate_transform);

unsigned int mtk_adda_ul_rate_transform(struct mtk_base_afe *afe, u32 rate)
{
	switch (rate) {
	case 8000:
		return MTK_AFE_ADDA_UL_RATE_8K;
	case 16000:
		return MTK_AFE_ADDA_UL_RATE_16K;
	case 32000:
		return MTK_AFE_ADDA_UL_RATE_32K;
	case 48000:
		return MTK_AFE_ADDA_UL_RATE_48K;
	case 96000:
		return MTK_AFE_ADDA_UL_RATE_96K;
	case 192000:
		return MTK_AFE_ADDA_UL_RATE_192K;
	default:
		dev_info(afe->dev, "%s(), rate %d invalid, use 48kHz!!!\n",
			 __func__, rate);
		return MTK_AFE_ADDA_UL_RATE_48K;
	}
}
EXPORT_SYMBOL_GPL(mtk_adda_ul_rate_transform);
