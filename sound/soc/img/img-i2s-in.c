// SPDX-License-Identifier: GPL-2.0-only
/*
 * IMG I2S input controller driver
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

#define IMG_I2S_IN_RX_FIFO			0x0

#define IMG_I2S_IN_CTL				0x4
#define IMG_I2S_IN_CTL_ACTIVE_CHAN_MASK		0xfffffffc
#define IMG_I2S_IN_CTL_ACTIVE_CH_SHIFT		2
#define IMG_I2S_IN_CTL_16PACK_MASK		BIT(1)
#define IMG_I2S_IN_CTL_ME_MASK			BIT(0)

#define IMG_I2S_IN_CH_CTL			0x4
#define IMG_I2S_IN_CH_CTL_CCDEL_MASK		0x38000
#define IMG_I2S_IN_CH_CTL_CCDEL_SHIFT		15
#define IMG_I2S_IN_CH_CTL_FEN_MASK		BIT(14)
#define IMG_I2S_IN_CH_CTL_FMODE_MASK		BIT(13)
#define IMG_I2S_IN_CH_CTL_16PACK_MASK		BIT(12)
#define IMG_I2S_IN_CH_CTL_JUST_MASK		BIT(10)
#define IMG_I2S_IN_CH_CTL_PACKH_MASK		BIT(9)
#define IMG_I2S_IN_CH_CTL_CLK_TRANS_MASK	BIT(8)
#define IMG_I2S_IN_CH_CTL_BLKP_MASK		BIT(7)
#define IMG_I2S_IN_CH_CTL_FIFO_FLUSH_MASK	BIT(6)
#define IMG_I2S_IN_CH_CTL_LRD_MASK		BIT(3)
#define IMG_I2S_IN_CH_CTL_FW_MASK		BIT(2)
#define IMG_I2S_IN_CH_CTL_SW_MASK		BIT(1)
#define IMG_I2S_IN_CH_CTL_ME_MASK		BIT(0)

#define IMG_I2S_IN_CH_STRIDE			0x20

struct img_i2s_in {
	void __iomem *base;
	struct clk *clk_sys;
	struct snd_dmaengine_dai_dma_data dma_data;
	struct device *dev;
	unsigned int max_i2s_chan;
	void __iomem *channel_base;
	unsigned int active_channels;
	struct snd_soc_dai_driver dai_driver;
	u32 suspend_ctl;
	u32 *suspend_ch_ctl;
};

static int img_i2s_in_runtime_suspend(struct device *dev)
{
	struct img_i2s_in *i2s = dev_get_drvdata(dev);

	clk_disable_unprepare(i2s->clk_sys);

	return 0;
}

static int img_i2s_in_runtime_resume(struct device *dev)
{
	struct img_i2s_in *i2s = dev_get_drvdata(dev);
	int ret;

	ret = clk_prepare_enable(i2s->clk_sys);
	if (ret) {
		dev_err(dev, "Unable to enable sys clock\n");
		return ret;
	}

	return 0;
}

static inline void img_i2s_in_writel(struct img_i2s_in *i2s, u32 val, u32 reg)
{
	writel(val, i2s->base + reg);
}

static inline u32 img_i2s_in_readl(struct img_i2s_in *i2s, u32 reg)
{
	return readl(i2s->base + reg);
}

static inline void img_i2s_in_ch_writel(struct img_i2s_in *i2s, u32 chan,
					u32 val, u32 reg)
{
	writel(val, i2s->channel_base + (chan * IMG_I2S_IN_CH_STRIDE) + reg);
}

static inline u32 img_i2s_in_ch_readl(struct img_i2s_in *i2s, u32 chan,
					u32 reg)
{
	return readl(i2s->channel_base + (chan * IMG_I2S_IN_CH_STRIDE) + reg);
}

static inline void img_i2s_in_ch_disable(struct img_i2s_in *i2s, u32 chan)
{
	u32 reg;

	reg = img_i2s_in_ch_readl(i2s, chan, IMG_I2S_IN_CH_CTL);
	reg &= ~IMG_I2S_IN_CH_CTL_ME_MASK;
	img_i2s_in_ch_writel(i2s, chan, reg, IMG_I2S_IN_CH_CTL);
}

static inline void img_i2s_in_ch_enable(struct img_i2s_in *i2s, u32 chan)
{
	u32 reg;

	reg = img_i2s_in_ch_readl(i2s, chan, IMG_I2S_IN_CH_CTL);
	reg |= IMG_I2S_IN_CH_CTL_ME_MASK;
	img_i2s_in_ch_writel(i2s, chan, reg, IMG_I2S_IN_CH_CTL);
}

static inline void img_i2s_in_disable(struct img_i2s_in *i2s)
{
	u32 reg;

	reg = img_i2s_in_readl(i2s, IMG_I2S_IN_CTL);
	reg &= ~IMG_I2S_IN_CTL_ME_MASK;
	img_i2s_in_writel(i2s, reg, IMG_I2S_IN_CTL);
}

static inline void img_i2s_in_enable(struct img_i2s_in *i2s)
{
	u32 reg;

	reg = img_i2s_in_readl(i2s, IMG_I2S_IN_CTL);
	reg |= IMG_I2S_IN_CTL_ME_MASK;
	img_i2s_in_writel(i2s, reg, IMG_I2S_IN_CTL);
}

static inline void img_i2s_in_flush(struct img_i2s_in *i2s)
{
	int i;
	u32 reg;

	for (i = 0; i < i2s->active_channels; i++) {
		reg = img_i2s_in_ch_readl(i2s, i, IMG_I2S_IN_CH_CTL);
		reg |= IMG_I2S_IN_CH_CTL_FIFO_FLUSH_MASK;
		img_i2s_in_ch_writel(i2s, i, reg, IMG_I2S_IN_CH_CTL);
		reg &= ~IMG_I2S_IN_CH_CTL_FIFO_FLUSH_MASK;
		img_i2s_in_ch_writel(i2s, i, reg, IMG_I2S_IN_CH_CTL);
	}
}

static int img_i2s_in_trigger(struct snd_pcm_substream *substream, int cmd,
	struct snd_soc_dai *dai)
{
	struct img_i2s_in *i2s = snd_soc_dai_get_drvdata(dai);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		img_i2s_in_enable(i2s);
		break;

	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		img_i2s_in_disable(i2s);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int img_i2s_in_check_rate(struct img_i2s_in *i2s,
		unsigned int sample_rate, unsigned int frame_size,
		unsigned int *bclk_filter_enable,
		unsigned int *bclk_filter_value)
{
	unsigned int bclk_freq, cur_freq;

	bclk_freq = sample_rate * frame_size;

	cur_freq = clk_get_rate(i2s->clk_sys);

	if (cur_freq >= bclk_freq * 8) {
		*bclk_filter_enable = 1;
		*bclk_filter_value = 0;
	} else if (cur_freq >= bclk_freq * 7) {
		*bclk_filter_enable = 1;
		*bclk_filter_value = 1;
	} else if (cur_freq >= bclk_freq * 6) {
		*bclk_filter_enable = 0;
		*bclk_filter_value = 0;
	} else {
		dev_err(i2s->dev,
			"Sys clock rate %u insufficient for sample rate %u\n",
			cur_freq, sample_rate);
		return -EINVAL;
	}

	return 0;
}

static int img_i2s_in_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	struct img_i2s_in *i2s = snd_soc_dai_get_drvdata(dai);
	unsigned int rate, channels, i2s_channels, frame_size;
	unsigned int bclk_filter_enable, bclk_filter_value;
	int i, ret = 0;
	u32 reg, control_mask, chan_control_mask;
	u32 control_set = 0, chan_control_set = 0;
	snd_pcm_format_t format;

	rate = params_rate(params);
	format = params_format(params);
	channels = params_channels(params);
	i2s_channels = channels / 2;

	switch (format) {
	case SNDRV_PCM_FORMAT_S32_LE:
		frame_size = 64;
		chan_control_set |= IMG_I2S_IN_CH_CTL_SW_MASK;
		chan_control_set |= IMG_I2S_IN_CH_CTL_FW_MASK;
		chan_control_set |= IMG_I2S_IN_CH_CTL_PACKH_MASK;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		frame_size = 64;
		chan_control_set |= IMG_I2S_IN_CH_CTL_SW_MASK;
		chan_control_set |= IMG_I2S_IN_CH_CTL_FW_MASK;
		break;
	case SNDRV_PCM_FORMAT_S16_LE:
		frame_size = 32;
		control_set |= IMG_I2S_IN_CTL_16PACK_MASK;
		chan_control_set |= IMG_I2S_IN_CH_CTL_16PACK_MASK;
		break;
	default:
		return -EINVAL;
	}

	if ((channels < 2) ||
	    (channels > (i2s->max_i2s_chan * 2)) ||
	    (channels % 2))
		return -EINVAL;

	control_set |= ((i2s_channels - 1) << IMG_I2S_IN_CTL_ACTIVE_CH_SHIFT);

	ret = img_i2s_in_check_rate(i2s, rate, frame_size,
			&bclk_filter_enable, &bclk_filter_value);
	if (ret < 0)
		return ret;

	if (bclk_filter_enable)
		chan_control_set |= IMG_I2S_IN_CH_CTL_FEN_MASK;

	if (bclk_filter_value)
		chan_control_set |= IMG_I2S_IN_CH_CTL_FMODE_MASK;

	control_mask = IMG_I2S_IN_CTL_16PACK_MASK |
		       IMG_I2S_IN_CTL_ACTIVE_CHAN_MASK;

	chan_control_mask = IMG_I2S_IN_CH_CTL_16PACK_MASK |
			    IMG_I2S_IN_CH_CTL_FEN_MASK |
			    IMG_I2S_IN_CH_CTL_FMODE_MASK |
			    IMG_I2S_IN_CH_CTL_SW_MASK |
			    IMG_I2S_IN_CH_CTL_FW_MASK |
			    IMG_I2S_IN_CH_CTL_PACKH_MASK;

	reg = img_i2s_in_readl(i2s, IMG_I2S_IN_CTL);
	reg = (reg & ~control_mask) | control_set;
	img_i2s_in_writel(i2s, reg, IMG_I2S_IN_CTL);

	for (i = 0; i < i2s->active_channels; i++)
		img_i2s_in_ch_disable(i2s, i);

	for (i = 0; i < i2s->max_i2s_chan; i++) {
		reg = img_i2s_in_ch_readl(i2s, i, IMG_I2S_IN_CH_CTL);
		reg = (reg & ~chan_control_mask) | chan_control_set;
		img_i2s_in_ch_writel(i2s, i, reg, IMG_I2S_IN_CH_CTL);
	}

	i2s->active_channels = i2s_channels;

	img_i2s_in_flush(i2s);

	for (i = 0; i < i2s->active_channels; i++)
		img_i2s_in_ch_enable(i2s, i);

	return 0;
}

static int img_i2s_in_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct img_i2s_in *i2s = snd_soc_dai_get_drvdata(dai);
	int i, ret;
	u32 chan_control_mask, lrd_set = 0, blkp_set = 0, chan_control_set = 0;
	u32 reg;

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		lrd_set |= IMG_I2S_IN_CH_CTL_LRD_MASK;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		break;
	case SND_SOC_DAIFMT_IB_NF:
		lrd_set |= IMG_I2S_IN_CH_CTL_LRD_MASK;
		blkp_set |= IMG_I2S_IN_CH_CTL_BLKP_MASK;
		break;
	case SND_SOC_DAIFMT_IB_IF:
		blkp_set |= IMG_I2S_IN_CH_CTL_BLKP_MASK;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		chan_control_set |= IMG_I2S_IN_CH_CTL_CLK_TRANS_MASK;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_CLOCK_PROVIDER_MASK) {
	case SND_SOC_DAIFMT_BC_FC:
		break;
	default:
		return -EINVAL;
	}

	chan_control_mask = IMG_I2S_IN_CH_CTL_CLK_TRANS_MASK;

	ret = pm_runtime_resume_and_get(i2s->dev);
	if (ret < 0)
		return ret;

	for (i = 0; i < i2s->active_channels; i++)
		img_i2s_in_ch_disable(i2s, i);

	/*
	 * BLKP and LRD must be set during separate register writes
	 */
	for (i = 0; i < i2s->max_i2s_chan; i++) {
		reg = img_i2s_in_ch_readl(i2s, i, IMG_I2S_IN_CH_CTL);
		reg = (reg & ~chan_control_mask) | chan_control_set;
		img_i2s_in_ch_writel(i2s, i, reg, IMG_I2S_IN_CH_CTL);
		reg = (reg & ~IMG_I2S_IN_CH_CTL_BLKP_MASK) | blkp_set;
		img_i2s_in_ch_writel(i2s, i, reg, IMG_I2S_IN_CH_CTL);
		reg = (reg & ~IMG_I2S_IN_CH_CTL_LRD_MASK) | lrd_set;
		img_i2s_in_ch_writel(i2s, i, reg, IMG_I2S_IN_CH_CTL);
	}

	for (i = 0; i < i2s->active_channels; i++)
		img_i2s_in_ch_enable(i2s, i);

	pm_runtime_put(i2s->dev);

	return 0;
}

