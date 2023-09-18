// SPDX-License-Identifier: GPL-2.0-only
/*
 * IMG I2S output controller driver
 *
 * Copyright (C) 2015 Imagination Technologies Ltd.
 *
 * Author: Damien Horsley <Damien.Horsley@imgtec.com>
 */

#include <linux/clk.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>

#include <sound/core.h>
#include <sound/dmaengine_pcm.h>
#include <sound/initval.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

#define IMG_I2S_OUT_TX_FIFO			0x0

#define IMG_I2S_OUT_CTL				0x4
#define IMG_I2S_OUT_CTL_DATA_EN_MASK		BIT(24)
#define IMG_I2S_OUT_CTL_ACTIVE_CHAN_MASK	0xffe000
#define IMG_I2S_OUT_CTL_ACTIVE_CHAN_SHIFT	13
#define IMG_I2S_OUT_CTL_FRM_SIZE_MASK		BIT(8)
#define IMG_I2S_OUT_CTL_MASTER_MASK		BIT(6)
#define IMG_I2S_OUT_CTL_CLK_MASK		BIT(5)
#define IMG_I2S_OUT_CTL_CLK_EN_MASK		BIT(4)
#define IMG_I2S_OUT_CTL_FRM_CLK_POL_MASK	BIT(3)
#define IMG_I2S_OUT_CTL_BCLK_POL_MASK		BIT(2)
#define IMG_I2S_OUT_CTL_ME_MASK			BIT(0)

#define IMG_I2S_OUT_CH_CTL			0x4
#define IMG_I2S_OUT_CHAN_CTL_CH_MASK		BIT(11)
#define IMG_I2S_OUT_CHAN_CTL_LT_MASK		BIT(10)
#define IMG_I2S_OUT_CHAN_CTL_FMT_MASK		0xf0
#define IMG_I2S_OUT_CHAN_CTL_FMT_SHIFT		4
#define IMG_I2S_OUT_CHAN_CTL_JUST_MASK		BIT(3)
#define IMG_I2S_OUT_CHAN_CTL_CLKT_MASK		BIT(1)
#define IMG_I2S_OUT_CHAN_CTL_ME_MASK		BIT(0)

#define IMG_I2S_OUT_CH_STRIDE			0x20

struct img_i2s_out {
	void __iomem *base;
	struct clk *clk_sys;
	struct clk *clk_ref;
	struct snd_dmaengine_dai_dma_data dma_data;
	struct device *dev;
	unsigned int max_i2s_chan;
	void __iomem *channel_base;
	bool force_clk_active;
	unsigned int active_channels;
	struct reset_control *rst;
	struct snd_soc_dai_driver dai_driver;
	u32 suspend_ctl;
	u32 *suspend_ch_ctl;
};

static int img_i2s_out_runtime_suspend(struct device *dev)
{
	struct img_i2s_out *i2s = dev_get_drvdata(dev);

	clk_disable_unprepare(i2s->clk_ref);
	clk_disable_unprepare(i2s->clk_sys);

	return 0;
}

