/*
 * ALSA SoC I2S Audio Layer for the Stretch S6000 family
 *
 * Author:      Daniel Gloeckner, <dg@emlix.com>
 * Copyright:   (C) 2009 emlix GmbH <info@emlix.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/delay.h>
#include <linux/clk.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/slab.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/initval.h>
#include <sound/soc.h>

#include "s6000-i2s.h"
#include "s6000-pcm.h"

struct s6000_i2s_dev {
	dma_addr_t sifbase;
	u8 __iomem *scbbase;
	unsigned int wide;
	unsigned int channel_in;
	unsigned int channel_out;
	unsigned int lines_in;
	unsigned int lines_out;
	struct s6000_pcm_dma_params dma_params;
};

#define S6_I2S_INTERRUPT_STATUS	0x00
#define   S6_I2S_INT_OVERRUN	1
#define   S6_I2S_INT_UNDERRUN	2
#define   S6_I2S_INT_ALIGNMENT	4
#define S6_I2S_INTERRUPT_ENABLE	0x04
#define S6_I2S_INTERRUPT_RAW	0x08
#define S6_I2S_INTERRUPT_CLEAR	0x0C
#define S6_I2S_INTERRUPT_SET	0x10
#define S6_I2S_MODE		0x20
#define   S6_I2S_DUAL		0
#define   S6_I2S_WIDE		1
#define S6_I2S_TX_DEFAULT	0x24
#define S6_I2S_DATA_CFG(c)	(0x40 + 0x10 * (c))
#define   S6_I2S_IN		0
#define   S6_I2S_OUT		1
#define   S6_I2S_UNUSED		2
#define S6_I2S_INTERFACE_CFG(c)	(0x44 + 0x10 * (c))
#define   S6_I2S_DIV_MASK	0x001fff
#define   S6_I2S_16BIT		0x000000
#define   S6_I2S_20BIT		0x002000
#define   S6_I2S_24BIT		0x004000
#define   S6_I2S_32BIT		0x006000
#define   S6_I2S_BITS_MASK	0x006000
#define   S6_I2S_MEM_16BIT	0x000000
#define   S6_I2S_MEM_32BIT	0x008000
#define   S6_I2S_MEM_MASK	0x008000
#define   S6_I2S_CHANNELS_SHIFT	16
#define   S6_I2S_CHANNELS_MASK	0x030000
#define   S6_I2S_SCK_IN		0x000000
#define   S6_I2S_SCK_OUT	0x040000
#define   S6_I2S_SCK_DIR	0x040000
#define   S6_I2S_WS_IN		0x000000
#define   S6_I2S_WS_OUT		0x080000
#define   S6_I2S_WS_DIR		0x080000
#define   S6_I2S_LEFT_FIRST	0x000000
#define   S6_I2S_RIGHT_FIRST	0x100000
#define   S6_I2S_FIRST		0x100000
#define   S6_I2S_CUR_SCK	0x200000
#define   S6_I2S_CUR_WS		0x400000
#define S6_I2S_ENABLE(c)	(0x48 + 0x10 * (c))
#define   S6_I2S_DISABLE_IF	0x02
#define   S6_I2S_ENABLE_IF	0x03
#define   S6_I2S_IS_BUSY	0x04
#define   S6_I2S_DMA_ACTIVE	0x08
#define   S6_I2S_IS_ENABLED	0x10

#define S6_I2S_NUM_LINES	4

#define S6_I2S_SIF_PORT0	0x0000000
#define S6_I2S_SIF_PORT1	0x0000080 /* docs say 0x0000010 */

static inline void s6_i2s_write_reg(struct s6000_i2s_dev *dev, int reg, u32 val)
{
	writel(val, dev->scbbase + reg);
}

static inline u32 s6_i2s_read_reg(struct s6000_i2s_dev *dev, int reg)
{
	return readl(dev->scbbase + reg);
}

static inline void s6_i2s_mod_reg(struct s6000_i2s_dev *dev, int reg,
				  u32 mask, u32 val)
{
	val ^= s6_i2s_read_reg(dev, reg) & ~mask;
	s6_i2s_write_reg(dev, reg, val);
}

