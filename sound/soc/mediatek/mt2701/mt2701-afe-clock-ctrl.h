/* SPDX-License-Identifier: GPL-2.0 */
/*
 * mt2701-afe-clock-ctrl.h  --  Mediatek 2701 afe clock ctrl definition
 *
 * Copyright (c) 2016 MediaTek Inc.
 * Author: Garlic Tseng <garlic.tseng@mediatek.com>
 *	   Ryder Lee <ryder.lee@mediatek.com>
 */

#ifndef _MT2701_AFE_CLOCK_CTRL_H_
#define _MT2701_AFE_CLOCK_CTRL_H_

struct mtk_base_afe;
struct mt2701_i2s_path;

int mt2701_init_clock(struct mtk_base_afe *afe);
int mt2701_afe_enable_clock(struct mtk_base_afe *afe);
int mt2701_afe_disable_clock(struct mtk_base_afe *afe);

int mt2701_afe_enable_i2s(struct mtk_base_afe *afe,
			  struct mt2701_i2s_path *i2s_path,
			  int dir);
void mt2701_afe_disable_i2s(struct mtk_base_afe *afe,
			    struct mt2701_i2s_path *i2s_path,
			    int dir);
int mt2701_afe_enable_mclk(struct mtk_base_afe *afe, int id);
void mt2701_afe_disable_mclk(struct mtk_base_afe *afe, int id);

int mt2701_enable_btmrg_clk(struct mtk_base_afe *afe);
void mt2701_disable_btmrg_clk(struct mtk_base_afe *afe);

int mt2701_mclk_configuration(struct mtk_base_afe *afe, int id);

#endif
