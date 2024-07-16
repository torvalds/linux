// SPDX-License-Identifier: GPL-2.0-only
/*
 * Rockchip PDM ALSA SoC Digital Audio Interface(DAI)  driver
 *
 * Copyright (C) 2017 Fuzhou Rockchip Electronics Co., Ltd
 */

#include <linux/module.h>
#include <linux/clk.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/pm_runtime.h>
#include <linux/rational.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <sound/dmaengine_pcm.h>
#include <sound/pcm_params.h>

#include "rockchip_pdm.h"

#define PDM_DMA_BURST_SIZE	(8) /* size * width: 8*4 = 32 bytes */
#define PDM_SIGNOFF_CLK_RATE	(100000000)
#define PDM_PATH_MAX		(4)

enum rk_pdm_version {
	RK_PDM_RK3229,
	RK_PDM_RK3308,
	RK_PDM_RV1126,
};

struct rk_pdm_dev {
	struct device *dev;
	struct clk *clk;
	struct clk *hclk;
	struct regmap *regmap;
	struct snd_dmaengine_dai_dma_data capture_dma_data;
	struct reset_control *reset;
	enum rk_pdm_version version;
};

struct rk_pdm_clkref {
	unsigned int sr;
	unsigned int clk;
	unsigned int clk_out;
};

struct rk_pdm_ds_ratio {
	unsigned int ratio;
	unsigned int sr;
};

static struct rk_pdm_clkref clkref[] = {
	{ 8000, 40960000, 2048000 },
	{ 11025, 56448000, 2822400 },
	{ 12000, 61440000, 3072000 },
	{ 8000, 98304000, 2048000 },
	{ 12000, 98304000, 3072000 },
};

static struct rk_pdm_ds_ratio ds_ratio[] = {
	{ 0, 192000 },
	{ 0, 176400 },
	{ 0, 128000 },
	{ 1, 96000 },
	{ 1, 88200 },
	{ 1, 64000 },
	{ 2, 48000 },
	{ 2, 44100 },
	{ 2, 32000 },
	{ 3, 24000 },
	{ 3, 22050 },
	{ 3, 16000 },
	{ 4, 12000 },
	{ 4, 11025 },
	{ 4, 8000 },
};

static unsigned int get_pdm_clk(struct rk_pdm_dev *pdm, unsigned int sr,
				unsigned int *clk_src, unsigned int *clk_out)
{
	unsigned int i, count, clk, div, rate;

	clk = 0;
	if (!sr)
		return clk;

	count = ARRAY_SIZE(clkref);
	for (i = 0; i < count; i++) {
		if (sr % clkref[i].sr)
			continue;
		div = sr / clkref[i].sr;
		if ((div & (div - 1)) == 0) {
			*clk_out = clkref[i].clk_out;
			rate = clk_round_rate(pdm->clk, clkref[i].clk);
			if (rate != clkref[i].clk)
				continue;
			clk = clkref[i].clk;
			*clk_src = clkref[i].clk;
			break;
		}
	}

	if (!clk) {
		clk = clk_round_rate(pdm->clk, PDM_SIGNOFF_CLK_RATE);
		*clk_src = clk;
	}
	return clk;
}

static unsigned int get_pdm_ds_ratio(unsigned int sr)
{
	unsigned int i, count, ratio;

	ratio = 0;
	if (!sr)
		return ratio;

	count = ARRAY_SIZE(ds_ratio);
	for (i = 0; i < count; i++) {
		if (sr == ds_ratio[i].sr)
			ratio = ds_ratio[i].ratio;
	}
	return ratio;
}

static unsigned int get_pdm_cic_ratio(unsigned int clk)
{
	switch (clk) {
	case 4096000:
	case 5644800:
	case 6144000:
		return 0;
	case 2048000:
	case 2822400:
	case 3072000:
		return 1;
	case 1024000:
	case 1411200:
	case 1536000:
		return 2;
	default:
		return 1;
	}
}

static unsigned int samplerate_to_bit(unsigned int samplerate)
{
	switch (samplerate) {
	case 8000:
	case 11025:
	case 12000:
		return 0;
	case 16000:
	case 22050:
	case 24000:
		return 1;
	case 32000:
		return 2;
	case 44100:
	case 48000:
		return 3;
	case 64000:
	case 88200:
	case 96000:
		return 4;
	case 128000:
	case 176400:
	case 192000:
		return 5;
	default:
		return 1;
	}
}