static int img_i2s_in_dai_probe(struct snd_soc_dai *dai)
{
	struct img_i2s_in *i2s = snd_soc_dai_get_drvdata(dai);

	snd_soc_dai_init_dma_data(dai, NULL, &i2s->dma_data);

	return 0;
}

static const struct snd_soc_dai_ops img_i2s_in_dai_ops = {
	.probe		= img_i2s_in_dai_probe,
	.trigger	= img_i2s_in_trigger,
	.hw_params	= img_i2s_in_hw_params,
	.set_fmt	= img_i2s_in_set_fmt
};

static const struct snd_soc_component_driver img_i2s_in_component = {
	.name = "img-i2s-in",
	.legacy_dai_naming = 1,
};

static int img_i2s_in_dma_prepare_slave_config(struct snd_pcm_substream *st,
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

	sc->src_addr = dma_data->addr;
	sc->src_addr_width = dma_data->addr_width;
	sc->src_maxburst = 4 * i2s_channels;

	return 0;
}

static const struct snd_dmaengine_pcm_config img_i2s_in_dma_config = {
	.prepare_slave_config = img_i2s_in_dma_prepare_slave_config
};

static int img_i2s_in_probe(struct platform_device *pdev)
{
	struct img_i2s_in *i2s;
	struct resource *res;
	void __iomem *base;
	int ret, i;
	struct reset_control *rst;
	unsigned int max_i2s_chan_pow_2;
	struct device *dev = &pdev->dev;

	i2s = devm_kzalloc(dev, sizeof(*i2s), GFP_KERNEL);
	if (!i2s)
		return -ENOMEM;

	platform_set_drvdata(pdev, i2s);

	i2s->dev = dev;

	base = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	i2s->base = base;

	if (of_property_read_u32(pdev->dev.of_node, "img,i2s-channels",
			&i2s->max_i2s_chan)) {
		dev_err(dev, "No img,i2s-channels property\n");
		return -EINVAL;
	}

	max_i2s_chan_pow_2 = 1 << get_count_order(i2s->max_i2s_chan);

	i2s->channel_base = base + (max_i2s_chan_pow_2 * 0x20);

	i2s->clk_sys = devm_clk_get(dev, "sys");
	if (IS_ERR(i2s->clk_sys))
		return dev_err_probe(dev, PTR_ERR(i2s->clk_sys),
				     "Failed to acquire clock 'sys'\n");

	pm_runtime_enable(&pdev->dev);
	if (!pm_runtime_enabled(&pdev->dev)) {
		ret = img_i2s_in_runtime_resume(&pdev->dev);
		if (ret)
			goto err_pm_disable;
	}
	ret = pm_runtime_resume_and_get(&pdev->dev);
	if (ret < 0)
		goto err_suspend;

	i2s->active_channels = 1;
	i2s->dma_data.addr = res->start + IMG_I2S_IN_RX_FIFO;
	i2s->dma_data.addr_width = 4;

	i2s->dai_driver.capture.channels_min = 2;
	i2s->dai_driver.capture.channels_max = i2s->max_i2s_chan * 2;
	i2s->dai_driver.capture.rates = SNDRV_PCM_RATE_8000_192000;
	i2s->dai_driver.capture.formats = SNDRV_PCM_FMTBIT_S32_LE |
		SNDRV_PCM_FMTBIT_S24_LE | SNDRV_PCM_FMTBIT_S16_LE;
	i2s->dai_driver.ops = &img_i2s_in_dai_ops;

	rst = devm_reset_control_get_exclusive(dev, "rst");
	if (IS_ERR(rst)) {
		if (PTR_ERR(rst) == -EPROBE_DEFER) {
			ret = -EPROBE_DEFER;
			pm_runtime_put(&pdev->dev);
			goto err_suspend;
		}

		dev_dbg(dev, "No top level reset found\n");

		img_i2s_in_disable(i2s);

		for (i = 0; i < i2s->max_i2s_chan; i++)
			img_i2s_in_ch_disable(i2s, i);
	} else {
		reset_control_assert(rst);
		reset_control_deassert(rst);
	}

	img_i2s_in_writel(i2s, 0, IMG_I2S_IN_CTL);

	for (i = 0; i < i2s->max_i2s_chan; i++)
		img_i2s_in_ch_writel(i2s, i,
			(4 << IMG_I2S_IN_CH_CTL_CCDEL_SHIFT) |
			IMG_I2S_IN_CH_CTL_JUST_MASK |
			IMG_I2S_IN_CH_CTL_FW_MASK, IMG_I2S_IN_CH_CTL);

	pm_runtime_put(&pdev->dev);

	i2s->suspend_ch_ctl = devm_kcalloc(dev,
		i2s->max_i2s_chan, sizeof(*i2s->suspend_ch_ctl), GFP_KERNEL);
	if (!i2s->suspend_ch_ctl) {
		ret = -ENOMEM;
		goto err_suspend;
	}

	ret = devm_snd_soc_register_component(dev, &img_i2s_in_component,
						&i2s->dai_driver, 1);
	if (ret)
		goto err_suspend;

	ret = devm_snd_dmaengine_pcm_register(dev, &img_i2s_in_dma_config, 0);
	if (ret)
		goto err_suspend;

	return 0;

err_suspend:
	if (!pm_runtime_enabled(&pdev->dev))
		img_i2s_in_runtime_suspend(&pdev->dev);
err_pm_disable:
	pm_runtime_disable(&pdev->dev);

	return ret;
}

