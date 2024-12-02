/* SPDX-License-Identifier: GPL-2.0 */
/*
 * mtk-soc-card.h  --  MediaTek soc card data definition
 *
 * Copyright (c) 2022 MediaTek Inc.
 * Author: Chunxu Li <chunxu.li@mediatek.com>
 */

#ifndef _MTK_SOC_CARD_H_
#define _MTK_SOC_CARD_H_

struct mtk_soc_card_data {
	void *mach_priv;
	void *sof_priv;
};

#endif
