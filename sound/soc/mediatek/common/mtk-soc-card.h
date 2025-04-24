/* SPDX-License-Identifier: GPL-2.0 */
/*
 * mtk-soc-card.h  --  MediaTek soc card data definition
 *
 * Copyright (c) 2022 MediaTek Inc.
 * Author: Chunxu Li <chunxu.li@mediatek.com>
 */

#ifndef _MTK_SOC_CARD_H_
#define _MTK_SOC_CARD_H_

struct mtk_platform_card_data;
struct mtk_sof_priv;

struct mtk_soc_card_data {
	const struct mtk_sof_priv *sof_priv;
	struct list_head sof_dai_link_list;
	struct mtk_platform_card_data *card_data;
	struct snd_soc_component *accdet;
	void *mach_priv;
};

#endif
