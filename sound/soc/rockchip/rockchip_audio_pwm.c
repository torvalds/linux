// SPDX-License-Identifier: GPL-2.0
/*
 * Rockchip Audio PWM Driver
 *
 * Copyright (C) 2020 Fuzhou Rockchip Electronics Co.,Ltd
 *
 */

#include <linux/clk.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <sound/dmaengine_pcm.h>
#include <sound/pcm_params.h>

#include "rockchip_audio_pwm.h"

#define AUDIO_PWM_DMA_BURST_SIZE	(16) /* size * width: 16*4 = 64 bytes */

struct rk_audio_pwm_dev {
	struct device *dev;
	struct clk *clk;
	struct clk *hclk;
	struct regmap *regmap;
	struct snd_dmaengine_dai_dma_data playback_dma_data;
	struct gpio_desc *spk_ctl_gpio;
	int interpolat_points;
	int sample_width_bits;
};

static inline struct rk_audio_pwm_dev *to_info(struct snd_soc_dai *dai)
{
	return snd_soc_dai_get_drvdata(dai);
}

static void rockchip_audio_spk_ctl(struct rk_audio_pwm_dev *apwm, int on)
{
	if (apwm->spk_ctl_gpio)
		gpiod_direction_output(apwm->spk_ctl_gpio, on);
}

static void rockchip_audio_pwm_xfer(struct rk_audio_pwm_dev *apwm, int on)
{
	if (on) {
		regmap_write(apwm->regmap, AUDPWM_FIFO_CFG, AUDPWM_DMA_EN);
		regmap_write(apwm->regmap, AUDPWM_XFER, AUDPWM_XFER_START);
		rockchip_audio_spk_ctl(apwm, on);
	} else {
		rockchip_audio_spk_ctl(apwm, on);
		regmap_write(apwm->regmap, AUDPWM_FIFO_CFG, AUDPWM_DMA_DIS);
		regmap_write(apwm->regmap, AUDPWM_XFER, AUDPWM_XFER_STOP);
	}
}

static int rockchip_audio_pwm_hw_params(struct snd_pcm_substream *substream,
					struct snd_pcm_hw_params *params,
					struct snd_soc_dai *dai)
{
	struct rk_audio_pwm_dev *apwm = to_info(dai);
	unsigned long rate;
	int ret;

	rate = params_rate(params) << apwm->sample_width_bits;
	if (apwm->interpolat_points) {
		rate *= (apwm->interpolat_points + 1);
		regmap_write(apwm->regmap, AUDPWM_PWM_CFG,
			     AUDPWM_LINEAR_INTERP_EN |
			     AUDPWM_INTERP_RATE(apwm->interpolat_points));
	}
	if (!rate)
		return -EINVAL;
	ret = clk_set_rate(apwm->clk, rate);
	if (ret)
		return -EINVAL;

	regmap_write(apwm->regmap, AUDPWM_SRC_CFG,
		     AUDPWM_SRC_WIDTH(params_width(params)));
	regmap_write(apwm->regmap, AUDPWM_PWM_CFG,
		     AUDPWM_SAMPLE_WIDTH(apwm->sample_width_bits));
	regmap_write(apwm->regmap, AUDPWM_FIFO_CFG,
		     AUDPWM_DMA_WATERMARK(16));

	return 0;
}

static int rockchip_audio_pwm_trigger(struct snd_pcm_substream *substream,
				      int cmd, struct snd_soc_dai *dai)
{
	struct rk_audio_pwm_dev *apwm = to_info(dai);
	int ret = 0;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			rockchip_audio_pwm_xfer(apwm, 1);
		break;
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			rockchip_audio_pwm_xfer(apwm, 0);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

static int rockchip_audio_pwm_dai_probe(struct snd_soc_dai *dai)
{
	struct rk_audio_pwm_dev *apwm = to_info(dai);

	dai->playback_dma_data = &apwm->playback_dma_data;

	return 0;
}

static const struct snd_soc_dai_ops rockchip_audio_pwm_dai_ops = {
	.trigger = rockchip_audio_pwm_trigger,
	.hw_params = rockchip_audio_pwm_hw_params,
};

#define ROCKCHIP_AUDIO_PWM_RATES SNDRV_PCM_RATE_8000_48000
#define ROCKCHIP_AUDIO_PWM_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | \
				    SNDRV_PCM_FMTBIT_S24_LE | \
				    SNDRV_PCM_FMTBIT_S32_LE)

