/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Freescale ALSA SoC Machine driver utility
 *
 * Author: Timur Tabi <timur@freescale.com>
 *
 * Copyright 2010 Freescale Semiconductor, Inc.
 */

#ifndef _FSL_UTILS_H
#define _FSL_UTILS_H

#define DAI_NAME_SIZE	32

struct snd_soc_dai_link;
struct device_node;

int fsl_asoc_get_dma_channel(struct device_node *ssi_np, const char *name,
			     struct snd_soc_dai_link *dai,
			     unsigned int *dma_channel_id,
			     unsigned int *dma_id);

void fsl_asoc_get_pll_clocks(struct device *dev, struct clk **pll8k_clk,
			     struct clk **pll11k_clk);

void fsl_asoc_reparent_pll_clocks(struct device *dev, struct clk *clk,
				  struct clk *pll8k_clk,
				  struct clk *pll11k_clk, u64 ratio);
#endif /* _FSL_UTILS_H */
