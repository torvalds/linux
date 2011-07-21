/*
 * Copyright 2011 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/dma-mapping.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <mach/dma.h>
#include <asm/mach-types.h>
#include <mach/hardware.h>
#include <mach/mxs.h>

#include "mxs-saif.h"

static struct mxs_saif *mxs_saif[2];

static int mxs_saif_set_dai_sysclk(struct snd_soc_dai *cpu_dai,
			int clk_id, unsigned int freq, int dir)
{
	struct mxs_saif *saif = snd_soc_dai_get_drvdata(cpu_dai);

	switch (clk_id) {
	case MXS_SAIF_MCLK:
		saif->mclk = freq;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

/*
 * Set SAIF clock and MCLK
 */
static int mxs_saif_set_clk(struct mxs_saif *saif,
				  unsigned int mclk,
				  unsigned int rate)
{
	u32 scr;
	int ret;

	scr = __raw_readl(saif->base + SAIF_CTRL);
	scr &= ~BM_SAIF_CTRL_BITCLK_MULT_RATE;
	scr &= ~BM_SAIF_CTRL_BITCLK_BASE_RATE;

	/*
	 * Set SAIF clock
	 *
	 * The SAIF clock should be either 384*fs or 512*fs.
	 * If MCLK is used, the SAIF clk ratio need to match mclk ratio.
	 *  For 32x mclk, set saif clk as 512*fs.
	 *  For 48x mclk, set saif clk as 384*fs.
	 *
	 * If MCLK is not used, we just set saif clk to 512*fs.
	 */
	if (saif->mclk_in_use) {
		if (mclk % 32 == 0) {
			scr &= ~BM_SAIF_CTRL_BITCLK_BASE_RATE;
			ret = clk_set_rate(saif->clk, 512 * rate);
		} else if (mclk % 48 == 0) {
			scr |= BM_SAIF_CTRL_BITCLK_BASE_RATE;
			ret = clk_set_rate(saif->clk, 384 * rate);
		} else {
			/* SAIF MCLK should be either 32x or 48x */
			return -EINVAL;
		}
	} else {
		ret = clk_set_rate(saif->clk, 512 * rate);
		scr &= ~BM_SAIF_CTRL_BITCLK_BASE_RATE;
	}

	if (ret)
		return ret;

	if (!saif->mclk_in_use) {
		__raw_writel(scr, saif->base + SAIF_CTRL);
		return 0;
	}

	/*
	 * Program the over-sample rate for MCLK output
	 *
	 * The available MCLK range is 32x, 48x... 512x. The rate
	 * could be from 8kHz to 192kH.
	 */
	switch (mclk / rate) {
	case 32:
		scr |= BF_SAIF_CTRL_BITCLK_MULT_RATE(4);
		break;
	case 64:
		scr |= BF_SAIF_CTRL_BITCLK_MULT_RATE(3);
		break;
	case 128:
		scr |= BF_SAIF_CTRL_BITCLK_MULT_RATE(2);
		break;
	case 256:
		scr |= BF_SAIF_CTRL_BITCLK_MULT_RATE(1);
		break;
	case 512:
		scr |= BF_SAIF_CTRL_BITCLK_MULT_RATE(0);
		break;
	case 48:
		scr |= BF_SAIF_CTRL_BITCLK_MULT_RATE(3);
		break;
	case 96:
		scr |= BF_SAIF_CTRL_BITCLK_MULT_RATE(2);
		break;
	case 192:
		scr |= BF_SAIF_CTRL_BITCLK_MULT_RATE(1);
		break;
	case 384:
		scr |= BF_SAIF_CTRL_BITCLK_MULT_RATE(0);
		break;
	default:
		return -EINVAL;
	}

	__raw_writel(scr, saif->base + SAIF_CTRL);

	return 0;
}

/*
 * Put and disable MCLK.
 */