static struct snd_soc_dai_driver rockchip_audio_pwm_dai = {
	.probe = rockchip_audio_pwm_dai_probe,
	.playback = {
		.stream_name = "Playback",
		.channels_min = 1,
		.channels_max = 2,
		.rates = ROCKCHIP_AUDIO_PWM_RATES,
		.formats = ROCKCHIP_AUDIO_PWM_FORMATS,
	},
	.ops = &rockchip_audio_pwm_dai_ops,
};

static const struct snd_soc_component_driver rockchip_audio_pwm_component = {
	.name = "rockchip-audio-pwm",
};

static int __maybe_unused rockchip_audio_pwm_runtime_suspend(struct device *dev)
{
	struct rk_audio_pwm_dev *apwm = dev_get_drvdata(dev);

	regcache_cache_only(apwm->regmap, true);
	clk_disable_unprepare(apwm->clk);
	clk_disable_unprepare(apwm->hclk);

	return 0;
}

static int __maybe_unused rockchip_audio_pwm_runtime_resume(struct device *dev)
{
	struct rk_audio_pwm_dev *apwm = dev_get_drvdata(dev);
	int ret;

	ret = clk_prepare_enable(apwm->clk);
	if (ret)
		return ret;

	ret = clk_prepare_enable(apwm->hclk);
	if (ret)
		return ret;

	regcache_cache_only(apwm->regmap, false);
	regcache_mark_dirty(apwm->regmap);
	ret = regcache_sync(apwm->regmap);
	if (ret) {
		clk_disable_unprepare(apwm->clk);
		clk_disable_unprepare(apwm->hclk);
	}

	return 0;
}

static bool rockchip_audio_pwm_wr_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case AUDPWM_XFER:
	case AUDPWM_SRC_CFG:
	case AUDPWM_PWM_CFG:
	case AUDPWM_FIFO_CFG:
	case AUDPWM_FIFO_INT_EN:
	case AUDPWM_FIFO_INT_ST:
	case AUDPWM_FIFO_ENTRY:
		return true;
	default:
		return false;
	}
}

static bool rockchip_audio_pwm_rd_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case AUDPWM_VERSION:
	case AUDPWM_XFER:
	case AUDPWM_SRC_CFG:
	case AUDPWM_PWM_CFG:
	case AUDPWM_PWM_ST:
	case AUDPWM_PWM_BUF_01:
	case AUDPWM_PWM_BUF_23:
	case AUDPWM_FIFO_CFG:
	case AUDPWM_FIFO_LVL:
	case AUDPWM_FIFO_INT_EN:
	case AUDPWM_FIFO_INT_ST:
		return true;
	default:
		return false;
	}
}

static bool rockchip_audio_pwm_volatile_reg(struct device *dev,
					    unsigned int reg)
{
	switch (reg) {
	case AUDPWM_XFER:
	case AUDPWM_PWM_ST:
	case AUDPWM_PWM_BUF_01:
	case AUDPWM_PWM_BUF_23:
	case AUDPWM_FIFO_LVL:
	case AUDPWM_FIFO_INT_ST:
		return true;
	default:
		return false;
	}
}

static const struct regmap_config rockchip_audio_pwm_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = AUDPWM_FIFO_ENTRY,
	.writeable_reg = rockchip_audio_pwm_wr_reg,
	.readable_reg = rockchip_audio_pwm_rd_reg,
	.volatile_reg = rockchip_audio_pwm_volatile_reg,
	.cache_type = REGCACHE_FLAT,
};

static const struct of_device_id rockchip_audio_pwm_match[] = {
	{ .compatible = "rockchip,audio-pwm-v1" },
	{},
};
MODULE_DEVICE_TABLE(of, rockchip_audio_pwm_match);