static void s6000_i2s_start_channel(struct s6000_i2s_dev *dev, int channel)
{
	int i, j, cur, prev;

	/*
	 * Wait for WCLK to toggle 5 times before enabling the channel
	 * s6000 Family Datasheet 3.6.4:
	 *   "At least two cycles of WS must occur between commands
	 *    to disable or enable the interface"
	 */
	j = 0;
	prev = ~S6_I2S_CUR_WS;
	for (i = 1000000; --i && j < 6; ) {
		cur = s6_i2s_read_reg(dev, S6_I2S_INTERFACE_CFG(channel))
		       & S6_I2S_CUR_WS;
		if (prev != cur) {
			prev = cur;
			j++;
		}
	}
	if (j < 6)
		printk(KERN_WARNING "s6000-i2s: timeout waiting for WCLK\n");

	s6_i2s_write_reg(dev, S6_I2S_ENABLE(channel), S6_I2S_ENABLE_IF);
}

static void s6000_i2s_stop_channel(struct s6000_i2s_dev *dev, int channel)
{
	s6_i2s_write_reg(dev, S6_I2S_ENABLE(channel), S6_I2S_DISABLE_IF);
}

static void s6000_i2s_start(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct s6000_i2s_dev *dev = rtd->dai->cpu_dai->private_data;
	int channel;

	channel = (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) ?
			dev->channel_out : dev->channel_in;

	s6000_i2s_start_channel(dev, channel);
}

static void s6000_i2s_stop(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct s6000_i2s_dev *dev = rtd->dai->cpu_dai->private_data;
	int channel;

	channel = (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) ?
			dev->channel_out : dev->channel_in;

	s6000_i2s_stop_channel(dev, channel);
}

static int s6000_i2s_trigger(struct snd_pcm_substream *substream, int cmd,
			     int after)
{
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		if ((substream->stream == SNDRV_PCM_STREAM_CAPTURE) ^ !after)
			s6000_i2s_start(substream);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		if (!after)
			s6000_i2s_stop(substream);
	}
	return 0;
}

static unsigned int s6000_i2s_int_sources(struct s6000_i2s_dev *dev)
{
	unsigned int pending;
	pending = s6_i2s_read_reg(dev, S6_I2S_INTERRUPT_RAW);
	pending &= S6_I2S_INT_ALIGNMENT |
		   S6_I2S_INT_UNDERRUN |
		   S6_I2S_INT_OVERRUN;
	s6_i2s_write_reg(dev, S6_I2S_INTERRUPT_CLEAR, pending);

	return pending;
}

static unsigned int s6000_i2s_check_xrun(struct snd_soc_dai *cpu_dai)
{
	struct s6000_i2s_dev *dev = cpu_dai->private_data;
	unsigned int errors;
	unsigned int ret;

	errors = s6000_i2s_int_sources(dev);
	if (likely(!errors))
		return 0;

	ret = 0;
	if (errors & S6_I2S_INT_ALIGNMENT)
		printk(KERN_ERR "s6000-i2s: WCLK misaligned\n");
	if (errors & S6_I2S_INT_UNDERRUN)
		ret |= 1 << SNDRV_PCM_STREAM_PLAYBACK;
	if (errors & S6_I2S_INT_OVERRUN)
		ret |= 1 << SNDRV_PCM_STREAM_CAPTURE;
	return ret;
}

static void s6000_i2s_wait_disabled(struct s6000_i2s_dev *dev)
{
	int channel;
	int n = 50;
	for (channel = 0; channel < 2; channel++) {
		while (--n >= 0) {
			int v = s6_i2s_read_reg(dev, S6_I2S_ENABLE(channel));
			if ((v & S6_I2S_IS_ENABLED)
			    || !(v & (S6_I2S_DMA_ACTIVE | S6_I2S_IS_BUSY)))
				break;
			udelay(20);
		}
	}
	if (n < 0)
		printk(KERN_WARNING "s6000-i2s: timeout disabling interfaces");
}

static int s6000_i2s_set_dai_fmt(struct snd_soc_dai *cpu_dai,
				   unsigned int fmt)
{
	struct s6000_i2s_dev *dev = cpu_dai->private_data;
	u32 w;

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		w = S6_I2S_SCK_IN | S6_I2S_WS_IN;
		break;
	case SND_SOC_DAIFMT_CBS_CFM:
		w = S6_I2S_SCK_OUT | S6_I2S_WS_IN;
		break;
	case SND_SOC_DAIFMT_CBM_CFS:
		w = S6_I2S_SCK_IN | S6_I2S_WS_OUT;
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		w = S6_I2S_SCK_OUT | S6_I2S_WS_OUT;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		w |= S6_I2S_LEFT_FIRST;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		w |= S6_I2S_RIGHT_FIRST;
		break;
	default:
		return -EINVAL;
	}

	s6_i2s_mod_reg(dev, S6_I2S_INTERFACE_CFG(0),
		       S6_I2S_FIRST | S6_I2S_WS_DIR | S6_I2S_SCK_DIR, w);
	s6_i2s_mod_reg(dev, S6_I2S_INTERFACE_CFG(1),
		       S6_I2S_FIRST | S6_I2S_WS_DIR | S6_I2S_SCK_DIR, w);

	return 0;
}