int mxs_saif_put_mclk(unsigned int saif_id)
{
	struct mxs_saif *saif = mxs_saif[saif_id];
	u32 stat;

	if (!saif)
		return -EINVAL;

	stat = __raw_readl(saif->base + SAIF_STAT);
	if (stat & BM_SAIF_STAT_BUSY) {
		dev_err(saif->dev, "error: busy\n");
		return -EBUSY;
	}

	clk_disable(saif->clk);

	/* disable MCLK output */
	__raw_writel(BM_SAIF_CTRL_CLKGATE,
		saif->base + SAIF_CTRL + MXS_SET_ADDR);
	__raw_writel(BM_SAIF_CTRL_RUN,
		saif->base + SAIF_CTRL + MXS_CLR_ADDR);

	saif->mclk_in_use = 0;
	return 0;
}

/*
 * Get MCLK and set clock rate, then enable it
 *
 * This interface is used for codecs who are using MCLK provided
 * by saif.
 */
int mxs_saif_get_mclk(unsigned int saif_id, unsigned int mclk,
					unsigned int rate)
{
	struct mxs_saif *saif = mxs_saif[saif_id];
	u32 stat;
	int ret;

	if (!saif)
		return -EINVAL;

	stat = __raw_readl(saif->base + SAIF_STAT);
	if (stat & BM_SAIF_STAT_BUSY) {
		dev_err(saif->dev, "error: busy\n");
		return -EBUSY;
	}

	/* Clear Reset */
	__raw_writel(BM_SAIF_CTRL_SFTRST,
		saif->base + SAIF_CTRL + MXS_CLR_ADDR);

	saif->mclk_in_use = 1;
	ret = mxs_saif_set_clk(saif, mclk, rate);
	if (ret)
		return ret;

	ret = clk_enable(saif->clk);
	if (ret)
		return ret;

	/* enable MCLK output */
	__raw_writel(BM_SAIF_CTRL_CLKGATE,
		saif->base + SAIF_CTRL + MXS_CLR_ADDR);
	__raw_writel(BM_SAIF_CTRL_RUN,
		saif->base + SAIF_CTRL + MXS_SET_ADDR);

	return 0;
}

/*
 * SAIF DAI format configuration.
 * Should only be called when port is inactive.
 */
static int mxs_saif_set_dai_fmt(struct snd_soc_dai *cpu_dai, unsigned int fmt)
{
	u32 scr, stat;
	u32 scr0;
	struct mxs_saif *saif = snd_soc_dai_get_drvdata(cpu_dai);

	stat = __raw_readl(saif->base + SAIF_STAT);
	if (stat & BM_SAIF_STAT_BUSY) {
		dev_err(cpu_dai->dev, "error: busy\n");
		return -EBUSY;
	}

	scr0 = __raw_readl(saif->base + SAIF_CTRL);
	scr0 = scr0 & ~BM_SAIF_CTRL_BITCLK_EDGE & ~BM_SAIF_CTRL_LRCLK_POLARITY \
		& ~BM_SAIF_CTRL_JUSTIFY & ~BM_SAIF_CTRL_DELAY;
	scr = 0;

	/* DAI mode */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		/* data frame low 1clk before data */
		scr |= BM_SAIF_CTRL_DELAY;
		scr &= ~BM_SAIF_CTRL_LRCLK_POLARITY;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		/* data frame high with data */
		scr &= ~BM_SAIF_CTRL_DELAY;
		scr &= ~BM_SAIF_CTRL_LRCLK_POLARITY;
		scr &= ~BM_SAIF_CTRL_JUSTIFY;
		break;
	default:
		return -EINVAL;
	}

	/* DAI clock inversion */
	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_IB_IF:
		scr |= BM_SAIF_CTRL_BITCLK_EDGE;
		scr |= BM_SAIF_CTRL_LRCLK_POLARITY;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		scr |= BM_SAIF_CTRL_BITCLK_EDGE;
		scr &= ~BM_SAIF_CTRL_LRCLK_POLARITY;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		scr &= ~BM_SAIF_CTRL_BITCLK_EDGE;
		scr |= BM_SAIF_CTRL_LRCLK_POLARITY;
		break;
	case SND_SOC_DAIFMT_NB_NF:
		scr &= ~BM_SAIF_CTRL_BITCLK_EDGE;
		scr &= ~BM_SAIF_CTRL_LRCLK_POLARITY;
		break;
	}

	/*
	 * Note: We simply just support master mode since SAIF TX can only
	 * work as master.
	 */
	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		scr &= ~BM_SAIF_CTRL_SLAVE_MODE;
		__raw_writel(scr | scr0, saif->base + SAIF_CTRL);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int mxs_saif_startup(struct snd_pcm_substream *substream,
			   struct snd_soc_dai *cpu_dai)
{
	struct mxs_saif *saif = snd_soc_dai_get_drvdata(cpu_dai);
	snd_soc_dai_set_dma_data(cpu_dai, substream, &saif->dma_param);

