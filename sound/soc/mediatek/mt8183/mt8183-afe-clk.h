/* SPDX-License-Identifier: GPL-2.0 */
/*
 * mt8183-afe-clk.h  --  Mediatek 8183 afe clock ctrl definition
 *
 * Copyright (c) 2018 MediaTek Inc.
 * Author: KaiChieh Chuang <kaichieh.chuang@mediatek.com>
 */

#ifndef _MT8183_AFE_CLK_H_
#define _MT8183_AFE_CLK_H_

/* APLL */
#define APLL1_W_NAME "APLL1"
#define APLL2_W_NAME "APLL2"
enum {
	MT8183_APLL1 = 0,
	MT8183_APLL2,
};

struct mtk_base_afe;

int mt8183_init_clock(struct mtk_base_afe *afe);
int mt8183_afe_enable_clock(struct mtk_base_afe *afe);
int mt8183_afe_disable_clock(struct mtk_base_afe *afe);

int mt8183_apll1_enable(struct mtk_base_afe *afe);
void mt8183_apll1_disable(struct mtk_base_afe *afe);

int mt8183_apll2_enable(struct mtk_base_afe *afe);
void mt8183_apll2_disable(struct mtk_base_afe *afe);

int mt8183_get_apll_rate(struct mtk_base_afe *afe, int apll);
int mt8183_get_apll_by_rate(struct mtk_base_afe *afe, int rate);
int mt8183_get_apll_by_name(struct mtk_base_afe *afe, const char *name);

int mt8183_mck_enable(struct mtk_base_afe *afe, int mck_id, int rate);
void mt8183_mck_disable(struct mtk_base_afe *afe, int mck_id);
#endif