static int s6000_i2s_set_clkdiv(struct snd_soc_dai *dai, int div_id, int div)
{
	struct s6000_i2s_dev *dev = dai->private_data;

	if (!div || (div & 1) || div > (S6_I2S_DIV_MASK + 1) * 2)
		return -EINVAL;

	s6_i2s_mod_reg(dev, S6_I2S_INTERFACE_CFG(div_id),
		       S6_I2S_DIV_MASK, div / 2 - 1);
	return 0;
}

static int s6000_i2s_hw_params(struct snd_pcm_substream *substream,
			       struct snd_pcm_hw_params *params,
			       struct snd_soc_dai *dai)
{
	struct s6000_i2s_dev *dev = dai->private_data;
	int interf;
	u32 w = 0;

	if (dev->wide)
		interf = 0;
	else {
		w |= (((params_channels(params) - 2) / 2)
		      << S6_I2S_CHANNELS_SHIFT) & S6_I2S_CHANNELS_MASK;
		interf = (substream->stream == SNDRV_PCM_STREAM_PLAYBACK)
				? dev->channel_out : dev->channel_in;
	}

	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S16_LE:
		w |= S6_I2S_16BIT | S6_I2S_MEM_16BIT;
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		w |= S6_I2S_32BIT | S6_I2S_MEM_32BIT;
		break;
	default:
		printk(KERN_WARNING "s6000-i2s: unsupported PCM format %x\n",
		       params_format(params));
		return -EINVAL;
	}

	if (s6_i2s_read_reg(dev, S6_I2S_INTERFACE_CFG(interf))
	     & S6_I2S_IS_ENABLED) {
		printk(KERN_ERR "s6000-i2s: interface already enabled\n");
		return -EBUSY;
	}

	s6_i2s_mod_reg(dev, S6_I2S_INTERFACE_CFG(interf),
		       S6_I2S_CHANNELS_MASK|S6_I2S_MEM_MASK|S6_I2S_BITS_MASK,
		       w);

	return 0;
}

static int s6000_i2s_dai_probe(struct platform_device *pdev,
			       struct snd_soc_dai *dai)
{
	struct s6000_i2s_dev *dev = dai->private_data;
	struct s6000_snd_platform_data *pdata = pdev->dev.platform_data;

	if (!pdata)
		return -EINVAL;

	dev->wide = pdata->wide;
	dev->channel_in = pdata->channel_in;
	dev->channel_out = pdata->channel_out;
	dev->lines_in = pdata->lines_in;
	dev->lines_out = pdata->lines_out;

	s6_i2s_write_reg(dev, S6_I2S_MODE,
			 dev->wide ? S6_I2S_WIDE : S6_I2S_DUAL);

	if (dev->wide) {
		int i;

		if (dev->lines_in + dev->lines_out > S6_I2S_NUM_LINES)
			return -EINVAL;

		dev->channel_in = 0;
		dev->channel_out = 1;
		dai->capture.channels_min = 2 * dev->lines_in;
		dai->capture.channels_max = dai->capture.channels_min;
		dai->playback.channels_min = 2 * dev->lines_out;
		dai->playback.channels_max = dai->playback.channels_min;

		for (i = 0; i < dev->lines_out; i++)
			s6_i2s_write_reg(dev, S6_I2S_DATA_CFG(i), S6_I2S_OUT);

		for (; i < S6_I2S_NUM_LINES - dev->lines_in; i++)
			s6_i2s_write_reg(dev, S6_I2S_DATA_CFG(i),
					 S6_I2S_UNUSED);

		for (; i < S6_I2S_NUM_LINES; i++)
			s6_i2s_write_reg(dev, S6_I2S_DATA_CFG(i), S6_I2S_IN);
	} else {
		unsigned int cfg[2] = {S6_I2S_UNUSED, S6_I2S_UNUSED};

		if (dev->lines_in > 1 || dev->lines_out > 1)
			return -EINVAL;

		dai->capture.channels_min = 2 * dev->lines_in;
		dai->capture.channels_max = 8 * dev->lines_in;
		dai->playback.channels_min = 2 * dev->lines_out;
		dai->playback.channels_max = 8 * dev->lines_out;

		if (dev->lines_in)
			cfg[dev->channel_in] = S6_I2S_IN;
		if (dev->lines_out)
			cfg[dev->channel_out] = S6_I2S_OUT;

		s6_i2s_write_reg(dev, S6_I2S_DATA_CFG(0), cfg[0]);
		s6_i2s_write_reg(dev, S6_I2S_DATA_CFG(1), cfg[1]);
	}

