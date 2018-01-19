/*
 * ALSA SoC SPDIF In Audio Layer for spear processors
 *
 * Copyright (C) 2012 ST Microelectronics
 * Vipin Kumar <vipin.kumar@st.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <sound/dmaengine_pcm.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/spear_dma.h>
#include <sound/spear_spdif.h>
#include "spdif_in_regs.h"
#include "spear_pcm.h"

struct spdif_in_params {
	u32 format;
};

struct spdif_in_dev {
	struct clk *clk;
	struct spear_dma_data dma_params;
	struct spdif_in_params saved_params;
	void *io_base;
	struct device *dev;
	void (*reset_perip)(void);
	int irq;
	struct snd_dmaengine_dai_dma_data dma_params_rx;
	struct snd_dmaengine_pcm_config config;
};

static void spdif_in_configure(struct spdif_in_dev *host)
{
	u32 ctrl = SPDIF_IN_PRTYEN | SPDIF_IN_STATEN | SPDIF_IN_USREN |
		SPDIF_IN_VALEN | SPDIF_IN_BLKEN;
	ctrl |= SPDIF_MODE_16BIT | SPDIF_FIFO_THRES_16;

	writel(ctrl, host->io_base + SPDIF_IN_CTRL);
	writel(0xF, host->io_base + SPDIF_IN_IRQ_MASK);
}

static int spdif_in_dai_probe(struct snd_soc_dai *dai)
{
	struct spdif_in_dev *host = snd_soc_dai_get_drvdata(dai);

	host->dma_params_rx.filter_data = &host->dma_params;
	dai->capture_dma_data = &host->dma_params_rx;

	return 0;
}

static void spdif_in_shutdown(struct snd_pcm_substream *substream,
		struct snd_soc_dai *dai)
{
	struct spdif_in_dev *host = snd_soc_dai_get_drvdata(dai);

	if (substream->stream != SNDRV_PCM_STREAM_CAPTURE)
		return;

	writel(0x0, host->io_base + SPDIF_IN_IRQ_MASK);
}

static void spdif_in_format(struct spdif_in_dev *host, u32 format)
{
	u32 ctrl = readl(host->io_base + SPDIF_IN_CTRL);

	switch (format) {
	case SNDRV_PCM_FORMAT_S16_LE:
		ctrl |= SPDIF_XTRACT_16BIT;
		break;

	case SNDRV_PCM_FORMAT_IEC958_SUBFRAME_LE:
		ctrl &= ~SPDIF_XTRACT_16BIT;
		break;
	}

	writel(ctrl, host->io_base + SPDIF_IN_CTRL);
}

static int spdif_in_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params,
		struct snd_soc_dai *dai)
{
	struct spdif_in_dev *host = snd_soc_dai_get_drvdata(dai);
	u32 format;

	if (substream->stream != SNDRV_PCM_STREAM_CAPTURE)
		return -EINVAL;

	format = params_format(params);
	host->saved_params.format = format;

	return 0;
}

static int spdif_in_trigger(struct snd_pcm_substream *substream, int cmd,
		struct snd_soc_dai *dai)
{
	struct spdif_in_dev *host = snd_soc_dai_get_drvdata(dai);
	u32 ctrl;
	int ret = 0;

	if (substream->stream != SNDRV_PCM_STREAM_CAPTURE)
		return -EINVAL;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		clk_enable(host->clk);
		spdif_in_configure(host);
		spdif_in_format(host, host->saved_params.format);

		ctrl = readl(host->io_base + SPDIF_IN_CTRL);
		ctrl |= SPDIF_IN_SAMPLE | SPDIF_IN_ENB;
		writel(ctrl, host->io_base + SPDIF_IN_CTRL);
		writel(0xF, host->io_base + SPDIF_IN_IRQ_MASK);
		break;

	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		ctrl = readl(host->io_base + SPDIF_IN_CTRL);
		ctrl &= ~(SPDIF_IN_SAMPLE | SPDIF_IN_ENB);
		writel(ctrl, host->io_base + SPDIF_IN_CTRL);
		writel(0x0, host->io_base + SPDIF_IN_IRQ_MASK);

		if (host->reset_perip)
			host->reset_perip();
		clk_disable(host->clk);
		break;

	default:
		ret = -EINVAL;
		break;
	}
	return ret;
}

static const struct snd_soc_dai_ops spdif_in_dai_ops = {
	.shutdown	= spdif_in_shutdown,
	.trigger	= spdif_in_trigger,
	.hw_params	= spdif_in_hw_params,
};

static struct snd_soc_dai_driver spdif_in_dai = {
	.probe = spdif_in_dai_probe,
	.capture = {
		.channels_min = 2,
		.channels_max = 2,
		.rates = (SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 | \
				 SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_96000 | \
				 SNDRV_PCM_RATE_192000),
		.formats = SNDRV_PCM_FMTBIT_S16_LE | \
			   SNDRV_PCM_FMTBIT_IEC958_SUBFRAME_LE,
	},
	.ops = &spdif_in_dai_ops,
};

static const struct snd_soc_component_driver spdif_in_component = {
	.name		= "spdif-in",
};

static irqreturn_t spdif_in_irq(int irq, void *arg)
{
	struct spdif_in_dev *host = (struct spdif_in_dev *)arg;

	u32 irq_status = readl(host->io_base + SPDIF_IN_IRQ);

	if (!irq_status)
		return IRQ_NONE;

	if (irq_status & SPDIF_IRQ_FIFOWRITE)
		dev_err(host->dev, "spdif in: fifo write error");
	if (irq_status & SPDIF_IRQ_EMPTYFIFOREAD)
		dev_err(host->dev, "spdif in: empty fifo read error");
	if (irq_status & SPDIF_IRQ_FIFOFULL)
		dev_err(host->dev, "spdif in: fifo full error");
	if (irq_status & SPDIF_IRQ_OUTOFRANGE)
		dev_err(host->dev, "spdif in: out of range error");

	writel(0, host->io_base + SPDIF_IN_IRQ);

	return IRQ_HANDLED;
}

static int spdif_in_probe(struct platform_device *pdev)
{
	struct spdif_in_dev *host;
	struct spear_spdif_platform_data *pdata;
	struct resource *res, *res_fifo;
	void __iomem *io_base;
	int ret;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	io_base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(io_base))
		return PTR_ERR(io_base);

	res_fifo = platform_get_resource(pdev, IORESOURCE_IO, 0);
	if (!res_fifo)
		return -EINVAL;

	host = devm_kzalloc(&pdev->dev, sizeof(*host), GFP_KERNEL);
	if (!host)
		return -ENOMEM;

	host->io_base = io_base;
	host->irq = platform_get_irq(pdev, 0);
	if (host->irq < 0) {
		dev_warn(&pdev->dev, "failed to get IRQ: %d\n", host->irq);
		return host->irq;
	}

	host->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(host->clk))
		return PTR_ERR(host->clk);

	pdata = dev_get_platdata(&pdev->dev);

	if (!pdata)
		return -EINVAL;

	host->dma_params.data = pdata->dma_params;
	host->dma_params.addr = res_fifo->start;
	host->dma_params.max_burst = 16;
	host->dma_params.addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
	host->reset_perip = pdata->reset_perip;

	host->dev = &pdev->dev;
	dev_set_drvdata(&pdev->dev, host);

	ret = devm_request_irq(&pdev->dev, host->irq, spdif_in_irq, 0,
			"spdif-in", host);
	if (ret) {
		dev_warn(&pdev->dev, "request_irq failed\n");
		return ret;
	}

	ret = devm_snd_soc_register_component(&pdev->dev, &spdif_in_component,
					      &spdif_in_dai, 1);
	if (ret)
		return ret;

	return devm_spear_pcm_platform_register(&pdev->dev, &host->config,
						pdata->filter);
}

static struct platform_driver spdif_in_driver = {
	.probe		= spdif_in_probe,
	.driver		= {
		.name	= "spdif-in",
	},
};

module_platform_driver(spdif_in_driver);

MODULE_AUTHOR("Vipin Kumar <vipin.kumar@st.com>");
MODULE_DESCRIPTION("SPEAr SPDIF IN SoC Interface");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:spdif_in");
