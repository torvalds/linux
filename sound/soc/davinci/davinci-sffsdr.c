/*
 * ASoC driver for Lyrtech SFFSDR board.
 *
 * Author:	Hugo Villeneuve
 * Copyright (C) 2008 Lyrtech inc
 *
 * Based on ASoC driver for TI DAVINCI EVM platform, original copyright follow:
 * Copyright:   (C) 2007 MontaVista Software, Inc., <source@mvista.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>

#include <asm/dma.h>
#include <asm/mach-types.h>
#include <asm/plat-sffsdr/sffsdr-fpga.h>

#include <mach/mcbsp.h>
#include <mach/edma.h>

#include "../codecs/pcm3008.h"
#include "davinci-pcm.h"
#include "davinci-i2s.h"

static int sffsdr_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params,
			    struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->dai->cpu_dai;
	int fs;
	int ret = 0;

	/* Set cpu DAI configuration:
	 * CLKX and CLKR are the inputs for the Sample Rate Generator.
	 * FSX and FSR are outputs, driven by the sample Rate Generator. */
	ret = snd_soc_dai_set_fmt(cpu_dai,
				  SND_SOC_DAIFMT_RIGHT_J |
				  SND_SOC_DAIFMT_CBM_CFS |
				  SND_SOC_DAIFMT_IB_NF);
	if (ret < 0)
		return ret;

	/* Fsref can be 32000, 44100 or 48000. */
	fs = params_rate(params);

	pr_debug("sffsdr_hw_params: rate = %d Hz\n", fs);

	return sffsdr_fpga_set_codec_fs(fs);
}

static struct snd_soc_ops sffsdr_ops = {
	.hw_params = sffsdr_hw_params,
};

/* davinci-sffsdr digital audio interface glue - connects codec <--> CPU */
static struct snd_soc_dai_link sffsdr_dai = {
	.name = "PCM3008", /* Codec name */
	.stream_name = "PCM3008 HiFi",
	.cpu_dai = &davinci_i2s_dai,
	.codec_dai = &pcm3008_dai,
	.ops = &sffsdr_ops,
};

/* davinci-sffsdr audio machine driver */
static struct snd_soc_card snd_soc_sffsdr = {
	.name = "DaVinci SFFSDR",
	.platform = &davinci_soc_platform,
	.dai_link = &sffsdr_dai,
	.num_links = 1,
};

/* sffsdr audio private data */
static struct pcm3008_setup_data sffsdr_pcm3008_setup = {
	.dem0_pin = GPIO(45),
	.dem1_pin = GPIO(46),
	.pdad_pin = GPIO(47),
	.pdda_pin = GPIO(38),
};

/* sffsdr audio subsystem */
static struct snd_soc_device sffsdr_snd_devdata = {
	.card = &snd_soc_sffsdr,
	.codec_dev = &soc_codec_dev_pcm3008,
	.codec_data = &sffsdr_pcm3008_setup,
};

static struct resource sffsdr_snd_resources[] = {
	{
		.start = DAVINCI_MCBSP_BASE,
		.end = DAVINCI_MCBSP_BASE + SZ_8K - 1,
		.flags = IORESOURCE_MEM,
	},
};

static struct evm_snd_platform_data sffsdr_snd_data = {
	.tx_dma_ch	= DAVINCI_DMA_MCBSP_TX,
	.rx_dma_ch	= DAVINCI_DMA_MCBSP_RX,
};

static struct platform_device *sffsdr_snd_device;

static int __init sffsdr_init(void)
{
	int ret;

	if (!machine_is_sffsdr())
		return -EINVAL;

	sffsdr_snd_device = platform_device_alloc("soc-audio", 0);
	if (!sffsdr_snd_device) {
		printk(KERN_ERR "platform device allocation failed\n");
		return -ENOMEM;
	}

	platform_set_drvdata(sffsdr_snd_device, &sffsdr_snd_devdata);
	sffsdr_snd_devdata.dev = &sffsdr_snd_device->dev;
	sffsdr_snd_device->dev.platform_data = &sffsdr_snd_data;

	ret = platform_device_add_resources(sffsdr_snd_device,
					    sffsdr_snd_resources,
					    ARRAY_SIZE(sffsdr_snd_resources));
	if (ret) {
		printk(KERN_ERR "platform device add ressources failed\n");
		goto error;
	}

	ret = platform_device_add(sffsdr_snd_device);
	if (ret)
		goto error;

	return ret;

error:
	platform_device_put(sffsdr_snd_device);
	return ret;
}

static void __exit sffsdr_exit(void)
{
	platform_device_unregister(sffsdr_snd_device);
}

module_init(sffsdr_init);
module_exit(sffsdr_exit);

MODULE_AUTHOR("Hugo Villeneuve");
MODULE_DESCRIPTION("Lyrtech SFFSDR ASoC driver");
MODULE_LICENSE("GPL");