static inline struct rk_pdm_dev *to_info(struct snd_soc_dai *dai)
{
	return snd_soc_dai_get_drvdata(dai);
}

static void rockchip_pdm_rxctrl(struct rk_pdm_dev *pdm, int on)
{
	if (on) {
		regmap_update_bits(pdm->regmap, PDM_DMA_CTRL,
				   PDM_DMA_RD_MSK, PDM_DMA_RD_EN);
		regmap_update_bits(pdm->regmap, PDM_SYSCONFIG,
				   PDM_RX_MASK, PDM_RX_START);
	} else {
		regmap_update_bits(pdm->regmap, PDM_DMA_CTRL,
				   PDM_DMA_RD_MSK, PDM_DMA_RD_DIS);
		regmap_update_bits(pdm->regmap, PDM_SYSCONFIG,
				   PDM_RX_MASK | PDM_RX_CLR_MASK,
				   PDM_RX_STOP | PDM_RX_CLR_WR);
	}
}

static int rockchip_pdm_hw_params(struct snd_pcm_substream *substream,
				  struct snd_pcm_hw_params *params,
				  struct snd_soc_dai *dai)
{
	struct rk_pdm_dev *pdm = to_info(dai);
	unsigned int val = 0;
	unsigned int clk_rate, clk_div, samplerate;
	unsigned int clk_src, clk_out = 0;
	unsigned long m, n;
	bool change;
	int ret;

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
		return 0;

	samplerate = params_rate(params);
	clk_rate = get_pdm_clk(pdm, samplerate, &clk_src, &clk_out);
	if (!clk_rate)
		return -EINVAL;

	ret = clk_set_rate(pdm->clk, clk_src);
	if (ret)
		return -EINVAL;

	if (pdm->version == RK_PDM_RK3308 ||
	    pdm->version == RK_PDM_RV1126) {
		rational_best_approximation(clk_out, clk_src,
					    GENMASK(16 - 1, 0),
					    GENMASK(16 - 1, 0),
					    &m, &n);

		val = (m << PDM_FD_NUMERATOR_SFT) |
			(n << PDM_FD_DENOMINATOR_SFT);
		regmap_update_bits_check(pdm->regmap, PDM_CTRL1,
					 PDM_FD_NUMERATOR_MSK |
					 PDM_FD_DENOMINATOR_MSK,
					 val, &change);
		if (change) {
			reset_control_assert(pdm->reset);
			reset_control_deassert(pdm->reset);
			rockchip_pdm_rxctrl(pdm, 0);
		}
		clk_div = n / m;
		if (clk_div >= 40)
			val = PDM_CLK_FD_RATIO_40;
		else if (clk_div <= 35)
			val = PDM_CLK_FD_RATIO_35;
		else
			return -EINVAL;
		regmap_update_bits(pdm->regmap, PDM_CLK_CTRL,
				   PDM_CLK_FD_RATIO_MSK,
				   val);
	}

	if (pdm->version == RK_PDM_RV1126) {
		val = get_pdm_cic_ratio(clk_out);
		regmap_update_bits(pdm->regmap, PDM_CLK_CTRL, PDM_CIC_RATIO_MSK, val);
		val = samplerate_to_bit(samplerate);
		regmap_update_bits(pdm->regmap, PDM_CTRL0,
				   PDM_SAMPLERATE_MSK, PDM_SAMPLERATE(val));
	} else {
		val = get_pdm_ds_ratio(samplerate);
		regmap_update_bits(pdm->regmap, PDM_CLK_CTRL, PDM_DS_RATIO_MSK, val);
	}

	regmap_update_bits(pdm->regmap, PDM_HPF_CTRL,
			   PDM_HPF_CF_MSK, PDM_HPF_60HZ);
	regmap_update_bits(pdm->regmap, PDM_HPF_CTRL,
			   PDM_HPF_LE | PDM_HPF_RE, PDM_HPF_LE | PDM_HPF_RE);
	regmap_update_bits(pdm->regmap, PDM_CLK_CTRL, PDM_CLK_EN, PDM_CLK_EN);
	if (pdm->version != RK_PDM_RK3229)
		regmap_update_bits(pdm->regmap, PDM_CTRL0,
				   PDM_MODE_MSK, PDM_MODE_LJ);

