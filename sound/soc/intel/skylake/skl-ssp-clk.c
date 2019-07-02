// SPDX-License-Identifier: GPL-2.0
// Copyright(c) 2015-17 Intel Corporation

/*
 *  skl-ssp-clk.c - ASoC skylake ssp clock driver
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/clk-provider.h>
#include <linux/clkdev.h>
#include "skl.h"
#include "skl-ssp-clk.h"
#include "skl-topology.h"

#define to_skl_clk(_hw)	container_of(_hw, struct skl_clk, hw)

struct skl_clk_parent {
	struct clk_hw *hw;
	struct clk_lookup *lookup;
};

struct skl_clk {
	struct clk_hw hw;
	struct clk_lookup *lookup;
	unsigned long rate;
	struct skl_clk_pdata *pdata;
	u32 id;
};

struct skl_clk_data {
	struct skl_clk_parent parent[SKL_MAX_CLK_SRC];
	struct skl_clk *clk[SKL_MAX_CLK_CNT];
	u8 avail_clk_cnt;
};

static int skl_get_clk_type(u32 index)
{
	switch (index) {
	case 0 ... (SKL_SCLK_OFS - 1):
		return SKL_MCLK;

	case SKL_SCLK_OFS ... (SKL_SCLKFS_OFS - 1):
		return SKL_SCLK;

	case SKL_SCLKFS_OFS ... (SKL_MAX_CLK_CNT - 1):
		return SKL_SCLK_FS;

	default:
		return -EINVAL;
	}
}

static int skl_get_vbus_id(u32 index, u8 clk_type)
{
	switch (clk_type) {
	case SKL_MCLK:
		return index;

	case SKL_SCLK:
		return index - SKL_SCLK_OFS;

	case SKL_SCLK_FS:
		return index - SKL_SCLKFS_OFS;

	default:
		return -EINVAL;
	}
}

static void skl_fill_clk_ipc(struct skl_clk_rate_cfg_table *rcfg, u8 clk_type)
{
	struct nhlt_fmt_cfg *fmt_cfg;
	union skl_clk_ctrl_ipc *ipc;
	struct wav_fmt *wfmt;

	if (!rcfg)
		return;

	ipc = &rcfg->dma_ctl_ipc;
	if (clk_type == SKL_SCLK_FS) {
		fmt_cfg = (struct nhlt_fmt_cfg *)rcfg->config;
		wfmt = &fmt_cfg->fmt_ext.fmt;

		/* Remove TLV Header size */
		ipc->sclk_fs.hdr.size = sizeof(struct skl_dmactrl_sclkfs_cfg) -
						sizeof(struct skl_tlv_hdr);
		ipc->sclk_fs.sampling_frequency = wfmt->samples_per_sec;
		ipc->sclk_fs.bit_depth = wfmt->bits_per_sample;
		ipc->sclk_fs.valid_bit_depth =
			fmt_cfg->fmt_ext.sample.valid_bits_per_sample;
		ipc->sclk_fs.number_of_channels = wfmt->channels;
	} else {
		ipc->mclk.hdr.type = DMA_CLK_CONTROLS;
		/* Remove TLV Header size */
		ipc->mclk.hdr.size = sizeof(struct skl_dmactrl_mclk_cfg) -
						sizeof(struct skl_tlv_hdr);
	}
}

/* Sends dma control IPC to turn the clock ON/OFF */
static int skl_send_clk_dma_control(struct skl *skl,
				struct skl_clk_rate_cfg_table *rcfg,
				u32 vbus_id, u8 clk_type,
				bool enable)
{
	struct nhlt_specific_cfg *sp_cfg;
	u32 i2s_config_size, node_id = 0;
	struct nhlt_fmt_cfg *fmt_cfg;
	union skl_clk_ctrl_ipc *ipc;
	void *i2s_config = NULL;
	u8 *data, size;
	int ret;

	if (!rcfg)
		return -EIO;

	ipc = &rcfg->dma_ctl_ipc;
	fmt_cfg = (struct nhlt_fmt_cfg *)rcfg->config;
	sp_cfg = &fmt_cfg->config;

	if (clk_type == SKL_SCLK_FS) {
		ipc->sclk_fs.hdr.type =
			enable ? DMA_TRANSMITION_START : DMA_TRANSMITION_STOP;
		data = (u8 *)&ipc->sclk_fs;
		size = sizeof(struct skl_dmactrl_sclkfs_cfg);
	} else {
		/* 1 to enable mclk, 0 to enable sclk */
		if (clk_type == SKL_SCLK)
			ipc->mclk.mclk = 0;
		else
			ipc->mclk.mclk = 1;

		ipc->mclk.keep_running = enable;
		ipc->mclk.warm_up_over = enable;
		ipc->mclk.clk_stop_over = !enable;
		data = (u8 *)&ipc->mclk;
		size = sizeof(struct skl_dmactrl_mclk_cfg);
	}

	i2s_config_size = sp_cfg->size + size;
	i2s_config = kzalloc(i2s_config_size, GFP_KERNEL);
	if (!i2s_config)
		return -ENOMEM;

	/* copy blob */
	memcpy(i2s_config, sp_cfg->caps, sp_cfg->size);

	/* copy additional dma controls information */
	memcpy(i2s_config + sp_cfg->size, data, size);

	node_id = ((SKL_DMA_I2S_LINK_INPUT_CLASS << 8) | (vbus_id << 4));
	ret = skl_dsp_set_dma_control(skl->skl_sst, (u32 *)i2s_config,
					i2s_config_size, node_id);
	kfree(i2s_config);

	return ret;
}