static int img_i2s_out_runtime_resume(struct device *dev)
{
	struct img_i2s_out *i2s = dev_get_drvdata(dev);
	int ret;

	ret = clk_prepare_enable(i2s->clk_sys);
	if (ret) {
		dev_err(dev, "clk_enable failed: %d\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(i2s->clk_ref);
	if (ret) {
		dev_err(dev, "clk_enable failed: %d\n", ret);
		clk_disable_unprepare(i2s->clk_sys);
		return ret;
	}

	return 0;
}

static inline void img_i2s_out_writel(struct img_i2s_out *i2s, u32 val,
					u32 reg)
{
	writel(val, i2s->base + reg);
}

static inline u32 img_i2s_out_readl(struct img_i2s_out *i2s, u32 reg)
{
	return readl(i2s->base + reg);
}

static inline void img_i2s_out_ch_writel(struct img_i2s_out *i2s,
					u32 chan, u32 val, u32 reg)
{
	writel(val, i2s->channel_base + (chan * IMG_I2S_OUT_CH_STRIDE) + reg);
}

static inline u32 img_i2s_out_ch_readl(struct img_i2s_out *i2s, u32 chan,
					u32 reg)
{
	return readl(i2s->channel_base + (chan * IMG_I2S_OUT_CH_STRIDE) + reg);
}

static inline void img_i2s_out_ch_disable(struct img_i2s_out *i2s, u32 chan)
{
	u32 reg;

	reg = img_i2s_out_ch_readl(i2s, chan, IMG_I2S_OUT_CH_CTL);
	reg &= ~IMG_I2S_OUT_CHAN_CTL_ME_MASK;
	img_i2s_out_ch_writel(i2s, chan, reg, IMG_I2S_OUT_CH_CTL);
}

static inline void img_i2s_out_ch_enable(struct img_i2s_out *i2s, u32 chan)
{
	u32 reg;

	reg = img_i2s_out_ch_readl(i2s, chan, IMG_I2S_OUT_CH_CTL);
	reg |= IMG_I2S_OUT_CHAN_CTL_ME_MASK;
	img_i2s_out_ch_writel(i2s, chan, reg, IMG_I2S_OUT_CH_CTL);
}

static inline void img_i2s_out_disable(struct img_i2s_out *i2s)
{
	u32 reg;

	reg = img_i2s_out_readl(i2s, IMG_I2S_OUT_CTL);
	reg &= ~IMG_I2S_OUT_CTL_ME_MASK;
	img_i2s_out_writel(i2s, reg, IMG_I2S_OUT_CTL);
}

static inline void img_i2s_out_enable(struct img_i2s_out *i2s)
{
	u32 reg;

	reg = img_i2s_out_readl(i2s, IMG_I2S_OUT_CTL);
	reg |= IMG_I2S_OUT_CTL_ME_MASK;
	img_i2s_out_writel(i2s, reg, IMG_I2S_OUT_CTL);
}

static void img_i2s_out_reset(struct img_i2s_out *i2s)
{
	int i;
	u32 core_ctl, chan_ctl;

	core_ctl = img_i2s_out_readl(i2s, IMG_I2S_OUT_CTL) &
			~IMG_I2S_OUT_CTL_ME_MASK &
			~IMG_I2S_OUT_CTL_DATA_EN_MASK;

	if (!i2s->force_clk_active)
		core_ctl &= ~IMG_I2S_OUT_CTL_CLK_EN_MASK;

	chan_ctl = img_i2s_out_ch_readl(i2s, 0, IMG_I2S_OUT_CH_CTL) &
			~IMG_I2S_OUT_CHAN_CTL_ME_MASK;

	reset_control_assert(i2s->rst);
	reset_control_deassert(i2s->rst);

	for (i = 0; i < i2s->max_i2s_chan; i++)
		img_i2s_out_ch_writel(i2s, i, chan_ctl, IMG_I2S_OUT_CH_CTL);

	for (i = 0; i < i2s->active_channels; i++)
		img_i2s_out_ch_enable(i2s, i);

	img_i2s_out_writel(i2s, core_ctl, IMG_I2S_OUT_CTL);
	img_i2s_out_enable(i2s);
}

static int img_i2s_out_trigger(struct snd_pcm_substream *substream, int cmd,
	struct snd_soc_dai *dai)
{
	struct img_i2s_out *i2s = snd_soc_dai_get_drvdata(dai);
	u32 reg;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		reg = img_i2s_out_readl(i2s, IMG_I2S_OUT_CTL);
		if (!i2s->force_clk_active)
			reg |= IMG_I2S_OUT_CTL_CLK_EN_MASK;
		reg |= IMG_I2S_OUT_CTL_DATA_EN_MASK;
		img_i2s_out_writel(i2s, reg, IMG_I2S_OUT_CTL);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		img_i2s_out_reset(i2s);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int img_i2s_out_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	struct img_i2s_out *i2s = snd_soc_dai_get_drvdata(dai);
	unsigned int channels, i2s_channels;
	long pre_div_a, pre_div_b, diff_a, diff_b, rate, clk_rate;
	int i;
	u32 reg, control_mask, control_set = 0;
	snd_pcm_format_t format;

	rate = params_rate(params);
	format = params_format(params);
	channels = params_channels(params);
	i2s_channels = channels / 2;

	if (format != SNDRV_PCM_FORMAT_S32_LE)
		return -EINVAL;

	if ((channels < 2) ||
	    (channels > (i2s->max_i2s_chan * 2)) ||
	    (channels % 2))
		return -EINVAL;

	pre_div_a = clk_round_rate(i2s->clk_ref, rate * 256);
	if (pre_div_a < 0)
		return pre_div_a;
	pre_div_b = clk_round_rate(i2s->clk_ref, rate * 384);
	if (pre_div_b < 0)
		return pre_div_b;

	diff_a = abs((pre_div_a / 256) - rate);
	diff_b = abs((pre_div_b / 384) - rate);

	/* If diffs are equal, use lower clock rate */
	if (diff_a > diff_b)
		clk_set_rate(i2s->clk_ref, pre_div_b);
	else
		clk_set_rate(i2s->clk_ref, pre_div_a);

	/*
	 * Another driver (eg alsa machine driver) may have rejected the above
	 * change. Get the current rate and set the register bit according to
	 * the new minimum diff
	 */
	clk_rate = clk_get_rate(i2s->clk_ref);

	diff_a = abs((clk_rate / 256) - rate);
	diff_b = abs((clk_rate / 384) - rate);

	if (diff_a > diff_b)
		control_set |= IMG_I2S_OUT_CTL_CLK_MASK;

	control_set |= ((i2s_channels - 1) <<
		       IMG_I2S_OUT_CTL_ACTIVE_CHAN_SHIFT) &
		       IMG_I2S_OUT_CTL_ACTIVE_CHAN_MASK;

	control_mask = IMG_I2S_OUT_CTL_CLK_MASK |
		       IMG_I2S_OUT_CTL_ACTIVE_CHAN_MASK;

	img_i2s_out_disable(i2s);

	reg = img_i2s_out_readl(i2s, IMG_I2S_OUT_CTL);
	reg = (reg & ~control_mask) | control_set;
	img_i2s_out_writel(i2s, reg, IMG_I2S_OUT_CTL);

	for (i = 0; i < i2s_channels; i++)
		img_i2s_out_ch_enable(i2s, i);

	for (; i < i2s->max_i2s_chan; i++)
		img_i2s_out_ch_disable(i2s, i);

	img_i2s_out_enable(i2s);

	i2s->active_channels = i2s_channels;

	return 0;
}

static int img_i2s_out_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct img_i2s_out *i2s = snd_soc_dai_get_drvdata(dai);
	int i, ret;
	bool force_clk_active;
	u32 chan_control_mask, control_mask, chan_control_set = 0;
	u32 reg, control_set = 0;

	force_clk_active = ((fmt & SND_SOC_DAIFMT_CLOCK_MASK) ==
			SND_SOC_DAIFMT_CONT);

	if (force_clk_active)
		control_set |= IMG_I2S_OUT_CTL_CLK_EN_MASK;

	switch (fmt & SND_SOC_DAIFMT_CLOCK_PROVIDER_MASK) {
	case SND_SOC_DAIFMT_BC_FC:
		break;
	case SND_SOC_DAIFMT_BP_FP:
		control_set |= IMG_I2S_OUT_CTL_MASTER_MASK;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		control_set |= IMG_I2S_OUT_CTL_BCLK_POL_MASK;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		control_set |= IMG_I2S_OUT_CTL_BCLK_POL_MASK;
		control_set |= IMG_I2S_OUT_CTL_FRM_CLK_POL_MASK;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		break;
	case SND_SOC_DAIFMT_IB_IF:
		control_set |= IMG_I2S_OUT_CTL_FRM_CLK_POL_MASK;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		chan_control_set |= IMG_I2S_OUT_CHAN_CTL_CLKT_MASK;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		break;
	default:
		return -EINVAL;
	}

	control_mask = IMG_I2S_OUT_CTL_CLK_EN_MASK |
		       IMG_I2S_OUT_CTL_MASTER_MASK |
		       IMG_I2S_OUT_CTL_BCLK_POL_MASK |
		       IMG_I2S_OUT_CTL_FRM_CLK_POL_MASK;

	chan_control_mask = IMG_I2S_OUT_CHAN_CTL_CLKT_MASK;

	ret = pm_runtime_resume_and_get(i2s->dev);
	if (ret < 0)
		return ret;

	img_i2s_out_disable(i2s);

	reg = img_i2s_out_readl(i2s, IMG_I2S_OUT_CTL);
	reg = (reg & ~control_mask) | control_set;
	img_i2s_out_writel(i2s, reg, IMG_I2S_OUT_CTL);

	for (i = 0; i < i2s->active_channels; i++)
		img_i2s_out_ch_disable(i2s, i);

	for (i = 0; i < i2s->max_i2s_chan; i++) {
		reg = img_i2s_out_ch_readl(i2s, i, IMG_I2S_OUT_CH_CTL);
		reg = (reg & ~chan_control_mask) | chan_control_set;
		img_i2s_out_ch_writel(i2s, i, reg, IMG_I2S_OUT_CH_CTL);
	}

	for (i = 0; i < i2s->active_channels; i++)
		img_i2s_out_ch_enable(i2s, i);

	img_i2s_out_enable(i2s);
	pm_runtime_put(i2s->dev);

	i2s->force_clk_active = force_clk_active;

	return 0;
}

static int img_i2s_out_dai_probe(struct snd_soc_dai *dai)
{
	struct img_i2s_out *i2s = snd_soc_dai_get_drvdata(dai);

	snd_soc_dai_init_dma_data(dai, &i2s->dma_data, NULL);

	return 0;
}

static const struct snd_soc_dai_ops img_i2s_out_dai_ops = {
	.probe		= img_i2s_out_dai_probe,
	.trigger	= img_i2s_out_trigger,
	.hw_params	= img_i2s_out_hw_params,
	.set_fmt	= img_i2s_out_set_fmt
};

static const struct snd_soc_component_driver img_i2s_out_component = {
	.name = "img-i2s-out",
	.legacy_dai_naming = 1,
};

static int img_i2s_out_dma_prepare_slave_config(struct snd_pcm_substream *st,
	struct snd_pcm_hw_params *params, struct dma_slave_config *sc)
{
	unsigned int i2s_channels = params_channels(params) / 2;
	struct snd_soc_pcm_runtime *rtd = st->private_data;
	struct snd_dmaengine_dai_dma_data *dma_data;
	int ret;

	dma_data = snd_soc_dai_get_dma_data(asoc_rtd_to_cpu(rtd, 0), st);

	ret = snd_hwparams_to_dma_slave_config(st, params, sc);
	if (ret)
		return ret;

	sc->dst_addr = dma_data->addr;
	sc->dst_addr_width = dma_data->addr_width;
	sc->dst_maxburst = 4 * i2s_channels;

	return 0;
}

static const struct snd_dmaengine_pcm_config img_i2s_out_dma_config = {
	.prepare_slave_config = img_i2s_out_dma_prepare_slave_config
};

static int img_i2s_out_probe(struct platform_device *pdev)
{
	struct img_i2s_out *i2s;
	struct resource *res;
	void __iomem *base;
	int i, ret;
	unsigned int max_i2s_chan_pow_2;
	u32 reg;
	struct device *dev = &pdev->dev;

	i2s = devm_kzalloc(&pdev->dev, sizeof(*i2s), GFP_KERNEL);
	if (!i2s)
		return -ENOMEM;

	platform_set_drvdata(pdev, i2s);

	i2s->dev = &pdev->dev;

	base = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	i2s->base = base;

	if (of_property_read_u32(pdev->dev.of_node, "img,i2s-channels",
			&i2s->max_i2s_chan)) {
		dev_err(&pdev->dev, "No img,i2s-channels property\n");
		return -EINVAL;
	}

	max_i2s_chan_pow_2 = 1 << get_count_order(i2s->max_i2s_chan);

	i2s->channel_base = base + (max_i2s_chan_pow_2 * 0x20);

	i2s->rst = devm_reset_control_get_exclusive(&pdev->dev, "rst");
	if (IS_ERR(i2s->rst))
		return dev_err_probe(&pdev->dev, PTR_ERR(i2s->rst),
				     "No top level reset found\n");

	i2s->clk_sys = devm_clk_get(&pdev->dev, "sys");
	if (IS_ERR(i2s->clk_sys))
		return dev_err_probe(dev, PTR_ERR(i2s->clk_sys),
				     "Failed to acquire clock 'sys'\n");

	i2s->clk_ref = devm_clk_get(&pdev->dev, "ref");
	if (IS_ERR(i2s->clk_ref))
		return dev_err_probe(dev, PTR_ERR(i2s->clk_ref),
				     "Failed to acquire clock 'ref'\n");

	i2s->suspend_ch_ctl = devm_kcalloc(dev,
		i2s->max_i2s_chan, sizeof(*i2s->suspend_ch_ctl), GFP_KERNEL);
	if (!i2s->suspend_ch_ctl)
		return -ENOMEM;

	pm_runtime_enable(&pdev->dev);
	if (!pm_runtime_enabled(&pdev->dev)) {
		ret = img_i2s_out_runtime_resume(&pdev->dev);
		if (ret)
			goto err_pm_disable;
	}
	ret = pm_runtime_resume_and_get(&pdev->dev);
	if (ret < 0)
		goto err_suspend;

	reg = IMG_I2S_OUT_CTL_FRM_SIZE_MASK;
	img_i2s_out_writel(i2s, reg, IMG_I2S_OUT_CTL);

	reg = IMG_I2S_OUT_CHAN_CTL_JUST_MASK |
		IMG_I2S_OUT_CHAN_CTL_LT_MASK |
		IMG_I2S_OUT_CHAN_CTL_CH_MASK |
		(8 << IMG_I2S_OUT_CHAN_CTL_FMT_SHIFT);

	for (i = 0; i < i2s->max_i2s_chan; i++)
		img_i2s_out_ch_writel(i2s, i, reg, IMG_I2S_OUT_CH_CTL);

	img_i2s_out_reset(i2s);
	pm_runtime_put(&pdev->dev);

	i2s->active_channels = 1;
	i2s->dma_data.addr = res->start + IMG_I2S_OUT_TX_FIFO;
	i2s->dma_data.addr_width = 4;
	i2s->dma_data.maxburst = 4;

	i2s->dai_driver.playback.channels_min = 2;
	i2s->dai_driver.playback.channels_max = i2s->max_i2s_chan * 2;
	i2s->dai_driver.playback.rates = SNDRV_PCM_RATE_8000_192000;
	i2s->dai_driver.playback.formats = SNDRV_PCM_FMTBIT_S32_LE;
	i2s->dai_driver.ops = &img_i2s_out_dai_ops;

	ret = devm_snd_soc_register_component(&pdev->dev,
			&img_i2s_out_component, &i2s->dai_driver, 1);
	if (ret)
		goto err_suspend;

	ret = devm_snd_dmaengine_pcm_register(&pdev->dev,
			&img_i2s_out_dma_config, 0);
	if (ret)
		goto err_suspend;

	return 0;

err_suspend:
	if (!pm_runtime_status_suspended(&pdev->dev))
		img_i2s_out_runtime_suspend(&pdev->dev);
err_pm_disable:
	pm_runtime_disable(&pdev->dev);

	return ret;
}

static void img_i2s_out_dev_remove(struct platform_device *pdev)
{
	pm_runtime_disable(&pdev->dev);
	if (!pm_runtime_status_suspended(&pdev->dev))
		img_i2s_out_runtime_suspend(&pdev->dev);
}

#ifdef CONFIG_PM_SLEEP
static int img_i2s_out_suspend(struct device *dev)
{
	struct img_i2s_out *i2s = dev_get_drvdata(dev);
	int i, ret;
	u32 reg;

	if (pm_runtime_status_suspended(dev)) {
		ret = img_i2s_out_runtime_resume(dev);
		if (ret)
			return ret;
	}

	for (i = 0; i < i2s->max_i2s_chan; i++) {
		reg = img_i2s_out_ch_readl(i2s, i, IMG_I2S_OUT_CH_CTL);
		i2s->suspend_ch_ctl[i] = reg;
	}

	i2s->suspend_ctl = img_i2s_out_readl(i2s, IMG_I2S_OUT_CTL);

	img_i2s_out_runtime_suspend(dev);

	return 0;
}

static int img_i2s_out_resume(struct device *dev)
{
	struct img_i2s_out *i2s = dev_get_drvdata(dev);
	int i, ret;
	u32 reg;

	ret = img_i2s_out_runtime_resume(dev);
	if (ret)
		return ret;

	for (i = 0; i < i2s->max_i2s_chan; i++) {
		reg = i2s->suspend_ch_ctl[i];
		img_i2s_out_ch_writel(i2s, i, reg, IMG_I2S_OUT_CH_CTL);
	}

	img_i2s_out_writel(i2s, i2s->suspend_ctl, IMG_I2S_OUT_CTL);

	if (pm_runtime_status_suspended(dev))
		img_i2s_out_runtime_suspend(dev);

	return 0;
}
#endif

static const struct of_device_id img_i2s_out_of_match[] = {
	{ .compatible = "img,i2s-out" },
	{}
};
MODULE_DEVICE_TABLE(of, img_i2s_out_of_match);

static const struct dev_pm_ops img_i2s_out_pm_ops = {
	SET_RUNTIME_PM_OPS(img_i2s_out_runtime_suspend,
			   img_i2s_out_runtime_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(img_i2s_out_suspend, img_i2s_out_resume)
};

static struct platform_driver img_i2s_out_driver = {
	.driver = {
		.name = "img-i2s-out",
		.of_match_table = img_i2s_out_of_match,
		.pm = &img_i2s_out_pm_ops
	},
	.probe = img_i2s_out_probe,
	.remove_new = img_i2s_out_dev_remove
};
module_platform_driver(img_i2s_out_driver);

MODULE_AUTHOR("Damien Horsley <Damien.Horsley@imgtec.com>");
MODULE_DESCRIPTION("IMG I2S Output Driver");
MODULE_LICENSE("GPL v2");
