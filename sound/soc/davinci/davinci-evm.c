/*
 * ASoC driver for TI DAVINCI EVM platform
 *
 * Author:      Vladimir Barinov, <vbarinov@ru.mvista.com>
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
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>

#include <asm/dma.h>
#include <mach/hardware.h>

#include "../codecs/tlv320aic3x.h"
#include "davinci-pcm.h"
#include "davinci-i2s.h"

#define EVM_CODEC_CLOCK 22579200

static int evm_hw_params(struct snd_pcm_substream *substream,
			 struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->dai->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->dai->cpu_dai;
	int ret = 0;

	/* set codec DAI configuration */
	ret = snd_soc_dai_set_fmt(codec_dai, SND_SOC_DAIFMT_I2S |
					 SND_SOC_DAIFMT_CBM_CFM);
	if (ret < 0)
		return ret;

	/* set cpu DAI configuration */
	ret = snd_soc_dai_set_fmt(cpu_dai, SND_SOC_DAIFMT_CBM_CFM |
				       SND_SOC_DAIFMT_IB_NF);
	if (ret < 0)
		return ret;

	/* set the codec system clock */
	ret = snd_soc_dai_set_sysclk(codec_dai, 0, EVM_CODEC_CLOCK,
					    SND_SOC_CLOCK_OUT);
	if (ret < 0)
		return ret;

	return 0;
}

static struct snd_soc_ops evm_ops = {
	.hw_params = evm_hw_params,
};

/* davinci-evm machine dapm widgets */
static const struct snd_soc_dapm_widget aic3x_dapm_widgets[] = {
	SND_SOC_DAPM_HP("Headphone Jack", NULL),
	SND_SOC_DAPM_LINE("Line Out", NULL),
	SND_SOC_DAPM_MIC("Mic Jack", NULL),
	SND_SOC_DAPM_LINE("Line In", NULL),
};

/* davinci-evm machine audio_mapnections to the codec pins */
static const struct snd_soc_dapm_route audio_map[] = {
	/* Headphone connected to HPLOUT, HPROUT */
	{"Headphone Jack", NULL, "HPLOUT"},
	{"Headphone Jack", NULL, "HPROUT"},

	/* Line Out connected to LLOUT, RLOUT */
	{"Line Out", NULL, "LLOUT"},
	{"Line Out", NULL, "RLOUT"},

	/* Mic connected to (MIC3L | MIC3R) */
	{"MIC3L", NULL, "Mic Bias 2V"},
	{"MIC3R", NULL, "Mic Bias 2V"},
	{"Mic Bias 2V", NULL, "Mic Jack"},

	/* Line In connected to (LINE1L | LINE2L), (LINE1R | LINE2R) */
	{"LINE1L", NULL, "Line In"},
	{"LINE2L", NULL, "Line In"},
	{"LINE1R", NULL, "Line In"},
	{"LINE2R", NULL, "Line In"},
};

/* Logic for a aic3x as connected on a davinci-evm */
static int evm_aic3x_init(struct snd_soc_codec *codec)
{
	/* Add davinci-evm specific widgets */
	snd_soc_dapm_new_controls(codec, aic3x_dapm_widgets,
				  ARRAY_SIZE(aic3x_dapm_widgets));

	/* Set up davinci-evm specific audio path audio_map */
	snd_soc_dapm_add_routes(codec, audio_map, ARRAY_SIZE(audio_map));

	/* not connected */
	snd_soc_dapm_disable_pin(codec, "MONO_LOUT");
	snd_soc_dapm_disable_pin(codec, "HPLCOM");
	snd_soc_dapm_disable_pin(codec, "HPRCOM");

	/* always connected */
	snd_soc_dapm_enable_pin(codec, "Headphone Jack");
	snd_soc_dapm_enable_pin(codec, "Line Out");
	snd_soc_dapm_enable_pin(codec, "Mic Jack");
	snd_soc_dapm_enable_pin(codec, "Line In");

	snd_soc_dapm_sync(codec);

	return 0;
}

/* davinci-evm digital audio interface glue - connects codec <--> CPU */
static struct snd_soc_dai_link evm_dai = {
	.name = "TLV320AIC3X",
	.stream_name = "AIC3X",
	.cpu_dai = &davinci_i2s_dai,
	.codec_dai = &aic3x_dai,
	.init = evm_aic3x_init,
	.ops = &evm_ops,
};

/* davinci-evm audio machine driver */
static struct snd_soc_machine snd_soc_machine_evm = {
	.name = "DaVinci EVM",
	.dai_link = &evm_dai,
	.num_links = 1,
};

/* evm audio private data */
static struct aic3x_setup_data evm_aic3x_setup = {
	.i2c_address = 0x1b,
};

/* evm audio subsystem */
static struct snd_soc_device evm_snd_devdata = {
	.machine = &snd_soc_machine_evm,
	.platform = &davinci_soc_platform,
	.codec_dev = &soc_codec_dev_aic3x,
	.codec_data = &evm_aic3x_setup,
};

static struct resource evm_snd_resources[] = {
	{
		.start = DAVINCI_MCBSP_BASE,
		.end = DAVINCI_MCBSP_BASE + SZ_8K - 1,
		.flags = IORESOURCE_MEM,
	},
};

static struct evm_snd_platform_data evm_snd_data = {
	.tx_dma_ch	= DM644X_DMACH_MCBSP_TX,
	.rx_dma_ch	= DM644X_DMACH_MCBSP_RX,
};

static struct platform_device *evm_snd_device;

static int __init evm_init(void)
{
	int ret;

	evm_snd_device = platform_device_alloc("soc-audio", 0);
	if (!evm_snd_device)
		return -ENOMEM;

	platform_set_drvdata(evm_snd_device, &evm_snd_devdata);
	evm_snd_devdata.dev = &evm_snd_device->dev;
	evm_snd_device->dev.platform_data = &evm_snd_data;

	ret = platform_device_add_resources(evm_snd_device, evm_snd_resources,
					    ARRAY_SIZE(evm_snd_resources));
	if (ret) {
		platform_device_put(evm_snd_device);
		return ret;
	}

	ret = platform_device_add(evm_snd_device);
	if (ret)
		platform_device_put(evm_snd_device);

	return ret;
}

static void __exit evm_exit(void)
{
	platform_device_unregister(evm_snd_device);
}

module_init(evm_init);
module_exit(evm_exit);

MODULE_AUTHOR("Vladimir Barinov");
MODULE_DESCRIPTION("TI DAVINCI EVM ASoC driver");
MODULE_LICENSE("GPL");
