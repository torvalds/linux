/*
 * ALSA SoC CQ0093 Voice Codec Driver for DaVinci platforms
 *
 * Copyright (C) 2010 Texas Instruments, Inc
 *
 * Author: Miguel Aguilar <miguel.aguilar@ridgerun.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
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
	struct snd_soc_codec *codec = dai->codec;
	u8 reg;

	if (mute)
		reg = DAVINCI_VC_REG09_MUTE;
	else
		reg = 0;

	snd_soc_update_bits(codec, DAVINCI_VC_REG09, DAVINCI_VC_REG09_MUTE,
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

static int cq93vc_set_bias_level(struct snd_soc_codec *codec,
				enum snd_soc_bias_level level)
{
	switch (level) {
	case SND_SOC_BIAS_ON:
		snd_soc_write(codec, DAVINCI_VC_REG12,
			     DAVINCI_VC_REG12_POWER_ALL_ON);
		break;
	case SND_SOC_BIAS_PREPARE:
		break;
	case SND_SOC_BIAS_STANDBY:
		snd_soc_write(codec, DAVINCI_VC_REG12,
			     DAVINCI_VC_REG12_POWER_ALL_OFF);
		break;
	case SND_SOC_BIAS_OFF:
		/* force all power off */
		snd_soc_write(codec, DAVINCI_VC_REG12,
			     DAVINCI_VC_REG12_POWER_ALL_OFF);
		break;
	}
	codec->dapm.bias_level = level;

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

static struct regmap *cq93vc_get_regmap(struct device *dev)
{
	struct davinci_vc *davinci_vc = dev->platform_data;

	return davinci_vc->regmap;
}

static struct snd_soc_codec_driver soc_codec_dev_cq93vc = {
	.set_bias_level = cq93vc_set_bias_level,
	.get_regmap = cq93vc_get_regmap,
	.controls = cq93vc_snd_controls,
	.num_controls = ARRAY_SIZE(cq93vc_snd_controls),
};

static int cq93vc_platform_probe(struct platform_device *pdev)
{
	return snd_soc_register_codec(&pdev->dev,
			&soc_codec_dev_cq93vc, &cq93vc_dai, 1);
}

static int cq93vc_platform_remove(struct platform_device *pdev)
{
	snd_soc_unregister_codec(&pdev->dev);
	return 0;
}

static struct platform_driver cq93vc_codec_driver = {
	.driver = {
			.name = "cq93vc-codec",
			.owner = THIS_MODULE,
	},

	.probe = cq93vc_platform_probe,
	.remove = cq93vc_platform_remove,
};

module_platform_driver(cq93vc_codec_driver);

MODULE_DESCRIPTION("Texas Instruments DaVinci ASoC CQ0093 Voice Codec Driver");
MODULE_AUTHOR("Miguel Aguilar");
MODULE_LICENSE("GPL");
