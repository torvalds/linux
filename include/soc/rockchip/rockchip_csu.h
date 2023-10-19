/* SPDX-License-Identifier: (GPL-2.0+ OR MIT) */
/*
 * Copyright (c) 2023, Rockchip Electronics Co., Ltd
 */

#ifndef __SOC_ROCKCHIP_CSU_H
#define __SOC_ROCKCHIP_CSU_H

#include <dt-bindings/soc/rockchip-csu.h>

#define CSU_MAX_DIV		8
#define CSU_DIV_MASK		0x7
#define CSU_EN_MASK		0xefff

struct csu_clk;

#if IS_REACHABLE(CONFIG_ROCKCHIP_CSU)
struct csu_clk *rockchip_csu_get(struct device *dev, const char *name);
int rockchip_csu_enable(struct csu_clk *clk);
int rockchip_csu_disable(struct csu_clk *clk);
int rockchip_csu_set_div(struct csu_clk *clk, unsigned int div);
#else
static inline struct csu_clk *
rockchip_csu_get(struct device *dev, const char *name)
{
	return ERR_PTR(-EOPNOTSUPP);
}

static inline int rockchip_csu_enable(struct csu_clk *clk)
{
	return -EOPNOTSUPP;
}

static inline int rockchip_csu_disable(struct csu_clk *clk)
{
	return -EOPNOTSUPP;
}

static inline int
rockchip_csu_set_div(struct csu_clk *clk, unsigned int div)
{
	return -EOPNOTSUPP;
}
#endif

#endif