	if (dev->lines_out) {
		if (dev->lines_in) {
			if (!dev->dma_params.dma_out)
				return -ENODEV;
		} else {
			dev->dma_params.dma_out = dev->dma_params.dma_in;
			dev->dma_params.dma_in = 0;
		}
	}
	dev->dma_params.sif_in = dev->sifbase + (dev->channel_in ?
					S6_I2S_SIF_PORT1 : S6_I2S_SIF_PORT0);
	dev->dma_params.sif_out = dev->sifbase + (dev->channel_out ?
					S6_I2S_SIF_PORT1 : S6_I2S_SIF_PORT0);
	dev->dma_params.same_rate = pdata->same_rate | pdata->wide;
	return 0;
}

#define S6000_I2S_RATES	(SNDRV_PCM_RATE_CONTINUOUS | SNDRV_PCM_RATE_5512 | \
			 SNDRV_PCM_RATE_8000_192000)
#define S6000_I2S_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S32_LE)

static struct snd_soc_dai_ops s6000_i2s_dai_ops = {
	.set_fmt = s6000_i2s_set_dai_fmt,
	.set_clkdiv = s6000_i2s_set_clkdiv,
	.hw_params = s6000_i2s_hw_params,
};

struct snd_soc_dai s6000_i2s_dai = {
	.name = "s6000-i2s",
	.id = 0,
	.probe = s6000_i2s_dai_probe,
	.playback = {
		.channels_min = 2,
		.channels_max = 8,
		.formats = S6000_I2S_FORMATS,
		.rates = S6000_I2S_RATES,
		.rate_min = 0,
		.rate_max = 1562500,
	},
	.capture = {
		.channels_min = 2,
		.channels_max = 8,
		.formats = S6000_I2S_FORMATS,
		.rates = S6000_I2S_RATES,
		.rate_min = 0,
		.rate_max = 1562500,
	},
	.ops = &s6000_i2s_dai_ops,
}
EXPORT_SYMBOL_GPL(s6000_i2s_dai);

static int __devinit s6000_i2s_probe(struct platform_device *pdev)
{
	struct s6000_i2s_dev *dev;
	struct resource *scbmem, *sifmem, *region, *dma1, *dma2;
	u8 __iomem *mmio;
	int ret;

	scbmem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!scbmem) {
		dev_err(&pdev->dev, "no mem resource?\n");
		ret = -ENODEV;
		goto err_release_none;
	}

	region = request_mem_region(scbmem->start,
				    scbmem->end - scbmem->start + 1,
				    pdev->name);
	if (!region) {
		dev_err(&pdev->dev, "I2S SCB region already claimed\n");
		ret = -EBUSY;
		goto err_release_none;
	}

	mmio = ioremap(scbmem->start, scbmem->end - scbmem->start + 1);
	if (!mmio) {
		dev_err(&pdev->dev, "can't ioremap SCB region\n");
		ret = -ENOMEM;
		goto err_release_scb;
	}

	sifmem = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (!sifmem) {
		dev_err(&pdev->dev, "no second mem resource?\n");
		ret = -ENODEV;
		goto err_release_map;
	}

	region = request_mem_region(sifmem->start,
				    sifmem->end - sifmem->start + 1,
				    pdev->name);
	if (!region) {
		dev_err(&pdev->dev, "I2S SIF region already claimed\n");
		ret = -EBUSY;
		goto err_release_map;
	}

	dma1 = platform_get_resource(pdev, IORESOURCE_DMA, 0);
	if (!dma1) {
		dev_err(&pdev->dev, "no dma resource?\n");
		ret = -ENODEV;
		goto err_release_sif;
	}

	region = request_mem_region(dma1->start, dma1->end - dma1->start + 1,
				    pdev->name);
	if (!region) {
		dev_err(&pdev->dev, "I2S DMA region already claimed\n");
		ret = -EBUSY;
		goto err_release_sif;
	}

	dma2 = platform_get_resource(pdev, IORESOURCE_DMA, 1);
	if (dma2) {
		region = request_mem_region(dma2->start,
					    dma2->end - dma2->start + 1,
					    pdev->name);
		if (!region) {
			dev_err(&pdev->dev,
				"I2S DMA region already claimed\n");
			ret = -EBUSY;
			goto err_release_dma1;
		}
	}

	dev = kzalloc(sizeof(struct s6000_i2s_dev), GFP_KERNEL);
	if (!dev) {
		ret = -ENOMEM;
		goto err_release_dma2;
	}

	s6000_i2s_dai.dev = &pdev->dev;
	s6000_i2s_dai.private_data = dev;
	s6000_i2s_dai.capture.dma_data = &dev->dma_params;
	s6000_i2s_dai.playback.dma_data = &dev->dma_params;

	dev->sifbase = sifmem->start;
	dev->scbbase = mmio;

	s6_i2s_write_reg(dev, S6_I2S_INTERRUPT_ENABLE, 0);
	s6_i2s_write_reg(dev, S6_I2S_INTERRUPT_CLEAR,
			 S6_I2S_INT_ALIGNMENT |
			 S6_I2S_INT_UNDERRUN |
			 S6_I2S_INT_OVERRUN);

	s6000_i2s_stop_channel(dev, 0);
	s6000_i2s_stop_channel(dev, 1);
	s6000_i2s_wait_disabled(dev);

	dev->dma_params.check_xrun = s6000_i2s_check_xrun;
	dev->dma_params.trigger = s6000_i2s_trigger;
	dev->dma_params.dma_in = dma1->start;
	dev->dma_params.dma_out = dma2 ? dma2->start : 0;
	dev->dma_params.irq = platform_get_irq(pdev, 0);
	if (dev->dma_params.irq < 0) {
		dev_err(&pdev->dev, "no irq resource?\n");
		ret = -ENODEV;
		goto err_release_dev;
	}

	s6_i2s_write_reg(dev, S6_I2S_INTERRUPT_ENABLE,
			 S6_I2S_INT_ALIGNMENT |
			 S6_I2S_INT_UNDERRUN |
			 S6_I2S_INT_OVERRUN);

	ret = snd_soc_register_dai(&s6000_i2s_dai);
	if (ret)
		goto err_release_dev;

	return 0;

