// SPDX-License-Identifier: GPL-2.0+
//
// AMD ALSA SoC PCM Driver
//
// Copyright (C) 2021 Advanced Micro Devices, Inc. All rights reserved.

#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/io.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dai.h>

#include "acp5x.h"

#define DRV_NAME "acp5x_i2s_dma"

static const struct snd_soc_component_driver acp5x_i2s_component = {
	.name		= DRV_NAME,
};

static irqreturn_t i2s_irq_handler(int irq, void *dev_id)
{
	struct i2s_dev_data *vg_i2s_data;
	u16 irq_flag;
	u32 val;

	vg_i2s_data = dev_id;
	if (!vg_i2s_data)
		return IRQ_NONE;

	irq_flag = 0;
	val = acp_readl(vg_i2s_data->acp5x_base + ACP_EXTERNAL_INTR_STAT);
	if ((val & BIT(HS_TX_THRESHOLD)) && vg_i2s_data->play_stream) {
		acp_writel(BIT(HS_TX_THRESHOLD), vg_i2s_data->acp5x_base +
			   ACP_EXTERNAL_INTR_STAT);
		snd_pcm_period_elapsed(vg_i2s_data->play_stream);
		irq_flag = 1;
	}
	if ((val & BIT(I2S_TX_THRESHOLD)) && vg_i2s_data->i2ssp_play_stream) {
		acp_writel(BIT(I2S_TX_THRESHOLD),
			   vg_i2s_data->acp5x_base + ACP_EXTERNAL_INTR_STAT);
		snd_pcm_period_elapsed(vg_i2s_data->i2ssp_play_stream);
		irq_flag = 1;
	}

	if ((val & BIT(HS_RX_THRESHOLD)) && vg_i2s_data->capture_stream) {
		acp_writel(BIT(HS_RX_THRESHOLD), vg_i2s_data->acp5x_base +
			   ACP_EXTERNAL_INTR_STAT);
		snd_pcm_period_elapsed(vg_i2s_data->capture_stream);
		irq_flag = 1;
	}
	if ((val & BIT(I2S_RX_THRESHOLD)) && vg_i2s_data->i2ssp_capture_stream) {
		acp_writel(BIT(I2S_RX_THRESHOLD),
			   vg_i2s_data->acp5x_base + ACP_EXTERNAL_INTR_STAT);
		snd_pcm_period_elapsed(vg_i2s_data->i2ssp_capture_stream);
		irq_flag = 1;
	}

	if (irq_flag)
		return IRQ_HANDLED;
	else
		return IRQ_NONE;
}

static int acp5x_audio_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct i2s_dev_data *adata;
	unsigned int irqflags;
	int status;

	if (!pdev->dev.platform_data) {
		dev_err(&pdev->dev, "platform_data not retrieved\n");
		return -ENODEV;
	}
	irqflags = *((unsigned int *)(pdev->dev.platform_data));

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		dev_err(&pdev->dev, "IORESOURCE_MEM FAILED\n");
		return -ENODEV;
	}

	adata = devm_kzalloc(&pdev->dev, sizeof(*adata), GFP_KERNEL);
	if (!adata)
		return -ENOMEM;

	adata->acp5x_base = devm_ioremap(&pdev->dev, res->start,
					 resource_size(res));
	if (!adata->acp5x_base)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!res) {
		dev_err(&pdev->dev, "IORESOURCE_IRQ FAILED\n");
		return -ENODEV;
	}

	adata->i2s_irq = res->start;
	dev_set_drvdata(&pdev->dev, adata);
	status = devm_snd_soc_register_component(&pdev->dev,
						 &acp5x_i2s_component,
						 NULL, 0);
	if (status) {
		dev_err(&pdev->dev, "Fail to register acp i2s component\n");
		return status;
	}
	status = devm_request_irq(&pdev->dev, adata->i2s_irq, i2s_irq_handler,
				  irqflags, "ACP5x_I2S_IRQ", adata);
	if (status)
		dev_err(&pdev->dev, "ACP5x I2S IRQ request failed\n");

	return status;
}

static struct platform_driver acp5x_dma_driver = {
	.probe = acp5x_audio_probe,
	.driver = {
		.name = "acp5x_i2s_dma",
	},
};

module_platform_driver(acp5x_dma_driver);

MODULE_AUTHOR("Vijendar.Mukunda@amd.com");
MODULE_DESCRIPTION("AMD ACP 5.x PCM Driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" DRV_NAME);