	/* clear error status to 0 for each re-open */
	saif->fifo_underrun = 0;
	saif->fifo_overrun = 0;

	/* Clear Reset for normal operations */
	__raw_writel(BM_SAIF_CTRL_SFTRST,
		saif->base + SAIF_CTRL + MXS_CLR_ADDR);

	return 0;
}

/*
 * Should only be called when port is inactive.
 * although can be called multiple times by upper layers.
 */
static int mxs_saif_hw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *params,
			     struct snd_soc_dai *cpu_dai)
{
	struct mxs_saif *saif = snd_soc_dai_get_drvdata(cpu_dai);
	u32 scr, stat;
	int ret;

	/* mclk should already be set */
	if (!saif->mclk && saif->mclk_in_use) {
		dev_err(cpu_dai->dev, "set mclk first\n");
		return -EINVAL;
	}

	stat = __raw_readl(saif->base + SAIF_STAT);
	if (stat & BM_SAIF_STAT_BUSY) {
		dev_err(cpu_dai->dev, "error: busy\n");
		return -EBUSY;
	}

	/*
	 * Set saif clk based on sample rate.
	 * If mclk is used, we also set mclk, if not, saif->mclk is
	 * default 0, means not used.
	 */
	ret = mxs_saif_set_clk(saif, saif->mclk, params_rate(params));
	if (ret) {
		dev_err(cpu_dai->dev, "unable to get proper clk\n");
		return ret;
	}

	scr = __raw_readl(saif->base + SAIF_CTRL);

	scr &= ~BM_SAIF_CTRL_WORD_LENGTH;
	scr &= ~BM_SAIF_CTRL_BITCLK_48XFS_ENABLE;
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		scr |= BF_SAIF_CTRL_WORD_LENGTH(0);
		break;
	case SNDRV_PCM_FORMAT_S20_3LE:
		scr |= BF_SAIF_CTRL_WORD_LENGTH(4);
		scr |= BM_SAIF_CTRL_BITCLK_48XFS_ENABLE;
		break;
	case SNDRV_PCM_FORMAT_S24_LE:
		scr |= BF_SAIF_CTRL_WORD_LENGTH(8);
		scr |= BM_SAIF_CTRL_BITCLK_48XFS_ENABLE;
		break;
	default:
		return -EINVAL;
	}

	/* Tx/Rx config */
	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		/* enable TX mode */
		scr &= ~BM_SAIF_CTRL_READ_MODE;
	} else {
		/* enable RX mode */
		scr |= BM_SAIF_CTRL_READ_MODE;
	}

	__raw_writel(scr, saif->base + SAIF_CTRL);
	return 0;
}

static int mxs_saif_prepare(struct snd_pcm_substream *substream,
			   struct snd_soc_dai *cpu_dai)
{
	struct mxs_saif *saif = snd_soc_dai_get_drvdata(cpu_dai);

	/* clear clock gate */
	__raw_writel(BM_SAIF_CTRL_CLKGATE,
		saif->base + SAIF_CTRL + MXS_CLR_ADDR);

	/* enable FIFO error irqs */
	__raw_writel(BM_SAIF_CTRL_FIFO_ERROR_IRQ_EN,
		saif->base + SAIF_CTRL + MXS_SET_ADDR);

	return 0;
}

static int mxs_saif_trigger(struct snd_pcm_substream *substream, int cmd,
				struct snd_soc_dai *cpu_dai)
{
	struct mxs_saif *saif = snd_soc_dai_get_drvdata(cpu_dai);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		dev_dbg(cpu_dai->dev, "start\n");

		clk_enable(saif->clk);
		if (!saif->mclk_in_use)
			__raw_writel(BM_SAIF_CTRL_RUN,
				saif->base + SAIF_CTRL + MXS_SET_ADDR);

