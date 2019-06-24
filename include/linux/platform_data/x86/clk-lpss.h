/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Intel Low Power Subsystem clocks.
 *
 * Copyright (C) 2013, Intel Corporation
 * Authors: Mika Westerberg <mika.westerberg@linux.intel.com>
 *          Rafael J. Wysocki <rafael.j.wysocki@intel.com>
 */

#ifndef __CLK_LPSS_H
#define __CLK_LPSS_H

struct lpss_clk_data {
	const char *name;
	struct clk *clk;
};

extern int lpt_clk_init(void);

#endif /* __CLK_LPSS_H */
