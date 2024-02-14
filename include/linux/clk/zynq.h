/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2013 Xilinx Inc.
 * Copyright (C) 2012 National Instruments
 */

#ifndef __LINUX_CLK_ZYNQ_H_
#define __LINUX_CLK_ZYNQ_H_

#include <linux/spinlock.h>

void zynq_clock_init(void);

struct clk *clk_register_zynq_pll(const char *name, const char *parent,
		void __iomem *pll_ctrl, void __iomem *pll_status, u8 lock_index,
		spinlock_t *lock);
#endif