static struct skl_clk_rate_cfg_table *skl_get_rate_cfg(
		struct skl_clk_rate_cfg_table *rcfg,
				unsigned long rate)
{
	int i;

	for (i = 0; (i < SKL_MAX_CLK_RATES) && rcfg[i].rate; i++) {
		if (rcfg[i].rate == rate)
			return &rcfg[i];
	}

	return NULL;
}

static int skl_clk_change_status(struct skl_clk *clkdev,
				bool enable)
{
	struct skl_clk_rate_cfg_table *rcfg;
	int vbus_id, clk_type;

	clk_type = skl_get_clk_type(clkdev->id);
	if (clk_type < 0)
		return clk_type;

	vbus_id = skl_get_vbus_id(clkdev->id, clk_type);
	if (vbus_id < 0)
		return vbus_id;

	rcfg = skl_get_rate_cfg(clkdev->pdata->ssp_clks[clkdev->id].rate_cfg,
						clkdev->rate);
	if (!rcfg)
		return -EINVAL;

	return skl_send_clk_dma_control(clkdev->pdata->pvt_data, rcfg,
					vbus_id, clk_type, enable);
}

static int skl_clk_prepare(struct clk_hw *hw)
{
	struct skl_clk *clkdev = to_skl_clk(hw);

	return skl_clk_change_status(clkdev, true);
}

static void skl_clk_unprepare(struct clk_hw *hw)
{
	struct skl_clk *clkdev = to_skl_clk(hw);

	skl_clk_change_status(clkdev, false);
}

static int skl_clk_set_rate(struct clk_hw *hw, unsigned long rate,
					unsigned long parent_rate)
{
	struct skl_clk *clkdev = to_skl_clk(hw);
	struct skl_clk_rate_cfg_table *rcfg;
	int clk_type;

	if (!rate)
		return -EINVAL;

	rcfg = skl_get_rate_cfg(clkdev->pdata->ssp_clks[clkdev->id].rate_cfg,
							rate);
	if (!rcfg)
		return -EINVAL;

	clk_type = skl_get_clk_type(clkdev->id);
	if (clk_type < 0)
		return clk_type;

	skl_fill_clk_ipc(rcfg, clk_type);
	clkdev->rate = rate;

	return 0;
}

static unsigned long skl_clk_recalc_rate(struct clk_hw *hw,
				unsigned long parent_rate)
{
	struct skl_clk *clkdev = to_skl_clk(hw);

	if (clkdev->rate)
		return clkdev->rate;

	return 0;
}

/* Not supported by clk driver. Implemented to satisfy clk fw */
static long skl_clk_round_rate(struct clk_hw *hw, unsigned long rate,
			       unsigned long *parent_rate)
{
	return rate;
}

/*
 * prepare/unprepare are used instead of enable/disable as IPC will be sent
 * in non-atomic context.
 */
static const struct clk_ops skl_clk_ops = {
	.prepare = skl_clk_prepare,
	.unprepare = skl_clk_unprepare,
	.set_rate = skl_clk_set_rate,
	.round_rate = skl_clk_round_rate,
	.recalc_rate = skl_clk_recalc_rate,
};

static void unregister_parent_src_clk(struct skl_clk_parent *pclk,
					unsigned int id)
{
	while (id--) {
		clkdev_drop(pclk[id].lookup);
		clk_hw_unregister_fixed_rate(pclk[id].hw);
	}
}

