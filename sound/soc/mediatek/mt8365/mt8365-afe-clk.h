/* SPDX-License-Identifier: GPL-2.0
 *
 * MediaTek 8365 AFE clock control definitions
 *
 * Copyright (c) 2024 MediaTek Inc.
 * Authors: Jia Zeng <jia.zeng@mediatek.com>
 *          Alexandre Mergnat <amergnat@baylibre.com>
 */

#ifndef _MT8365_AFE_UTILS_H_
#define _MT8365_AFE_UTILS_H_

struct mtk_base_afe;
struct clk;

int mt8365_afe_init_audio_clk(struct mtk_base_afe *afe);
void mt8365_afe_disable_clk(struct mtk_base_afe *afe, struct clk *clk);
int mt8365_afe_set_clk_rate(struct mtk_base_afe *afe, struct clk *clk, unsigned int rate);
int mt8365_afe_set_clk_parent(struct mtk_base_afe *afe, struct clk *clk, struct clk *parent);
int mt8365_afe_enable_top_cg(struct mtk_base_afe *afe, unsigned int cg_type);
int mt8365_afe_disable_top_cg(struct mtk_base_afe *afe, unsigned int cg_type);
int mt8365_afe_enable_main_clk(struct mtk_base_afe *afe);
int mt8365_afe_disable_main_clk(struct mtk_base_afe *afe);
int mt8365_afe_emi_clk_on(struct mtk_base_afe *afe);
int mt8365_afe_emi_clk_off(struct mtk_base_afe *afe);
int mt8365_afe_enable_afe_on(struct mtk_base_afe *afe);
int mt8365_afe_disable_afe_on(struct mtk_base_afe *afe);
int mt8365_afe_enable_apll_tuner_cfg(struct mtk_base_afe *afe, unsigned int apll);
int mt8365_afe_disable_apll_tuner_cfg(struct mtk_base_afe *afe, unsigned int apll);
int mt8365_afe_enable_apll_associated_cfg(struct mtk_base_afe *afe, unsigned int apll);
int mt8365_afe_disable_apll_associated_cfg(struct mtk_base_afe *afe, unsigned int apll);
#endif
