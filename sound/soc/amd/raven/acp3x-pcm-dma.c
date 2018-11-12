/*
 * AMD ALSA SoC PCM Driver
 *
 * Copyright 2016 Advanced Micro Devices, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/io.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dai.h>

#include "acp3x.h"

#define DRV_NAME "acp3x-i2s-audio"

struct i2s_dev_data {
	void __iomem *acp3x_base;
	struct snd_pcm_substream *play_stream;
	struct snd_pcm_substream *capture_stream;
};

static int acp3x_power_on(void __iomem *acp3x_base, bool on)
{
	u16 val, mask;
	u32 timeout;

	if (on == true) {
		val = 1;
		mask = ACP3x_POWER_ON;
	} else {
		val = 0;
		mask = ACP3x_POWER_OFF;
	}

	rv_writel(val, acp3x_base + mmACP_PGFSM_CONTROL);
	timeout = 0;
	while (true) {
		val = rv_readl(acp3x_base + mmACP_PGFSM_STATUS);
		if ((val & ACP3x_POWER_OFF_IN_PROGRESS) == mask)
			break;
		if (timeout > 100) {
			pr_err("ACP3x power state change failure\n");
			return -ENODEV;
		}
		timeout++;
		cpu_relax();
	}
	return 0;
}

static int acp3x_reset(void __iomem *acp3x_base)
{
	u32 val, timeout;

	rv_writel(1, acp3x_base + mmACP_SOFT_RESET);
	timeout = 0;
	while (true) {
		val = rv_readl(acp3x_base + mmACP_SOFT_RESET);
		if ((val & ACP3x_SOFT_RESET__SoftResetAudDone_MASK) ||
		     timeout > 100) {
			if (val & ACP3x_SOFT_RESET__SoftResetAudDone_MASK)
				break;
			return -ENODEV;
		}
		timeout++;
		cpu_relax();
	}

	rv_writel(0, acp3x_base + mmACP_SOFT_RESET);
	timeout = 0;
	while (true) {
		val = rv_readl(acp3x_base + mmACP_SOFT_RESET);
		if (!val || timeout > 100) {
			if (!val)
				break;
			return -ENODEV;
		}
		timeout++;
		cpu_relax();
	}
	return 0;
}

static int acp3x_init(void __iomem *acp3x_base)
{
	int ret;

	/* power on */
	ret = acp3x_power_on(acp3x_base, true);
	if (ret) {
		pr_err("ACP3x power on failed\n");
		return ret;
	}
	/* Reset */
	ret = acp3x_reset(acp3x_base);
	if (ret) {
		pr_err("ACP3x reset failed\n");
		return ret;
	}
	return 0;
}

static int acp3x_deinit(void __iomem *acp3x_base)
{
	int ret;

	/* Reset */
	ret = acp3x_reset(acp3x_base);
	if (ret) {
		pr_err("ACP3x reset failed\n");
		return ret;
	}
	/* power off */
	ret = acp3x_power_on(acp3x_base, false);
	if (ret) {
		pr_err("ACP3x power off failed\n");
		return ret;
	}
	return 0;
}

static struct snd_pcm_ops acp3x_dma_ops = {
	.open = NULL,
	.close = NULL,
	.ioctl = NULL,
	.hw_params = NULL,
	.hw_free = NULL,
	.pointer = NULL,
	.mmap = NULL,
};

struct snd_soc_dai_ops acp3x_dai_i2s_ops = {
	.hw_params = NULL,
	.trigger   = NULL,
	.set_fmt = NULL,
};

static struct snd_soc_dai_driver acp3x_i2s_dai_driver = {
	.playback = {
		.rates = SNDRV_PCM_RATE_8000_96000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S8 |
					SNDRV_PCM_FMTBIT_U8 |
					SNDRV_PCM_FMTBIT_S24_LE |
					SNDRV_PCM_FMTBIT_S32_LE,
		.channels_min = 2,
		.channels_max = 8,

		.rate_min = 8000,
		.rate_max = 96000,
	},
	.capture = {
		.rates = SNDRV_PCM_RATE_8000_48000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S8 |
					SNDRV_PCM_FMTBIT_U8 |
					SNDRV_PCM_FMTBIT_S24_LE |
					SNDRV_PCM_FMTBIT_S32_LE,
		.channels_min = 2,
		.channels_max = 2,
		.rate_min = 8000,
		.rate_max = 48000,
	},
	.ops = &acp3x_dai_i2s_ops,
};

static const struct snd_soc_component_driver acp3x_i2s_component = {
	.name           = DRV_NAME,
	.ops		= &acp3x_dma_ops,
	.pcm_new	= acp3x_dma_new,
};

static int acp3x_audio_probe(struct platform_device *pdev)
{
	int status;
	struct resource *res;
	struct i2s_dev_data *adata;
	unsigned int irqflags;

	if (!pdev->dev.platform_data) {
		dev_err(&pdev->dev, "platform_data not retrieved\n");
		return -ENODEV;
	}
	irqflags = *((unsigned int *)(pdev->dev.platform_data));

	adata = devm_kzalloc(&pdev->dev, sizeof(struct i2s_dev_data),
			     GFP_KERNEL);
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "IORESOURCE_IRQ FAILED\n");
			return -ENODEV;
	}

	adata->acp3x_base = devm_ioremap(&pdev->dev, res->start,
					 resource_size(res));

	adata->play_stream = NULL;
	adata->capture_stream = NULL;

	dev_set_drvdata(&pdev->dev, adata);
	/* Initialize ACP */
	status = acp3x_init(adata->acp3x_base);
	if (status)
		return -ENODEV;
	status = devm_snd_soc_register_component(&pdev->dev,
						 &acp3x_i2s_component,
						 &acp3x_i2s_dai_driver, 1);
	if (status) {
		dev_err(&pdev->dev, "Fail to register acp i2s dai\n");
		goto dev_err;
	}

	return 0;
dev_err:
	status = acp3x_deinit(adata->acp3x_base);
	if (status)
		dev_err(&pdev->dev, "ACP de-init failed\n");
	else
		dev_info(&pdev->dev, "ACP de-initialized\n");
	/*ignore device status and return driver probe error*/
	return -ENODEV;
}

static int acp3x_audio_remove(struct platform_device *pdev)
{
	int ret;
	struct i2s_dev_data *adata = dev_get_drvdata(&pdev->dev);

	ret = acp3x_deinit(adata->acp3x_base);
	if (ret)
		dev_err(&pdev->dev, "ACP de-init failed\n");
	else
		dev_info(&pdev->dev, "ACP de-initialized\n");

	return 0;
}

static struct platform_driver acp3x_dma_driver = {
	.probe = acp3x_audio_probe,
	.remove = acp3x_audio_remove,
	.driver = {
		.name = "acp3x_rv_i2s",
	},
};

module_platform_driver(acp3x_dma_driver);

MODULE_AUTHOR("Maruthi.Bayyavarapu@amd.com");
MODULE_AUTHOR("Vijendar.Mukunda@amd.com");
MODULE_DESCRIPTION("AMD ACP 3.x PCM Driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" DRV_NAME);
