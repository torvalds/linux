/*
 * mt6797-afe-clk.h  --  Mediatek 6797 afe clock ctrl definition
 *
 * Copyright (c) 2018 MediaTek Inc.
 * Author: KaiChieh Chuang <kaichieh.chuang@mediatek.com>
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

#ifndef _MT6797_AFE_CLK_H_
#define _MT6797_AFE_CLK_H_

struct mtk_base_afe;

int mt6797_init_clock(struct mtk_base_afe *afe);
int mt6797_afe_enable_clock(struct mtk_base_afe *afe);
int mt6797_afe_disable_clock(struct mtk_base_afe *afe);
#endif
