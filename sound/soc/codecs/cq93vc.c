// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * ALSA SoC CQ0093 Voice Codec Driver for DaVinci platforms
 *
 * Copyright (C) 2010 Texas Instruments, Inc
 *
 * Author: Miguel Aguilar <miguel.aguilar@ridgerun.com>
 */
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/platform_device.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <linux/mfd/davinci_voicecodec.h>
#include <linux/spi/spi.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/initval.h>

static const struct snd_kcontrol_new cq93vc_snd_controls[] = {
	SOC_SINGLE("PGA Capture Volume", DAVINCI_VC_REG05, 0, 0x03, 0),
	SOC_SINGLE("Mono DAC Playback Volume", DAVINCI_VC_REG09, 0, 0x3f, 0),
};

static int cq93vc_mute(struct snd_soc_dai *dai, int mute)
{
	struct snd_soc_component *component = dai->component;
	u8 reg;

	if (mute)
		reg = DAVINCI_VC_REG09_MUTE;
	else
		reg = 0;

	snd_soc_component_update_bits(component, DAVINCI_VC_REG09, DAVINCI_VC_REG09_MUTE,
			    reg);

	return 0;
}

static int cq93vc_set_dai_sysclk(struct snd_soc_dai *codec_dai,
				 int clk_id, unsigned int freq, int dir)
{
	switch (freq) {
	case 22579200:
	case 27000000:
	case 33868800:
		return 0;
	}

	return -EINVAL;
}

static int cq93vc_set_bias_level(struct snd_soc_component *component,
				enum snd_soc_bias_level level)
{
	switch (level) {
	case SND_SOC_BIAS_ON:
		snd_soc_component_write(component, DAVINCI_VC_REG12,
			     DAVINCI_VC_REG12_POWER_ALL_ON);
		break;
	case SND_SOC_BIAS_PREPARE:
		break;
	case SND_SOC_BIAS_STANDBY:
		snd_soc_component_write(component, DAVINCI_VC_REG12,
			     DAVINCI_VC_REG12_POWER_ALL_OFF);
		break;
	case SND_SOC_BIAS_OFF:
		/* force all power off */
		snd_soc_component_write(component, DAVINCI_VC_REG12,
			     DAVINCI_VC_REG12_POWER_ALL_OFF);
		break;
	}

	return 0;
}

#define CQ93VC_RATES	(SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000)
#define CQ93VC_FORMATS	(SNDRV_PCM_FMTBIT_U8 | SNDRV_PCM_FMTBIT_S16_LE)

static const struct snd_soc_dai_ops cq93vc_dai_ops = {
	.digital_mute	= cq93vc_mute,
	.set_sysclk	= cq93vc_set_dai_sysclk,
};

static struct snd_soc_dai_driver cq93vc_dai = {
	.name = "cq93vc-hifi",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 1,
		.channels_max = 2,
		.rates = CQ93VC_RATES,
		.formats = CQ93VC_FORMATS,},
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = 2,
		.rates = CQ93VC_RATES,
		.formats = CQ93VC_FORMATS,},
	.ops = &cq93vc_dai_ops,
};

static int cq93vc_probe(struct snd_soc_component *component)
{
	struct davinci_vc *davinci_vc = component->dev->platform_data;

	snd_soc_component_init_regmap(component, davinci_vc->regmap);

	return 0;
}

static const struct snd_soc_component_driver soc_component_dev_cq93vc = {
	.set_bias_level		= cq93vc_set_bias_level,
	.probe			= cq93vc_probe,
	.controls		= cq93vc_snd_controls,
	.num_controls		= ARRAY_SIZE(cq93vc_snd_controls),
	.idle_bias_on		= 1,
	.use_pmdown_time	= 1,
	.endianness		= 1,
	.non_legacy_dai_naming	= 1,
};

static int cq93vc_platform_probe(struct platform_device *pdev)
{
	return devm_snd_soc_register_component(&pdev->dev,
			&soc_component_dev_cq93vc, &cq93vc_dai, 1);
}

static int cq93vc_platform_remove(struct platform_device *pdev)
{
	return 0;
}

static struct platform_driver cq93vc_codec_driver = {
	.driver = {
			.name = "cq93vc-codec",
	},

	.probe = cq93vc_platform_probe,
	.remove = cq93vc_platform_remove,
};

module_platform_driver(cq93vc_codec_driver);

MODULE_DESCRIPTION("Texas Instruments DaVinci ASoC CQ0093 Voice Codec Driver");
MODULE_AUTHOR("Miguel Aguilar");
MODULE_LICENSE("GPL");