static void unregister_src_clk(struct skl_clk_data *dclk)
{
	while (dclk->avail_clk_cnt--)
		clkdev_drop(dclk->clk[dclk->avail_clk_cnt]->lookup);
}

static int skl_register_parent_clks(struct device *dev,
			struct skl_clk_parent *parent,
			struct skl_clk_parent_src *pclk)
{
	int i, ret;

	for (i = 0; i < SKL_MAX_CLK_SRC; i++) {

		/* Register Parent clock */
		parent[i].hw = clk_hw_register_fixed_rate(dev, pclk[i].name,
				pclk[i].parent_name, 0, pclk[i].rate);
		if (IS_ERR(parent[i].hw)) {
			ret = PTR_ERR(parent[i].hw);
			goto err;
		}

		parent[i].lookup = clkdev_hw_create(parent[i].hw, pclk[i].name,
									NULL);
		if (!parent[i].lookup) {
			clk_hw_unregister_fixed_rate(parent[i].hw);
			ret = -ENOMEM;
			goto err;
		}
	}

	return 0;
err:
	unregister_parent_src_clk(parent, i);
	return ret;
}

/* Assign fmt_config to clk_data */
static struct skl_clk *register_skl_clk(struct device *dev,
			struct skl_ssp_clk *clk,
			struct skl_clk_pdata *clk_pdata, int id)
{
	struct clk_init_data init;
	struct skl_clk *clkdev;
	int ret;

	clkdev = devm_kzalloc(dev, sizeof(*clkdev), GFP_KERNEL);
	if (!clkdev)
		return ERR_PTR(-ENOMEM);

	init.name = clk->name;
	init.ops = &skl_clk_ops;
	init.flags = CLK_SET_RATE_GATE;
	init.parent_names = &clk->parent_name;
	init.num_parents = 1;
	clkdev->hw.init = &init;
	clkdev->pdata = clk_pdata;

	clkdev->id = id;
	ret = devm_clk_hw_register(dev, &clkdev->hw);
	if (ret) {
		clkdev = ERR_PTR(ret);
		return clkdev;
	}

	clkdev->lookup = clkdev_hw_create(&clkdev->hw, init.name, NULL);
	if (!clkdev->lookup)
		clkdev = ERR_PTR(-ENOMEM);

	return clkdev;
}

static int skl_clk_dev_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device *parent_dev = dev->parent;
	struct skl_clk_parent_src *parent_clks;
	struct skl_clk_pdata *clk_pdata;
	struct skl_clk_data *data;
	struct skl_ssp_clk *clks;
	int ret, i;

	clk_pdata = dev_get_platdata(&pdev->dev);
	parent_clks = clk_pdata->parent_clks;
	clks = clk_pdata->ssp_clks;
	if (!parent_clks || !clks)
		return -EIO;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	/* Register Parent clock */
	ret = skl_register_parent_clks(parent_dev, data->parent, parent_clks);
	if (ret < 0)
		return ret;

	for (i = 0; i < clk_pdata->num_clks; i++) {
		/*
		 * Only register valid clocks
		 * i.e. for which nhlt entry is present.
		 */
		if (clks[i].rate_cfg[0].rate == 0)
			continue;

		data->clk[data->avail_clk_cnt] = register_skl_clk(dev,
				&clks[i], clk_pdata, i);

		if (IS_ERR(data->clk[data->avail_clk_cnt])) {
			ret = PTR_ERR(data->clk[data->avail_clk_cnt++]);
			goto err_unreg_skl_clk;
		}
	}

	platform_set_drvdata(pdev, data);

	return 0;

err_unreg_skl_clk:
	unregister_src_clk(data);
	unregister_parent_src_clk(data->parent, SKL_MAX_CLK_SRC);

	return ret;
}

static int skl_clk_dev_remove(struct platform_device *pdev)
{
	struct skl_clk_data *data;

	data = platform_get_drvdata(pdev);
	unregister_src_clk(data);
	unregister_parent_src_clk(data->parent, SKL_MAX_CLK_SRC);

	return 0;
}

static struct platform_driver skl_clk_driver = {
	.driver = {
		.name = "skl-ssp-clk",
	},
	.probe = skl_clk_dev_probe,
	.remove = skl_clk_dev_remove,
};

module_platform_driver(skl_clk_driver);

MODULE_DESCRIPTION("Skylake clock driver");
MODULE_AUTHOR("Jaikrishna Nemallapudi <jaikrishnax.nemallapudi@intel.com>");
MODULE_AUTHOR("Subhransu S. Prusty <subhransu.s.prusty@intel.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:skl-ssp-clk");
