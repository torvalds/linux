/*
 * omap-mcbsp.h
 *
 * Copyright (C) 2008 Nokia Corporation
 *
 * Contact: Jarkko Nikula <jarkko.nikula@nokia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#ifndef __OMAP_I2S_H__
#define __OMAP_I2S_H__

/* Source clocks for McBSP sample rate generator */
enum omap_mcbsp_clksrg_clk {
	OMAP_MCBSP_SYSCLK_CLKS_FCLK,	/* Internal FCLK */
	OMAP_MCBSP_SYSCLK_CLKS_EXT,	/* External CLKS pin */
	OMAP_MCBSP_SYSCLK_CLK,		/* Internal ICLK */
	OMAP_MCBSP_SYSCLK_CLKX_EXT,	/* External CLKX pin */
	OMAP_MCBSP_SYSCLK_CLKR_EXT,	/* External CLKR pin */
};

/* McBSP dividers */
enum omap_mcbsp_div {
	OMAP_MCBSP_CLKGDV,		/* Sample rate generator divider */
};

/*
 * REVISIT: Preparation for the ASoC v2. Let the number of available links to
 * be same than number of McBSP ports found in OMAP(s) we are compiling for.
 */
#define NUM_LINKS	1

extern struct snd_soc_cpu_dai omap_mcbsp_dai[NUM_LINKS];

#endif
