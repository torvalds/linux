// SPDX-License-Identifier: GPL-2.0
//
// Freescale ALSA SoC Machine driver utility
//
// Author: Timur Tabi <timur@freescale.com>
//
// Copyright 2010 Freescale Semiconductor, Inc.

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <sound/soc.h>

#include "fsl_utils.h"

/**
 * fsl_asoc_get_dma_channel - determine the dma channel for a SSI node
 *
 * @ssi_np: pointer to the SSI device tree node
 * @name: name of the phandle pointing to the dma channel
 * @dai: ASoC DAI link pointer to be filled with platform_name
 * @dma_channel_id: dma channel id to be returned
 * @dma_id: dma id to be returned
 *
 * This function determines the dma and channel id for given SSI node.  It
 * also discovers the platform_name for the ASoC DAI link.
 */
int fsl_asoc_get_dma_channel(struct device_node *ssi_np,
			     const char *name,
			     struct snd_soc_dai_link *dai,
			     unsigned int *dma_channel_id,
			     unsigned int *dma_id)
{
	struct resource res;
	struct device_node *dma_channel_np, *dma_np;
	const __be32 *iprop;
	int ret;

	dma_channel_np = of_parse_phandle(ssi_np, name, 0);
	if (!dma_channel_np)
		return -EINVAL;

	if (!of_device_is_compatible(dma_channel_np, "fsl,ssi-dma-channel")) {
		of_node_put(dma_channel_np);
		return -EINVAL;
	}

	/* Determine the dev_name for the device_node.  This code mimics the
	 * behavior of of_device_make_bus_id(). We need this because ASoC uses
	 * the dev_name() of the device to match the platform (DMA) device with
	 * the CPU (SSI) device.  It's all ugly and hackish, but it works (for
	 * now).
	 *
	 * dai->platform name should already point to an allocated buffer.
	 */
	ret = of_address_to_resource(dma_channel_np, 0, &res);
	if (ret) {
		of_node_put(dma_channel_np);
		return ret;
	}
	snprintf((char *)dai->platforms->name, DAI_NAME_SIZE, "%llx.%pOFn",
		 (unsigned long long) res.start, dma_channel_np);

	iprop = of_get_property(dma_channel_np, "cell-index", NULL);
	if (!iprop) {
		of_node_put(dma_channel_np);
		return -EINVAL;
	}
	*dma_channel_id = be32_to_cpup(iprop);

	dma_np = of_get_parent(dma_channel_np);
	iprop = of_get_property(dma_np, "cell-index", NULL);
	if (!iprop) {
		of_node_put(dma_np);
		of_node_put(dma_channel_np);
		return -EINVAL;
	}
	*dma_id = be32_to_cpup(iprop);

	of_node_put(dma_np);
	of_node_put(dma_channel_np);

	return 0;
}
EXPORT_SYMBOL(fsl_asoc_get_dma_channel);

/**
 * fsl_asoc_get_pll_clocks - get two PLL clock source
 *
 * @dev: device pointer
 * @pll8k_clk: PLL clock pointer for 8kHz
 * @pll11k_clk: PLL clock pointer for 11kHz
 *
 * This function get two PLL clock source
 */
void fsl_asoc_get_pll_clocks(struct device *dev, struct clk **pll8k_clk,
			     struct clk **pll11k_clk)
{
	*pll8k_clk = devm_clk_get(dev, "pll8k");
	if (IS_ERR(*pll8k_clk))
		*pll8k_clk = NULL;

	*pll11k_clk = devm_clk_get(dev, "pll11k");
	if (IS_ERR(*pll11k_clk))
		*pll11k_clk = NULL;
}
EXPORT_SYMBOL(fsl_asoc_get_pll_clocks);

/**
 * fsl_asoc_reparent_pll_clocks - set clock parent if necessary
 *
 * @dev: device pointer
 * @clk: root clock pointer
 * @pll8k_clk: PLL clock pointer for 8kHz
 * @pll11k_clk: PLL clock pointer for 11kHz
 * @ratio: target requency for root clock
 *
 * This function set root clock parent according to the target ratio
 */
void fsl_asoc_reparent_pll_clocks(struct device *dev, struct clk *clk,
				  struct clk *pll8k_clk,
				  struct clk *pll11k_clk, u64 ratio)
{
	struct clk *p, *pll = NULL, *npll = NULL;
	bool reparent = false;
	int ret;

	if (!clk || !pll8k_clk || !pll11k_clk)
		return;

	p = clk;
	while (p && pll8k_clk && pll11k_clk) {
		struct clk *pp = clk_get_parent(p);

		if (clk_is_match(pp, pll8k_clk) ||
		    clk_is_match(pp, pll11k_clk)) {
			pll = pp;
			break;
		}
		p = pp;
	}

	npll = (do_div(ratio, 8000) ? pll11k_clk : pll8k_clk);
	reparent = (pll && !clk_is_match(pll, npll));

	if (reparent) {
		ret = clk_set_parent(p, npll);
		if (ret < 0)
			dev_warn(dev, "failed to set parent:%d\n", ret);
	}
}
EXPORT_SYMBOL(fsl_asoc_reparent_pll_clocks);

MODULE_AUTHOR("Timur Tabi <timur@freescale.com>");
MODULE_DESCRIPTION("Freescale ASoC utility code");
MODULE_LICENSE("GPL v2");
