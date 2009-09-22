/*
 * ASoC driver for TI DAVINCI EVM platform
 *
 * Author:      Vladimir Barinov, <vbarinov@embeddedalley.com>
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
#include <linux/i2c.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>

#include <asm/dma.h>
#include <asm/mach-types.h>

#include <mach/asp.h>
#include <mach/edma.h>
#include <mach/mux.h>

#include "../codecs/tlv320aic3x.h"
#include "../codecs/spdif_transciever.h"
#include "davinci-pcm.h"
#include "davinci-i2s.h"
#include "davinci-mcasp.h"

#define AUDIO_FORMAT (SND_SOC_DAIFMT_DSP_B | \
		SND_SOC_DAIFMT_CBM_CFM | SND_SOC_DAIFMT_IB_NF)
static int evm_hw_params(struct snd_pcm_substream *substream,
			 struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->dai->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->dai->cpu_dai;
	int ret = 0;
	unsigned sysclk;

	/* ASP1 on DM355 EVM is clocked by an external oscillator */
	if (machine_is_davinci_dm355_evm() || machine_is_davinci_dm6467_evm())
		sysclk = 27000000;

	/* ASP0 in DM6446 EVM is clocked by U55, as configured by
	 * board-dm644x-evm.c using GPIOs from U18.  There are six
	 * options; here we "know" we use a 48 KHz sample rate.
	 */
	else if (machine_is_davinci_evm())
		sysclk = 12288000;

	else if (machine_is_davinci_da830_evm() ||
				machine_is_davinci_da850_evm())
		sysclk = 24576000;

	else
		return -EINVAL;

	/* set codec DAI configuration */
	ret = snd_soc_dai_set_fmt(codec_dai, AUDIO_FORMAT);
	if (ret < 0)
		return ret;

	/* set cpu DAI configuration */
	ret = snd_soc_dai_set_fmt(cpu_dai, AUDIO_FORMAT);
	if (ret < 0)
		return ret;

	/* set the codec system clock */
	ret = snd_soc_dai_set_sysclk(codec_dai, 0, sysclk, SND_SOC_CLOCK_OUT);
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

static struct snd_soc_dai_link dm6467_evm_dai[] = {
	{
		.name = "TLV320AIC3X",
		.stream_name = "AIC3X",
		.cpu_dai = &davinci_mcasp_dai[DAVINCI_MCASP_I2S_DAI],
		.codec_dai = &aic3x_dai,
		.init = evm_aic3x_init,
		.ops = &evm_ops,
	},
	{
		.name = "McASP",
		.stream_name = "spdif",
		.cpu_dai = &davinci_mcasp_dai[DAVINCI_MCASP_DIT_DAI],
		.codec_dai = &dit_stub_dai,
		.ops = &evm_ops,
	},
};
static struct snd_soc_dai_link da8xx_evm_dai = {
	.name = "TLV320AIC3X",
	.stream_name = "AIC3X",
	.cpu_dai = &davinci_mcasp_dai[DAVINCI_MCASP_I2S_DAI],
	.codec_dai = &aic3x_dai,
	.init = evm_aic3x_init,
	.ops = &evm_ops,
};

/* davinci-evm audio machine driver */
static struct snd_soc_card snd_soc_card_evm = {
	.name = "DaVinci EVM",
	.platform = &davinci_soc_platform,
	.dai_link = &evm_dai,
	.num_links = 1,
};

/* davinci dm6467 evm audio machine driver */
static struct snd_soc_card dm6467_snd_soc_card_evm = {
	.name = "DaVinci DM6467 EVM",
	.platform = &davinci_soc_platform,
	.dai_link = dm6467_evm_dai,
	.num_links = ARRAY_SIZE(dm6467_evm_dai),
};

static struct snd_soc_card da830_snd_soc_card = {
	.name = "DA830/OMAP-L137 EVM",
	.dai_link = &da8xx_evm_dai,
	.platform = &davinci_soc_platform,
	.num_links = 1,
};

static struct snd_soc_card da850_snd_soc_card = {
	.name = "DA850/OMAP-L138 EVM",
	.dai_link = &da8xx_evm_dai,
	.platform = &davinci_soc_platform,
	.num_links = 1,
};

static struct aic3x_setup_data aic3x_setup;

/* evm audio subsystem */
static struct snd_soc_device evm_snd_devdata = {
	.card = &snd_soc_card_evm,
	.codec_dev = &soc_codec_dev_aic3x,
	.codec_data = &aic3x_setup,
};

/* evm audio subsystem */
static struct snd_soc_device dm6467_evm_snd_devdata = {
	.card = &dm6467_snd_soc_card_evm,
	.codec_dev = &soc_codec_dev_aic3x,
	.codec_data = &aic3x_setup,
};

/* evm audio subsystem */
static struct snd_soc_device da830_evm_snd_devdata = {
	.card = &da830_snd_soc_card,
	.codec_dev = &soc_codec_dev_aic3x,
	.codec_data = &aic3x_setup,
};

static struct snd_soc_device da850_evm_snd_devdata = {
	.card		= &da850_snd_soc_card,
	.codec_dev	= &soc_codec_dev_aic3x,
	.codec_data	= &aic3x_setup,
};

static struct platform_device *evm_snd_device;

static int __init evm_init(void)
{
	struct snd_soc_device *evm_snd_dev_data;
	int index;
	int ret;

	if (machine_is_davinci_evm()) {
		evm_snd_dev_data = &evm_snd_devdata;
		index = 0;
	} else if (machine_is_davinci_dm355_evm()) {
		evm_snd_dev_data = &evm_snd_devdata;
		index = 1;
	} else if (machine_is_davinci_dm6467_evm()) {
		evm_snd_dev_data = &dm6467_evm_snd_devdata;
		index = 0;
	} else if (machine_is_davinci_da830_evm()) {
		evm_snd_dev_data = &da830_evm_snd_devdata;
		index = 1;
	} else if (machine_is_davinci_da850_evm()) {
		evm_snd_dev_data = &da850_evm_snd_devdata;
		index = 0;
	} else
		return -EINVAL;

	evm_snd_device = platform_device_alloc("soc-audio", index);
	if (!evm_snd_device)
		return -ENOMEM;

	platform_set_drvdata(evm_snd_device, evm_snd_dev_data);
	evm_snd_dev_data->dev = &evm_snd_device->dev;
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
