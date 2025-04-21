/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2017-2021 NXP
 */

#ifndef __FSL_RPMSG_H
#define __FSL_RPMSG_H

/*
 * struct fsl_rpmsg_soc_data
 * @rates: supported rates
 * @formats: supported formats
 */
struct fsl_rpmsg_soc_data {
	int rates;
	u64 formats;
};

/*
 * struct fsl_rpmsg - rpmsg private data
 *
 * @ipg: ipg clock for cpu dai (SAI)
 * @mclk: master clock for cpu dai (SAI)
 * @dma: clock for dma device
 * @pll8k: parent clock for multiple of 8kHz frequency
 * @pll11k: parent clock for multiple of 11kHz frequency
 * @card_pdev: Platform_device pointer to register a sound card
 * @soc_data: soc specific data
 * @mclk_streams: Active streams that are using baudclk
 * @force_lpa: force enable low power audio routine if condition satisfy
 * @enable_lpa: enable low power audio routine according to dts setting
 * @buffer_size: pre allocated dma buffer size
 */
struct fsl_rpmsg {
	struct clk *ipg;
	struct clk *mclk;
	struct clk *dma;
	struct clk *pll8k;
	struct clk *pll11k;
	struct platform_device *card_pdev;
	const struct fsl_rpmsg_soc_data *soc_data;
	unsigned int mclk_streams;
	int force_lpa;
	int enable_lpa;
	int buffer_size[2];
};
#endif /* __FSL_RPMSG_H */