		if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
			/*
			 * write a data to saif data register to trigger
			 * the transfer
			 */
			__raw_writel(0, saif->base + SAIF_DATA);
		} else {
			/*
			 * read a data from saif data register to trigger
			 * the receive
			 */
			__raw_readl(saif->base + SAIF_DATA);
		}

		dev_dbg(cpu_dai->dev, "CTRL 0x%x STAT 0x%x\n",
			__raw_readl(saif->base + SAIF_CTRL),
			__raw_readl(saif->base + SAIF_STAT));

		break;
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		dev_dbg(cpu_dai->dev, "stop\n");

		clk_disable(saif->clk);
		if (!saif->mclk_in_use)
			__raw_writel(BM_SAIF_CTRL_RUN,
				saif->base + SAIF_CTRL + MXS_CLR_ADDR);

		break;
	default:
		return -EINVAL;
	}

	return 0;
}

#define MXS_SAIF_RATES		SNDRV_PCM_RATE_8000_192000
#define MXS_SAIF_FORMATS \
	(SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S20_3LE | \
	SNDRV_PCM_FMTBIT_S24_LE)

static struct snd_soc_dai_ops mxs_saif_dai_ops = {
	.startup = mxs_saif_startup,
	.trigger = mxs_saif_trigger,
	.prepare = mxs_saif_prepare,
	.hw_params = mxs_saif_hw_params,
	.set_sysclk = mxs_saif_set_dai_sysclk,
	.set_fmt = mxs_saif_set_dai_fmt,
};

static int mxs_saif_dai_probe(struct snd_soc_dai *dai)
{
	struct mxs_saif *saif = dev_get_drvdata(dai->dev);

	snd_soc_dai_set_drvdata(dai, saif);

	return 0;
}

static struct snd_soc_dai_driver mxs_saif_dai = {
	.name = "mxs-saif",
	.probe = mxs_saif_dai_probe,
	.playback = {
		.channels_min = 2,
		.channels_max = 2,
		.rates = MXS_SAIF_RATES,
		.formats = MXS_SAIF_FORMATS,
	},
	.capture = {
		.channels_min = 2,
		.channels_max = 2,
		.rates = MXS_SAIF_RATES,
		.formats = MXS_SAIF_FORMATS,
	},
	.ops = &mxs_saif_dai_ops,
};

static irqreturn_t mxs_saif_irq(int irq, void *dev_id)
{
	struct mxs_saif *saif = dev_id;
	unsigned int stat;

	stat = __raw_readl(saif->base + SAIF_STAT);
	if (!(stat & (BM_SAIF_STAT_FIFO_UNDERFLOW_IRQ |
			BM_SAIF_STAT_FIFO_OVERFLOW_IRQ)))
		return IRQ_NONE;

	if (stat & BM_SAIF_STAT_FIFO_UNDERFLOW_IRQ) {
		dev_dbg(saif->dev, "underrun!!! %d\n", ++saif->fifo_underrun);
		__raw_writel(BM_SAIF_STAT_FIFO_UNDERFLOW_IRQ,
				saif->base + SAIF_STAT + MXS_CLR_ADDR);
	}

	if (stat & BM_SAIF_STAT_FIFO_OVERFLOW_IRQ) {
		dev_dbg(saif->dev, "overrun!!! %d\n", ++saif->fifo_overrun);
		__raw_writel(BM_SAIF_STAT_FIFO_OVERFLOW_IRQ,
				saif->base + SAIF_STAT + MXS_CLR_ADDR);
	}

	dev_dbg(saif->dev, "SAIF_CTRL %x SAIF_STAT %x\n",
	       __raw_readl(saif->base + SAIF_CTRL),
	       __raw_readl(saif->base + SAIF_STAT));

	return IRQ_HANDLED;
}