static int rockchip_audio_pwm_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct rk_audio_pwm_dev *apwm;
	struct resource *res;
	void __iomem *regs;
	int ret;
	int val;

	apwm = devm_kzalloc(&pdev->dev, sizeof(*apwm), GFP_KERNEL);
	if (!apwm)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	regs = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	apwm->regmap = devm_regmap_init_mmio(&pdev->dev, regs,
					     &rockchip_audio_pwm_config);
	if (IS_ERR(apwm->regmap))
		return PTR_ERR(apwm->regmap);

	apwm->playback_dma_data.addr = res->start + AUDPWM_FIFO_ENTRY;
	apwm->playback_dma_data.addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	apwm->playback_dma_data.maxburst = AUDIO_PWM_DMA_BURST_SIZE;

	apwm->dev = &pdev->dev;
	dev_set_drvdata(&pdev->dev, apwm);

	apwm->clk = devm_clk_get(&pdev->dev, "clk");
	if (IS_ERR(apwm->clk))
		return PTR_ERR(apwm->clk);

	apwm->hclk = devm_clk_get(&pdev->dev, "hclk");
	if (IS_ERR(apwm->hclk))
		return PTR_ERR(apwm->hclk);

	pm_runtime_enable(&pdev->dev);
	if (!pm_runtime_enabled(&pdev->dev)) {
		ret = rockchip_audio_pwm_runtime_resume(&pdev->dev);
		if (ret)
			goto err_pm_disable;
	}

	apwm->sample_width_bits = 8;
	of_property_read_u32(np, "rockchip,sample-width-bits", &val);
	if (val >= 8 && val <= 11)
		apwm->sample_width_bits = val;

	of_property_read_u32(np, "rockchip,interpolat-points",
			     &apwm->interpolat_points);

	apwm->spk_ctl_gpio = devm_gpiod_get_optional(&pdev->dev, "spk-ctl",
						     GPIOD_OUT_LOW);

	if (!apwm->spk_ctl_gpio) {
		dev_info(&pdev->dev, "no need spk-ctl gpio\n");
	} else if (IS_ERR(apwm->spk_ctl_gpio)) {
		ret = PTR_ERR(apwm->spk_ctl_gpio);
		dev_err(&pdev->dev, "fail to request gpio spk-ctl\n");
		goto err_suspend;
	}

	ret = devm_snd_soc_register_component(&pdev->dev,
					      &rockchip_audio_pwm_component,
					      &rockchip_audio_pwm_dai, 1);
	if (ret) {
		dev_err(&pdev->dev, "could not register dai: %d\n", ret);
		goto err_suspend;
	}

	ret = devm_snd_dmaengine_pcm_register(&pdev->dev, NULL, 0);
	if (ret) {
		dev_err(&pdev->dev, "could not register pcm: %d\n", ret);
		goto err_suspend;
	}

	return 0;

err_suspend:
	if (!pm_runtime_status_suspended(&pdev->dev))
		rockchip_audio_pwm_runtime_suspend(&pdev->dev);
err_pm_disable:
	pm_runtime_disable(&pdev->dev);

	return ret;
}

static int rockchip_audio_pwm_remove(struct platform_device *pdev)
{
	pm_runtime_disable(&pdev->dev);
	if (!pm_runtime_status_suspended(&pdev->dev))
		rockchip_audio_pwm_runtime_suspend(&pdev->dev);

	return 0;
}

static const struct dev_pm_ops rockchip_audio_pwm_pm_ops = {
	SET_RUNTIME_PM_OPS(rockchip_audio_pwm_runtime_suspend,
			   rockchip_audio_pwm_runtime_resume, NULL)
};

static struct platform_driver rockchip_audio_pwm_driver = {
	.probe = rockchip_audio_pwm_probe,
	.remove = rockchip_audio_pwm_remove,
	.driver = {
		.name = "rockchip-audio-pwm",
		.of_match_table = of_match_ptr(rockchip_audio_pwm_match),
		.pm = &rockchip_audio_pwm_pm_ops,
	},
};

module_platform_driver(rockchip_audio_pwm_driver);

MODULE_AUTHOR("Sugar Zhang <sugar.zhang@rock-chips.com>");
MODULE_DESCRIPTION("Rockchip Audio PWM Driver");
MODULE_LICENSE("GPL v2");
