/*
 * omap-mcbsp.h
 *
 * Copyright (C) 2008 Nokia Corporation
 *
 * Contact: Jarkko Nikula <jarkko.nikula@bitmer.com>
 *          Peter Ujfalusi <peter.ujfalusi@ti.com>
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
	OMAP_MCBSP_CLKR_SRC_CLKR,	/* CLKR from CLKR pin */
	OMAP_MCBSP_CLKR_SRC_CLKX,	/* CLKR from CLKX pin */
	OMAP_MCBSP_FSR_SRC_FSR,		/* FSR from FSR pin */
	OMAP_MCBSP_FSR_SRC_FSX,		/* FSR from FSX pin */
};

/* McBSP dividers */
enum omap_mcbsp_div {
	OMAP_MCBSP_CLKGDV,		/* Sample rate generator divider */
};

#if defined(CONFIG_SOC_OMAP2420)
#define NUM_LINKS	2
#endif
#if defined(CONFIG_ARCH_OMAP15XX) || defined(CONFIG_ARCH_OMAP16XX)
#undef  NUM_LINKS
#define NUM_LINKS	3
#endif
#if defined(CONFIG_ARCH_OMAP4)
#undef  NUM_LINKS
#define NUM_LINKS	4
#endif
#if defined(CONFIG_ARCH_OMAP3) || defined(CONFIG_SOC_OMAP2430)
#undef  NUM_LINKS
#define NUM_LINKS	5
#endif

int omap_mcbsp_st_add_controls(struct snd_soc_codec *codec, int mcbsp_id);

#endif