err_release_dev:
	kfree(dev);
err_release_dma2:
	if (dma2)
		release_mem_region(dma2->start, dma2->end - dma2->start + 1);
err_release_dma1:
	release_mem_region(dma1->start, dma1->end - dma1->start + 1);
err_release_sif:
	release_mem_region(sifmem->start, (sifmem->end - sifmem->start) + 1);
err_release_map:
	iounmap(mmio);
err_release_scb:
	release_mem_region(scbmem->start, (scbmem->end - scbmem->start) + 1);
err_release_none:
	return ret;
}

static void __devexit s6000_i2s_remove(struct platform_device *pdev)
{
	struct s6000_i2s_dev *dev = s6000_i2s_dai.private_data;
	struct resource *region;
	void __iomem *mmio = dev->scbbase;

	snd_soc_unregister_dai(&s6000_i2s_dai);

	s6000_i2s_stop_channel(dev, 0);
	s6000_i2s_stop_channel(dev, 1);

	s6_i2s_write_reg(dev, S6_I2S_INTERRUPT_ENABLE, 0);
	s6000_i2s_dai.private_data = 0;
	kfree(dev);

	region = platform_get_resource(pdev, IORESOURCE_DMA, 0);
	release_mem_region(region->start, region->end - region->start + 1);

	region = platform_get_resource(pdev, IORESOURCE_DMA, 1);
	if (region)
		release_mem_region(region->start,
				   region->end - region->start + 1);

	region = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	release_mem_region(region->start, (region->end - region->start) + 1);

	iounmap(mmio);
	region = platform_get_resource(pdev, IORESOURCE_IO, 0);
	release_mem_region(region->start, (region->end - region->start) + 1);
}

static struct platform_driver s6000_i2s_driver = {
	.probe  = s6000_i2s_probe,
	.remove = __devexit_p(s6000_i2s_remove),
	.driver = {
		.name   = "s6000-i2s",
		.owner  = THIS_MODULE,
	},
};

static int __init s6000_i2s_init(void)
{
	return platform_driver_register(&s6000_i2s_driver);
}
module_init(s6000_i2s_init);

static void __exit s6000_i2s_exit(void)
{
	platform_driver_unregister(&s6000_i2s_driver);
}
module_exit(s6000_i2s_exit);

MODULE_AUTHOR("Daniel Gloeckner");
MODULE_DESCRIPTION("Stretch s6000 family I2S SoC Interface");
MODULE_LICENSE("GPL");