static void img_i2s_in_dev_remove(struct platform_device *pdev)
{
	pm_runtime_disable(&pdev->dev);
	if (!pm_runtime_status_suspended(&pdev->dev))
		img_i2s_in_runtime_suspend(&pdev->dev);
}

#ifdef CONFIG_PM_SLEEP
static int img_i2s_in_suspend(struct device *dev)
{
	struct img_i2s_in *i2s = dev_get_drvdata(dev);
	int i, ret;
	u32 reg;

	if (pm_runtime_status_suspended(dev)) {
		ret = img_i2s_in_runtime_resume(dev);
		if (ret)
			return ret;
	}

	for (i = 0; i < i2s->max_i2s_chan; i++) {
		reg = img_i2s_in_ch_readl(i2s, i, IMG_I2S_IN_CH_CTL);
		i2s->suspend_ch_ctl[i] = reg;
	}

	i2s->suspend_ctl = img_i2s_in_readl(i2s, IMG_I2S_IN_CTL);

	img_i2s_in_runtime_suspend(dev);

	return 0;
}

static int img_i2s_in_resume(struct device *dev)
{
	struct img_i2s_in *i2s = dev_get_drvdata(dev);
	int i, ret;
	u32 reg;

	ret = img_i2s_in_runtime_resume(dev);
	if (ret)
		return ret;

	for (i = 0; i < i2s->max_i2s_chan; i++) {
		reg = i2s->suspend_ch_ctl[i];
		img_i2s_in_ch_writel(i2s, i, reg, IMG_I2S_IN_CH_CTL);
	}

	img_i2s_in_writel(i2s, i2s->suspend_ctl, IMG_I2S_IN_CTL);

	if (pm_runtime_status_suspended(dev))
		img_i2s_in_runtime_suspend(dev);

	return 0;
}
#endif

static const struct of_device_id img_i2s_in_of_match[] = {
	{ .compatible = "img,i2s-in" },
	{}
};
MODULE_DEVICE_TABLE(of, img_i2s_in_of_match);

static const struct dev_pm_ops img_i2s_in_pm_ops = {
	SET_RUNTIME_PM_OPS(img_i2s_in_runtime_suspend,
			   img_i2s_in_runtime_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(img_i2s_in_suspend, img_i2s_in_resume)
};

static struct platform_driver img_i2s_in_driver = {
	.driver = {
		.name = "img-i2s-in",
		.of_match_table = img_i2s_in_of_match,
		.pm = &img_i2s_in_pm_ops
	},
	.probe = img_i2s_in_probe,
	.remove_new = img_i2s_in_dev_remove
};
module_platform_driver(img_i2s_in_driver);

MODULE_AUTHOR("Damien Horsley <Damien.Horsley@imgtec.com>");
MODULE_DESCRIPTION("IMG I2S Input Driver");
MODULE_LICENSE("GPL v2");
