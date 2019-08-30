/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * omap-mcbsp.h
 *
 * Copyright (C) 2008 Nokia Corporation
 *
 * Contact: Jarkko Nikula <jarkko.nikula@bitmer.com>
 *          Peter Ujfalusi <peter.ujfalusi@ti.com>
 */

#ifndef __OMAP_MCBSP_H__
#define __OMAP_MCBSP_H__

#include <sound/dmaengine_pcm.h>

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

int omap_mcbsp_st_add_controls(struct snd_soc_pcm_runtime *rtd, int port_id);

#endif /* __OMAP_MCBSP_H__ */
