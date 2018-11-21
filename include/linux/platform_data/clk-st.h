/* SPDX-License-Identifier: MIT */
/*
 * clock framework for AMD Stoney based clock
 *
 * Copyright 2018 Advanced Micro Devices, Inc.
 */

#ifndef __CLK_ST_H
#define __CLK_ST_H

#include <linux/compiler.h>

struct st_clk_data {
	void __iomem *base;
};

#endif /* __CLK_ST_H */
