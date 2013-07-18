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


void ti_dt_clocks_register(struct ti_dt_clk *oclks);

#endif
