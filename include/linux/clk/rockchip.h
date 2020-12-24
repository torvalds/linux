/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) 2020 Rockchip Electronics Co. Ltd.
 */

#ifndef __LINUX_CLK_ROCKCHIP_H_
#define __LINUX_CLK_ROCKCHIP_H_

#ifdef CONFIG_ROCKCHIP_CLK_COMPENSATION
int rockchip_pll_clk_compensation(struct clk *clk, int ppm);
#else
static inline int rockchip_pll_clk_compensation(struct clk *clk, int ppm)
{
	return -ENOSYS;
}
#endif

#endif /* __LINUX_CLK_ROCKCHIP_H_ */