	val = 0;
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S8:
		val |= PDM_VDW(8);
		break;
	case SNDRV_PCM_FORMAT_S16_LE:
		val |= PDM_VDW(16);
		break;
	case SNDRV_PCM_FORMAT_S20_3LE:
		val |= PDM_VDW(20);
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		val |= PDM_VDW(24);
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		val |= PDM_VDW(32);
		break;
	default:
		return -EINVAL;
	}

	switch (params_channels(params)) {
	case 8:
		val |= PDM_PATH3_EN;
		fallthrough;
	case 6:
		val |= PDM_PATH2_EN;
		fallthrough;
	case 4:
		val |= PDM_PATH1_EN;
		fallthrough;
	case 2:
		val |= PDM_PATH0_EN;
		break;
	default:
		dev_err(pdm->dev, "invalid channel: %d\n",
			params_channels(params));
		return -EINVAL;
	}

	regmap_update_bits(pdm->regmap, PDM_CTRL0,
			   PDM_PATH_MSK | PDM_VDW_MSK,
			   val);
	/* all channels share the single FIFO */
	regmap_update_bits(pdm->regmap, PDM_DMA_CTRL, PDM_DMA_RDL_MSK,
			   PDM_DMA_RDL(8 * params_channels(params)));

	return 0;
}

static int rockchip_pdm_set_fmt(struct snd_soc_dai *cpu_dai,
				unsigned int fmt)
{
	struct rk_pdm_dev *pdm = to_info(cpu_dai);
	unsigned int mask = 0, val = 0;

	mask = PDM_CKP_MSK;
	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		val = PDM_CKP_NORMAL;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		val = PDM_CKP_INVERTED;
		break;
	default:
		return -EINVAL;
	}

	pm_runtime_get_sync(cpu_dai->dev);
	regmap_update_bits(pdm->regmap, PDM_CLK_CTRL, mask, val);
	pm_runtime_put(cpu_dai->dev);

	return 0;
}

static int rockchip_pdm_trigger(struct snd_pcm_substream *substream, int cmd,
				struct snd_soc_dai *dai)
{
	struct rk_pdm_dev *pdm = to_info(dai);
	int ret = 0;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
			rockchip_pdm_rxctrl(pdm, 1);
		break;
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
			rockchip_pdm_rxctrl(pdm, 0);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int rockchip_pdm_dai_probe(struct snd_soc_dai *dai)
{
	struct rk_pdm_dev *pdm = to_info(dai);

	dai->capture_dma_data = &pdm->capture_dma_data;

	return 0;
}

static const struct snd_soc_dai_ops rockchip_pdm_dai_ops = {
	.set_fmt = rockchip_pdm_set_fmt,
	.trigger = rockchip_pdm_trigger,
	.hw_params = rockchip_pdm_hw_params,
};

#define ROCKCHIP_PDM_RATES SNDRV_PCM_RATE_8000_192000
#define ROCKCHIP_PDM_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | \
			      SNDRV_PCM_FMTBIT_S20_3LE | \
			      SNDRV_PCM_FMTBIT_S24_LE | \
			      SNDRV_PCM_FMTBIT_S32_LE)

static struct snd_soc_dai_driver rockchip_pdm_dai = {
	.probe = rockchip_pdm_dai_probe,
	.capture = {
		.stream_name = "Capture",
		.channels_min = 2,
		.channels_max = 8,
		.rates = ROCKCHIP_PDM_RATES,
		.formats = ROCKCHIP_PDM_FORMATS,
	},
	.ops = &rockchip_pdm_dai_ops,
	.symmetric_rate = 1,
};

static const struct snd_soc_component_driver rockchip_pdm_component = {
	.name = "rockchip-pdm",
	.legacy_dai_naming = 1,
};

static int rockchip_pdm_runtime_suspend(struct device *dev)
{
	struct rk_pdm_dev *pdm = dev_get_drvdata(dev);

	clk_disable_unprepare(pdm->clk);
	clk_disable_unprepare(pdm->hclk);

	return 0;
}

static int rockchip_pdm_runtime_resume(struct device *dev)
{
	struct rk_pdm_dev *pdm = dev_get_drvdata(dev);
	int ret;

	ret = clk_prepare_enable(pdm->clk);
	if (ret) {
		dev_err(pdm->dev, "clock enable failed %d\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(pdm->hclk);
	if (ret) {
		clk_disable_unprepare(pdm->clk);
		dev_err(pdm->dev, "hclock enable failed %d\n", ret);
		return ret;
	}

	return 0;
}

static bool rockchip_pdm_wr_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case PDM_SYSCONFIG:
	case PDM_CTRL0:
	case PDM_CTRL1:
	case PDM_CLK_CTRL:
	case PDM_HPF_CTRL:
	case PDM_FIFO_CTRL:
	case PDM_DMA_CTRL:
	case PDM_INT_EN:
	case PDM_INT_CLR:
	case PDM_DATA_VALID:
		return true;
	default:
		return false;
	}
}

