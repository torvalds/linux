// SPDX-License-Identifier: GPL-2.0-only
/*
 * tegra20_ac97.c - Tegra20 AC97 platform driver
 *
 * Copyright (c) 2012 Lucas Stach <dev@lynxeye.de>
 *
 * Partly based on code copyright/by:
 *
 * Copyright (c) 2011,2012 Toradex Inc.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/gpio.h>
#include <linux/io.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/dmaengine_pcm.h>

#include "tegra20_ac97.h"

#define DRV_NAME "tegra20-ac97"

static struct tegra20_ac97 *workdata;

static void tegra20_ac97_codec_reset(struct snd_ac97 *ac97)
{
	u32 readback;
	unsigned long timeout;

	/* reset line is not driven by DAC pad group, have to toggle GPIO */
	gpio_set_value(workdata->reset_gpio, 0);
	udelay(2);

	gpio_set_value(workdata->reset_gpio, 1);
	udelay(2);

	timeout = jiffies + msecs_to_jiffies(100);

	do {
		regmap_read(workdata->regmap, TEGRA20_AC97_STATUS1, &readback);
		if (readback & TEGRA20_AC97_STATUS1_CODEC1_RDY)
			break;
		usleep_range(1000, 2000);
	} while (!time_after(jiffies, timeout));
}

static void tegra20_ac97_codec_warm_reset(struct snd_ac97 *ac97)
{
	u32 readback;
	unsigned long timeout;

	/*
	 * although sync line is driven by the DAC pad group warm reset using
	 * the controller cmd is not working, have to toggle sync line
	 * manually.
	 */
	gpio_request(workdata->sync_gpio, "codec-sync");

	gpio_direction_output(workdata->sync_gpio, 1);

	udelay(2);
	gpio_set_value(workdata->sync_gpio, 0);
	udelay(2);
	gpio_free(workdata->sync_gpio);

	timeout = jiffies + msecs_to_jiffies(100);

	do {
		regmap_read(workdata->regmap, TEGRA20_AC97_STATUS1, &readback);
		if (readback & TEGRA20_AC97_STATUS1_CODEC1_RDY)
			break;
		usleep_range(1000, 2000);
	} while (!time_after(jiffies, timeout));
}

static unsigned short tegra20_ac97_codec_read(struct snd_ac97 *ac97_snd,
					      unsigned short reg)
{
	u32 readback;
	unsigned long timeout;

	regmap_write(workdata->regmap, TEGRA20_AC97_CMD,
		     (((reg | 0x80) << TEGRA20_AC97_CMD_CMD_ADDR_SHIFT) &
		      TEGRA20_AC97_CMD_CMD_ADDR_MASK) |
		     TEGRA20_AC97_CMD_BUSY);

	timeout = jiffies + msecs_to_jiffies(100);

	do {
		regmap_read(workdata->regmap, TEGRA20_AC97_STATUS1, &readback);
		if (readback & TEGRA20_AC97_STATUS1_STA_VALID1)
			break;
		usleep_range(1000, 2000);
	} while (!time_after(jiffies, timeout));

	return ((readback & TEGRA20_AC97_STATUS1_STA_DATA1_MASK) >>
		TEGRA20_AC97_STATUS1_STA_DATA1_SHIFT);
}

static void tegra20_ac97_codec_write(struct snd_ac97 *ac97_snd,
				     unsigned short reg, unsigned short val)
{
	u32 readback;
	unsigned long timeout;

	regmap_write(workdata->regmap, TEGRA20_AC97_CMD,
		     ((reg << TEGRA20_AC97_CMD_CMD_ADDR_SHIFT) &
		      TEGRA20_AC97_CMD_CMD_ADDR_MASK) |
		     ((val << TEGRA20_AC97_CMD_CMD_DATA_SHIFT) &
		      TEGRA20_AC97_CMD_CMD_DATA_MASK) |
		     TEGRA20_AC97_CMD_BUSY);

	timeout = jiffies + msecs_to_jiffies(100);

	do {
		regmap_read(workdata->regmap, TEGRA20_AC97_CMD, &readback);
		if (!(readback & TEGRA20_AC97_CMD_BUSY))
			break;
		usleep_range(1000, 2000);
	} while (!time_after(jiffies, timeout));
}

static struct snd_ac97_bus_ops tegra20_ac97_ops = {
	.read		= tegra20_ac97_codec_read,
	.write		= tegra20_ac97_codec_write,
	.reset		= tegra20_ac97_codec_reset,
	.warm_reset	= tegra20_ac97_codec_warm_reset,
};

static inline void tegra20_ac97_start_playback(struct tegra20_ac97 *ac97)
{
	regmap_update_bits(ac97->regmap, TEGRA20_AC97_FIFO1_SCR,
			   TEGRA20_AC97_FIFO_SCR_PB_QRT_MT_EN,
			   TEGRA20_AC97_FIFO_SCR_PB_QRT_MT_EN);

	regmap_update_bits(ac97->regmap, TEGRA20_AC97_CTRL,
			   TEGRA20_AC97_CTRL_PCM_DAC_EN |
			   TEGRA20_AC97_CTRL_STM_EN,
			   TEGRA20_AC97_CTRL_PCM_DAC_EN |
			   TEGRA20_AC97_CTRL_STM_EN);
}

