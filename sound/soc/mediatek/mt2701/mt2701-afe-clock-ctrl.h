/*
 * mt2701-afe-clock-ctrl.h  --  Mediatek 2701 afe clock ctrl definition
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

#ifndef _MT2701_AFE_CLOCK_CTRL_H_
#define _MT2701_AFE_CLOCK_CTRL_H_

struct mtk_base_afe;

int mt2701_init_clock(struct mtk_base_afe *afe);
int mt2701_afe_enable_clock(struct mtk_base_afe *afe);
int mt2701_afe_disable_clock(struct mtk_base_afe *afe);

int mt2701_afe_enable_i2s(struct mtk_base_afe *afe, int id, int dir);
void mt2701_afe_disable_i2s(struct mtk_base_afe *afe, int id, int dir);
int mt2701_afe_enable_mclk(struct mtk_base_afe *afe, int id);
void mt2701_afe_disable_mclk(struct mtk_base_afe *afe, int id);

int mt2701_enable_btmrg_clk(struct mtk_base_afe *afe);
void mt2701_disable_btmrg_clk(struct mtk_base_afe *afe);

void mt2701_mclk_configuration(struct mtk_base_afe *afe, int id, int domain,
			       int mclk);

#endif