static bool rockchip_pdm_rd_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case PDM_SYSCONFIG:
	case PDM_CTRL0:
	case PDM_CTRL1:
	case PDM_CLK_CTRL:
	case PDM_HPF_CTRL:
	case PDM_FIFO_CTRL:
	case PDM_DMA_CTRL:
	case PDM_INT_EN:
	case PDM_INT_CLR:
	case PDM_INT_ST:
	case PDM_DATA_VALID:
	case PDM_RXFIFO_DATA:
	case PDM_VERSION:
		return true;
	default:
		return false;
	}
}

static bool rockchip_pdm_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case PDM_SYSCONFIG:
	case PDM_FIFO_CTRL:
	case PDM_INT_CLR:
	case PDM_INT_ST:
	case PDM_RXFIFO_DATA:
		return true;
	default:
		return false;
	}
}

static bool rockchip_pdm_precious_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case PDM_RXFIFO_DATA:
		return true;
	default:
		return false;
	}
}

static const struct reg_default rockchip_pdm_reg_defaults[] = {
	{ PDM_CTRL0, 0x78000017 },
	{ PDM_CTRL1, 0x0bb8ea60 },
	{ PDM_CLK_CTRL, 0x0000e401 },
	{ PDM_DMA_CTRL, 0x0000001f },
};

static const struct regmap_config rockchip_pdm_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = PDM_VERSION,
	.reg_defaults = rockchip_pdm_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(rockchip_pdm_reg_defaults),
	.writeable_reg = rockchip_pdm_wr_reg,
	.readable_reg = rockchip_pdm_rd_reg,
	.volatile_reg = rockchip_pdm_volatile_reg,
	.precious_reg = rockchip_pdm_precious_reg,
	.cache_type = REGCACHE_FLAT,
};

static const struct of_device_id rockchip_pdm_match[] __maybe_unused = {
	{ .compatible = "rockchip,pdm",
	  .data = (void *)RK_PDM_RK3229 },
	{ .compatible = "rockchip,px30-pdm",
	  .data = (void *)RK_PDM_RK3308 },
	{ .compatible = "rockchip,rk1808-pdm",
	  .data = (void *)RK_PDM_RK3308 },
	{ .compatible = "rockchip,rk3308-pdm",
	  .data = (void *)RK_PDM_RK3308 },
	{ .compatible = "rockchip,rk3568-pdm",
	  .data = (void *)RK_PDM_RV1126 },
	{ .compatible = "rockchip,rv1126-pdm",
	  .data = (void *)RK_PDM_RV1126 },
	{},
};
MODULE_DEVICE_TABLE(of, rockchip_pdm_match);

static int rockchip_pdm_path_parse(struct rk_pdm_dev *pdm, struct device_node *node)
{
	unsigned int path[PDM_PATH_MAX];
	int cnt = 0, ret = 0, i = 0, val = 0, msk = 0;

	cnt = of_count_phandle_with_args(node, "rockchip,path-map",
					 NULL);
	if (cnt != PDM_PATH_MAX)
		return cnt;

	ret = of_property_read_u32_array(node, "rockchip,path-map",
					 path, cnt);
	if (ret)
		return ret;

	for (i = 0; i < cnt; i++) {
		if (path[i] >= PDM_PATH_MAX)
			return -EINVAL;
		msk |= PDM_PATH_MASK(i);
		val |= PDM_PATH(i, path[i]);
	}

	regmap_update_bits(pdm->regmap, PDM_CLK_CTRL, msk, val);

	return 0;
}