static inline void tegra20_ac97_stop_playback(struct tegra20_ac97 *ac97)
{
	regmap_update_bits(ac97->regmap, TEGRA20_AC97_FIFO1_SCR,
			   TEGRA20_AC97_FIFO_SCR_PB_QRT_MT_EN, 0);

	regmap_update_bits(ac97->regmap, TEGRA20_AC97_CTRL,
			   TEGRA20_AC97_CTRL_PCM_DAC_EN, 0);
}

static inline void tegra20_ac97_start_capture(struct tegra20_ac97 *ac97)
{
	regmap_update_bits(ac97->regmap, TEGRA20_AC97_FIFO1_SCR,
			   TEGRA20_AC97_FIFO_SCR_REC_FULL_EN,
			   TEGRA20_AC97_FIFO_SCR_REC_FULL_EN);
}

static inline void tegra20_ac97_stop_capture(struct tegra20_ac97 *ac97)
{
	regmap_update_bits(ac97->regmap, TEGRA20_AC97_FIFO1_SCR,
			   TEGRA20_AC97_FIFO_SCR_REC_FULL_EN, 0);
}

static int tegra20_ac97_trigger(struct snd_pcm_substream *substream, int cmd,
				struct snd_soc_dai *dai)
{
	struct tegra20_ac97 *ac97 = snd_soc_dai_get_drvdata(dai);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
	case SNDRV_PCM_TRIGGER_RESUME:
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			tegra20_ac97_start_playback(ac97);
		else
			tegra20_ac97_start_capture(ac97);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
			tegra20_ac97_stop_playback(ac97);
		else
			tegra20_ac97_stop_capture(ac97);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static const struct snd_soc_dai_ops tegra20_ac97_dai_ops = {
	.trigger	= tegra20_ac97_trigger,
};

static int tegra20_ac97_probe(struct snd_soc_dai *dai)
{
	struct tegra20_ac97 *ac97 = snd_soc_dai_get_drvdata(dai);

	snd_soc_dai_init_dma_data(dai,	&ac97->playback_dma_data,
					&ac97->capture_dma_data);

	return 0;
}

static struct snd_soc_dai_driver tegra20_ac97_dai = {
	.name = "tegra-ac97-pcm",
	.probe = tegra20_ac97_probe,
	.playback = {
		.stream_name = "PCM Playback",
		.channels_min = 2,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000_48000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
	.capture = {
		.stream_name = "PCM Capture",
		.channels_min = 2,
		.channels_max = 2,
		.rates = SNDRV_PCM_RATE_8000_48000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE,
	},
	.ops = &tegra20_ac97_dai_ops,
};

static const struct snd_soc_component_driver tegra20_ac97_component = {
	.name			= DRV_NAME,
	.legacy_dai_naming	= 1,
};

static bool tegra20_ac97_wr_rd_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case TEGRA20_AC97_CTRL:
	case TEGRA20_AC97_CMD:
	case TEGRA20_AC97_STATUS1:
	case TEGRA20_AC97_FIFO1_SCR:
	case TEGRA20_AC97_FIFO_TX1:
	case TEGRA20_AC97_FIFO_RX1:
		return true;
	default:
		break;
	}

	return false;
}

static bool tegra20_ac97_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case TEGRA20_AC97_STATUS1:
	case TEGRA20_AC97_FIFO1_SCR:
	case TEGRA20_AC97_FIFO_TX1:
	case TEGRA20_AC97_FIFO_RX1:
		return true;
	default:
		break;
	}

	return false;
}

static bool tegra20_ac97_precious_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case TEGRA20_AC97_FIFO_TX1:
	case TEGRA20_AC97_FIFO_RX1:
		return true;
	default:
		break;
	}

	return false;
}

static const struct regmap_config tegra20_ac97_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = TEGRA20_AC97_FIFO_RX1,
	.writeable_reg = tegra20_ac97_wr_rd_reg,
	.readable_reg = tegra20_ac97_wr_rd_reg,
	.volatile_reg = tegra20_ac97_volatile_reg,
	.precious_reg = tegra20_ac97_precious_reg,
	.cache_type = REGCACHE_FLAT,
};