static int mxs_saif_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct mxs_saif *saif;
	int ret = 0;

	saif = kzalloc(sizeof(*saif), GFP_KERNEL);
	if (!saif)
		return -ENOMEM;

	if (pdev->id >= ARRAY_SIZE(mxs_saif))
		return -EINVAL;
	mxs_saif[pdev->id] = saif;

	saif->clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(saif->clk)) {
		ret = PTR_ERR(saif->clk);
		dev_err(&pdev->dev, "Cannot get the clock: %d\n",
			ret);
		goto failed_clk;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		ret = -ENODEV;
		dev_err(&pdev->dev, "failed to get io resource: %d\n",
			ret);
		goto failed_get_resource;
	}

	if (!request_mem_region(res->start, resource_size(res), "mxs-saif")) {
		dev_err(&pdev->dev, "request_mem_region failed\n");
		ret = -EBUSY;
		goto failed_get_resource;
	}

	saif->base = ioremap(res->start, resource_size(res));
	if (!saif->base) {
		dev_err(&pdev->dev, "ioremap failed\n");
		ret = -ENODEV;
		goto failed_ioremap;
	}

	res = platform_get_resource(pdev, IORESOURCE_DMA, 0);
	if (!res) {
		ret = -ENODEV;
		dev_err(&pdev->dev, "failed to get dma resource: %d\n",
			ret);
		goto failed_ioremap;
	}
	saif->dma_param.chan_num = res->start;

	saif->irq = platform_get_irq(pdev, 0);
	if (saif->irq < 0) {
		ret = saif->irq;
		dev_err(&pdev->dev, "failed to get irq resource: %d\n",
			ret);
		goto failed_get_irq1;
	}

	saif->dev = &pdev->dev;
	ret = request_irq(saif->irq, mxs_saif_irq, 0, "mxs-saif", saif);
	if (ret) {
		dev_err(&pdev->dev, "failed to request irq\n");
		goto failed_get_irq1;
	}

	saif->dma_param.chan_irq = platform_get_irq(pdev, 1);
	if (saif->dma_param.chan_irq < 0) {
		ret = saif->dma_param.chan_irq;
		dev_err(&pdev->dev, "failed to get dma irq resource: %d\n",
			ret);
		goto failed_get_irq2;
	}

	platform_set_drvdata(pdev, saif);

	ret = snd_soc_register_dai(&pdev->dev, &mxs_saif_dai);
	if (ret) {
		dev_err(&pdev->dev, "register DAI failed\n");
		goto failed_register;
	}

	saif->soc_platform_pdev = platform_device_alloc(
					"mxs-pcm-audio", pdev->id);
	if (!saif->soc_platform_pdev) {
		ret = -ENOMEM;
		goto failed_pdev_alloc;
	}

	platform_set_drvdata(saif->soc_platform_pdev, saif);
	ret = platform_device_add(saif->soc_platform_pdev);
	if (ret) {
		dev_err(&pdev->dev, "failed to add soc platform device\n");
		goto failed_pdev_add;
	}

	return 0;

failed_pdev_add:
	platform_device_put(saif->soc_platform_pdev);
failed_pdev_alloc:
	snd_soc_unregister_dai(&pdev->dev);
failed_register:
failed_get_irq2:
	free_irq(saif->irq, saif);
failed_get_irq1:
	iounmap(saif->base);
failed_ioremap:
	release_mem_region(res->start, resource_size(res));
failed_get_resource:
	clk_put(saif->clk);
failed_clk:
	kfree(saif);

	return ret;
}

static int __devexit mxs_saif_remove(struct platform_device *pdev)
{
	struct resource *res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	struct mxs_saif *saif = platform_get_drvdata(pdev);

	platform_device_unregister(saif->soc_platform_pdev);

	snd_soc_unregister_dai(&pdev->dev);

	iounmap(saif->base);
	release_mem_region(res->start, resource_size(res));
	free_irq(saif->irq, saif);

	clk_put(saif->clk);
	kfree(saif);

	return 0;
}

static struct platform_driver mxs_saif_driver = {
	.probe = mxs_saif_probe,
	.remove = __devexit_p(mxs_saif_remove),

	.driver = {
		.name = "mxs-saif",
		.owner = THIS_MODULE,
	},
};

static int __init mxs_saif_init(void)
{
	return platform_driver_register(&mxs_saif_driver);
}

static void __exit mxs_saif_exit(void)
{
	platform_driver_unregister(&mxs_saif_driver);
}

module_init(mxs_saif_init);
module_exit(mxs_saif_exit);
MODULE_AUTHOR("Freescale Semiconductor, Inc.");
MODULE_DESCRIPTION("MXS ASoC SAIF driver");
MODULE_LICENSE("GPL");