static int rockchip_pdm_probe(struct platform_device *pdev)
{
	struct device_node *node = pdev->dev.of_node;
	const struct of_device_id *match;
	struct rk_pdm_dev *pdm;
	struct resource *res;
	void __iomem *regs;
	int ret;

	pdm = devm_kzalloc(&pdev->dev, sizeof(*pdm), GFP_KERNEL);
	if (!pdm)
		return -ENOMEM;

	match = of_match_device(rockchip_pdm_match, &pdev->dev);
	if (match)
		pdm->version = (enum rk_pdm_version)match->data;

	if (pdm->version == RK_PDM_RK3308) {
		pdm->reset = devm_reset_control_get(&pdev->dev, "pdm-m");
		if (IS_ERR(pdm->reset))
			return PTR_ERR(pdm->reset);
	}

	regs = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	pdm->regmap = devm_regmap_init_mmio(&pdev->dev, regs,
					    &rockchip_pdm_regmap_config);
	if (IS_ERR(pdm->regmap))
		return PTR_ERR(pdm->regmap);

	pdm->capture_dma_data.addr = res->start + PDM_RXFIFO_DATA;
	pdm->capture_dma_data.addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	pdm->capture_dma_data.maxburst = PDM_DMA_BURST_SIZE;

	pdm->dev = &pdev->dev;
	dev_set_drvdata(&pdev->dev, pdm);

	pdm->clk = devm_clk_get(&pdev->dev, "pdm_clk");
	if (IS_ERR(pdm->clk))
		return PTR_ERR(pdm->clk);

	pdm->hclk = devm_clk_get(&pdev->dev, "pdm_hclk");
	if (IS_ERR(pdm->hclk))
		return PTR_ERR(pdm->hclk);

	ret = clk_prepare_enable(pdm->hclk);
	if (ret)
		return ret;

	pm_runtime_enable(&pdev->dev);
	if (!pm_runtime_enabled(&pdev->dev)) {
		ret = rockchip_pdm_runtime_resume(&pdev->dev);
		if (ret)
			goto err_pm_disable;
	}

	ret = devm_snd_soc_register_component(&pdev->dev,
					      &rockchip_pdm_component,
					      &rockchip_pdm_dai, 1);

	if (ret) {
		dev_err(&pdev->dev, "could not register dai: %d\n", ret);
		goto err_suspend;
	}

	rockchip_pdm_rxctrl(pdm, 0);

	ret = rockchip_pdm_path_parse(pdm, node);
	if (ret != 0 && ret != -ENOENT)
		goto err_suspend;

	ret = devm_snd_dmaengine_pcm_register(&pdev->dev, NULL, 0);
	if (ret) {
		dev_err(&pdev->dev, "could not register pcm: %d\n", ret);
		goto err_suspend;
	}

	return 0;

err_suspend:
	if (!pm_runtime_status_suspended(&pdev->dev))
		rockchip_pdm_runtime_suspend(&pdev->dev);
err_pm_disable:
	pm_runtime_disable(&pdev->dev);

	clk_disable_unprepare(pdm->hclk);

	return ret;
}

static int rockchip_pdm_remove(struct platform_device *pdev)
{
	struct rk_pdm_dev *pdm = dev_get_drvdata(&pdev->dev);

	pm_runtime_disable(&pdev->dev);
	if (!pm_runtime_status_suspended(&pdev->dev))
		rockchip_pdm_runtime_suspend(&pdev->dev);

	clk_disable_unprepare(pdm->clk);
	clk_disable_unprepare(pdm->hclk);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int rockchip_pdm_suspend(struct device *dev)
{
	struct rk_pdm_dev *pdm = dev_get_drvdata(dev);

	regcache_mark_dirty(pdm->regmap);

	return 0;
}

static int rockchip_pdm_resume(struct device *dev)
{
	struct rk_pdm_dev *pdm = dev_get_drvdata(dev);
	int ret;

	ret = pm_runtime_resume_and_get(dev);
	if (ret < 0)
		return ret;

	ret = regcache_sync(pdm->regmap);

	pm_runtime_put(dev);

	return ret;
}
#endif

static const struct dev_pm_ops rockchip_pdm_pm_ops = {
	SET_RUNTIME_PM_OPS(rockchip_pdm_runtime_suspend,
			   rockchip_pdm_runtime_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(rockchip_pdm_suspend, rockchip_pdm_resume)
};

static struct platform_driver rockchip_pdm_driver = {
	.probe  = rockchip_pdm_probe,
	.remove = rockchip_pdm_remove,
	.driver = {
		.name = "rockchip-pdm",
		.of_match_table = of_match_ptr(rockchip_pdm_match),
		.pm = &rockchip_pdm_pm_ops,
	},
};

module_platform_driver(rockchip_pdm_driver);

MODULE_AUTHOR("Sugar <sugar.zhang@rock-chips.com>");
MODULE_DESCRIPTION("Rockchip PDM Controller Driver");
MODULE_LICENSE("GPL v2");