static int tegra20_ac97_platform_probe(struct platform_device *pdev)
{
	struct tegra20_ac97 *ac97;
	struct resource *mem;
	void __iomem *regs;
	int ret = 0;

	ac97 = devm_kzalloc(&pdev->dev, sizeof(struct tegra20_ac97),
			    GFP_KERNEL);
	if (!ac97) {
		ret = -ENOMEM;
		goto err;
	}
	dev_set_drvdata(&pdev->dev, ac97);

	ac97->reset = devm_reset_control_get_exclusive(&pdev->dev, "ac97");
	if (IS_ERR(ac97->reset)) {
		dev_err(&pdev->dev, "Can't retrieve ac97 reset\n");
		ret = PTR_ERR(ac97->reset);
		goto err;
	}

	ac97->clk_ac97 = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(ac97->clk_ac97)) {
		dev_err(&pdev->dev, "Can't retrieve ac97 clock\n");
		ret = PTR_ERR(ac97->clk_ac97);
		goto err;
	}

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	regs = devm_ioremap_resource(&pdev->dev, mem);
	if (IS_ERR(regs)) {
		ret = PTR_ERR(regs);
		goto err_clk_put;
	}

	ac97->regmap = devm_regmap_init_mmio(&pdev->dev, regs,
					    &tegra20_ac97_regmap_config);
	if (IS_ERR(ac97->regmap)) {
		dev_err(&pdev->dev, "regmap init failed\n");
		ret = PTR_ERR(ac97->regmap);
		goto err_clk_put;
	}

	ac97->reset_gpio = of_get_named_gpio(pdev->dev.of_node,
					     "nvidia,codec-reset-gpio", 0);
	if (gpio_is_valid(ac97->reset_gpio)) {
		ret = devm_gpio_request_one(&pdev->dev, ac97->reset_gpio,
					    GPIOF_OUT_INIT_HIGH, "codec-reset");
		if (ret) {
			dev_err(&pdev->dev, "could not get codec-reset GPIO\n");
			goto err_clk_put;
		}
	} else {
		dev_err(&pdev->dev, "no codec-reset GPIO supplied\n");
		ret = -EINVAL;
		goto err_clk_put;
	}

	ac97->sync_gpio = of_get_named_gpio(pdev->dev.of_node,
					    "nvidia,codec-sync-gpio", 0);
	if (!gpio_is_valid(ac97->sync_gpio)) {
		dev_err(&pdev->dev, "no codec-sync GPIO supplied\n");
		ret = -EINVAL;
		goto err_clk_put;
	}

	ac97->capture_dma_data.addr = mem->start + TEGRA20_AC97_FIFO_RX1;
	ac97->capture_dma_data.addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	ac97->capture_dma_data.maxburst = 4;

	ac97->playback_dma_data.addr = mem->start + TEGRA20_AC97_FIFO_TX1;
	ac97->playback_dma_data.addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	ac97->playback_dma_data.maxburst = 4;

	ret = reset_control_assert(ac97->reset);
	if (ret) {
		dev_err(&pdev->dev, "Failed to assert AC'97 reset: %d\n", ret);
		goto err_clk_put;
	}

	ret = clk_prepare_enable(ac97->clk_ac97);
	if (ret) {
		dev_err(&pdev->dev, "clk_enable failed: %d\n", ret);
		goto err_clk_put;
	}

	usleep_range(10, 100);

	ret = reset_control_deassert(ac97->reset);
	if (ret) {
		dev_err(&pdev->dev, "Failed to deassert AC'97 reset: %d\n", ret);
		goto err_clk_disable_unprepare;
	}

	ret = snd_soc_set_ac97_ops(&tegra20_ac97_ops);
	if (ret) {
		dev_err(&pdev->dev, "Failed to set AC'97 ops: %d\n", ret);
		goto err_clk_disable_unprepare;
	}

	ret = snd_soc_register_component(&pdev->dev, &tegra20_ac97_component,
					 &tegra20_ac97_dai, 1);
	if (ret) {
		dev_err(&pdev->dev, "Could not register DAI: %d\n", ret);
		ret = -ENOMEM;
		goto err_clk_disable_unprepare;
	}

	ret = tegra_pcm_platform_register(&pdev->dev);
	if (ret) {
		dev_err(&pdev->dev, "Could not register PCM: %d\n", ret);
		goto err_unregister_component;
	}

	/* XXX: crufty ASoC AC97 API - only one AC97 codec allowed */
	workdata = ac97;

	return 0;

err_unregister_component:
	snd_soc_unregister_component(&pdev->dev);
err_clk_disable_unprepare:
	clk_disable_unprepare(ac97->clk_ac97);
err_clk_put:
err:
	snd_soc_set_ac97_ops(NULL);
	return ret;
}

static void tegra20_ac97_platform_remove(struct platform_device *pdev)
{
	struct tegra20_ac97 *ac97 = dev_get_drvdata(&pdev->dev);

	tegra_pcm_platform_unregister(&pdev->dev);
	snd_soc_unregister_component(&pdev->dev);

	clk_disable_unprepare(ac97->clk_ac97);

	snd_soc_set_ac97_ops(NULL);
}

static const struct of_device_id tegra20_ac97_of_match[] = {
	{ .compatible = "nvidia,tegra20-ac97", },
	{},
};

static struct platform_driver tegra20_ac97_driver = {
	.driver = {
		.name = DRV_NAME,
		.of_match_table = tegra20_ac97_of_match,
	},
	.probe = tegra20_ac97_platform_probe,
	.remove_new = tegra20_ac97_platform_remove,
};
module_platform_driver(tegra20_ac97_driver);

MODULE_AUTHOR("Lucas Stach");
MODULE_DESCRIPTION("Tegra20 AC97 ASoC driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" DRV_NAME);
MODULE_DEVICE_TABLE(of, tegra20_ac97_of_match);
