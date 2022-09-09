/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Defines for Multi-Channel Buffered Serial Port
 *
 * Copyright (C) 2002 RidgeRun, Inc.
 * Author: Steve Johnson
 */
#ifndef __ASOC_TI_MCBSP_H
#define __ASOC_TI_MCBSP_H

#include <linux/spinlock.h>
#include <linux/clk.h>

/* Platform specific configuration */
struct omap_mcbsp_ops {
	void (*request)(unsigned int);
	void (*free)(unsigned int);
};

struct omap_mcbsp_platform_data {
	struct omap_mcbsp_ops *ops;
	u16 buffer_size;
	u8 reg_size;
	u8 reg_step;

	/* McBSP platform and instance specific features */
	bool has_wakeup; /* Wakeup capability */
	bool has_ccr; /* Transceiver has configuration control registers */
	int (*force_ick_on)(struct clk *clk, bool force_on);
};

void omap3_mcbsp_init_pdata_callback(struct omap_mcbsp_platform_data *pdata);

#endif
