/* SPDX-License-Identifier: GPL-2.0 */
/*
 * mt7986-afe-common.h  --  MediaTek 7986 audio driver definitions
 *
 * Copyright (c) 2023 MediaTek Inc.
 * Authors: Vic Wu <vic.wu@mediatek.com>
 *          Maso Huang <maso.huang@mediatek.com>
 */

#ifndef _MT_7986_AFE_COMMON_H_
#define _MT_7986_AFE_COMMON_H_

#include <sound/soc.h>
#include <linux/clk.h>
#include <linux/list.h>
#include <linux/regmap.h>
#include "../common/mtk-base-afe.h"

enum {
	MT7986_MEMIF_DL1,
	MT7986_MEMIF_VUL12,
	MT7986_MEMIF_NUM,
	MT7986_DAI_ETDM = MT7986_MEMIF_NUM,
	MT7986_DAI_NUM,
};

enum {
	MT7986_IRQ_0,
	MT7986_IRQ_1,
	MT7986_IRQ_2,
	MT7986_IRQ_NUM,
};

struct mt7986_afe_private {
	struct clk_bulk_data *clks;
	int num_clks;

	int pm_runtime_bypass_reg_ctl;

	/* dai */
	void *dai_priv[MT7986_DAI_NUM];
};

unsigned int mt7986_afe_rate_transform(struct device *dev,
				       unsigned int rate);

/* dai register */
int mt7986_dai_etdm_register(struct mtk_base_afe *afe);
#endif
