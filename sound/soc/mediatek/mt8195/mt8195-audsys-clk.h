/* SPDX-License-Identifier: GPL-2.0 */
/*
 * mt8195-audsys-clk.h  --  Mediatek 8195 audsys clock definition
 *
 * Copyright (c) 2021 MediaTek Inc.
 * Author: Trevor Wu <trevor.wu@mediatek.com>
 */

#ifndef _MT8195_AUDSYS_CLK_H_
#define _MT8195_AUDSYS_CLK_H_

int mt8195_audsys_clk_register(struct mtk_base_afe *afe);
void mt8195_audsys_clk_unregister(struct mtk_base_afe *afe);

#endif
