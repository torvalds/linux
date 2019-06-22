/*
 * Intel Atom platform clocks for BayTrail and CherryTrail SoC.
 *
 * Copyright (C) 2016, Intel Corporation
 * Author: Irina Tirdea <irina.tirdea@intel.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#ifndef __PLATFORM_DATA_X86_CLK_PMC_ATOM_H
#define __PLATFORM_DATA_X86_CLK_PMC_ATOM_H

/**
 * struct pmc_clk - PMC platform clock configuration
 *
 * @name:	identified, typically pmc_plt_clk_<x>, x=[0..5]
 * @freq:	in Hz, 19.2MHz  and 25MHz (Baytrail only) supported
 * @parent_name: one of 'xtal' or 'osc'
 */
struct pmc_clk {
	const char *name;
	unsigned long freq;
	const char *parent_name;
};

/**
 * struct pmc_clk_data - common PMC clock configuration
 *
 * @base:	PMC clock register base offset
 * @clks:	pointer to set of registered clocks, typically 0..5
 * @critical:	flag to indicate if firmware enabled pmc_plt_clks
 *		should be marked as critial or not
 */
struct pmc_clk_data {
	void __iomem *base;
	const struct pmc_clk *clks;
	bool critical;
};

#endif /* __PLATFORM_DATA_X86_CLK_PMC_ATOM_H */
