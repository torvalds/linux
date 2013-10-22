/*
 * TI clock drivers support
 *
 * Copyright (C) 2013 Texas Instruments, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#ifndef __LINUX_CLK_TI_H__
#define __LINUX_CLK_TI_H__

#include <linux/clkdev.h>

/**
 * struct ti_dt_clk - OMAP DT clock alias declarations
 * @lk: clock lookup definition
 * @node_name: clock DT node to map to
 */
struct ti_dt_clk {
	struct clk_lookup		lk;
	char				*node_name;
};

#define DT_CLK(dev, con, name)		\
	{				\
		.lk = {			\
			.dev_id = dev,	\
			.con_id = con,	\
		},			\
		.node_name = name,	\
	}

/* Maximum number of clock memmaps */
#define CLK_MAX_MEMMAPS			4

typedef void (*ti_of_clk_init_cb_t)(struct clk_hw *, struct device_node *);

/**
 * struct clk_omap_reg - OMAP register declaration
 * @offset: offset from the master IP module base address
 * @index: index of the master IP module
 */
struct clk_omap_reg {
	u16 offset;
	u16 index;
};

/**
 * struct ti_clk_ll_ops - low-level register access ops for a clock
 * @clk_readl: pointer to register read function
 * @clk_writel: pointer to register write function
 *
 * Low-level register access ops are generally used by the basic clock types
 * (clk-gate, clk-mux, clk-divider etc.) to provide support for various
 * low-level hardware interfaces (direct MMIO, regmap etc.), but can also be
 * used by other hardware-specific clock drivers if needed.
 */
struct ti_clk_ll_ops {
	u32	(*clk_readl)(void __iomem *reg);
	void	(*clk_writel)(u32 val, void __iomem *reg);
};

extern struct ti_clk_ll_ops *ti_clk_ll_ops;

void __iomem *ti_clk_get_reg_addr(struct device_node *node, int index);
void ti_dt_clocks_register(struct ti_dt_clk *oclks);
void ti_dt_clk_init_provider(struct device_node *np, int index);
int ti_clk_retry_init(struct device_node *node, struct clk_hw *hw,
		      ti_of_clk_init_cb_t func);

#endif
