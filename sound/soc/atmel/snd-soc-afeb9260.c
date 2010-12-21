/*
 * afeb9260.c  --  SoC audio for AFEB9260
 *
 * Copyright (C) 2009 Sergey Lapin <slapin@ossfans.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/clk.h>
#include <linux/platform_device.h>

#include <linux/atmel-ssc.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>

#include <asm/mach-types.h>
#include <mach/hardware.h>
#include <linux/gpio.h>

#include "../codecs/tlv320aic23.h"
#include "atmel-pcm.h"
#include "atmel_ssc_dai.h"

#define CODEC_CLOCK 	12000000

static int afeb9260_hw_params(struct snd_pcm_substream *substream,
			 struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	int err;

	/* Set codec DAI configuration */
	err = snd_soc_dai_set_fmt(codec_dai,
				  SND_SOC_DAIFMT_I2S|
				  SND_SOC_DAIFMT_NB_IF |
				  SND_SOC_DAIFMT_CBM_CFM);
	if (err < 0) {
		printk(KERN_ERR "can't set codec DAI configuration\n");
		return err;
	}

	/* Set cpu DAI configuration */
	err = snd_soc_dai_set_fmt(cpu_dai,
				  SND_SOC_DAIFMT_I2S |
				  SND_SOC_DAIFMT_NB_IF |
				  SND_SOC_DAIFMT_CBM_CFM);
	if (err < 0) {
		printk(KERN_ERR "can't set cpu DAI configuration\n");
		return err;
	}

	/* Set the codec system clock for DAC and ADC */
	err =
	    snd_soc_dai_set_sysclk(codec_dai, 0, CODEC_CLOCK, SND_SOC_CLOCK_IN);

	if (err < 0) {
		printk(KERN_ERR "can't set codec system clock\n");
		return err;
	}

	return err;
}

static struct snd_soc_ops afeb9260_ops = {
	.hw_params = afeb9260_hw_params,
};

static const struct snd_soc_dapm_widget tlv320aic23_dapm_widgets[] = {
	SND_SOC_DAPM_HP("Headphone Jack", NULL),
	SND_SOC_DAPM_LINE("Line In", NULL),
	SND_SOC_DAPM_MIC("Mic Jack", NULL),
};

static const struct snd_soc_dapm_route audio_map[] = {
	{"Headphone Jack", NULL, "LHPOUT"},
	{"Headphone Jack", NULL, "RHPOUT"},

	{"LLINEIN", NULL, "Line In"},
	{"RLINEIN", NULL, "Line In"},

	{"MICIN", NULL, "Mic Jack"},
};

static int afeb9260_tlv320aic23_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_codec *codec = rtd->codec;

	/* Add afeb9260 specific widgets */
	snd_soc_dapm_new_controls(codec, tlv320aic23_dapm_widgets,
				  ARRAY_SIZE(tlv320aic23_dapm_widgets));

	/* Set up afeb9260 specific audio path audio_map */
	snd_soc_dapm_add_routes(codec, audio_map, ARRAY_SIZE(audio_map));

	snd_soc_dapm_enable_pin(codec, "Headphone Jack");
	snd_soc_dapm_enable_pin(codec, "Line In");
	snd_soc_dapm_enable_pin(codec, "Mic Jack");

	snd_soc_dapm_sync(codec);

	return 0;
}

/* Digital audio interface glue - connects codec <--> CPU */
static struct snd_soc_dai_link afeb9260_dai = {
	.name = "TLV320AIC23",
	.stream_name = "AIC23",
	.cpu_dai_name = "atmel-ssc-dai.0",
	.codec_dai_name = "tlv320aic23-hifi",
	.platform_name = "atmel_pcm-audio",
	.codec_name = "tlv320aic23-codec.0-0x1a",
	.init = afeb9260_tlv320aic23_init,
	.ops = &afeb9260_ops,
};

/* Audio machine driver */
static struct snd_soc_card snd_soc_machine_afeb9260 = {
	.name = "AFEB9260",
	.dai_link = &afeb9260_dai,
	.num_links = 1,
};

static struct platform_device *afeb9260_snd_device;

static int __init afeb9260_soc_init(void)
{
	int err;
	struct device *dev;

	if (!(machine_is_afeb9260()))
		return -ENODEV;


	afeb9260_snd_device = platform_device_alloc("soc-audio", -1);
	if (!afeb9260_snd_device) {
		printk(KERN_ERR "ASoC: Platform device allocation failed\n");
		return -ENOMEM;
	}

	platform_set_drvdata(afeb9260_snd_device, &snd_soc_machine_afeb9260);
	err = platform_device_add(afeb9260_snd_device);
	if (err)
		goto err1;

	dev = &afeb9260_snd_device->dev;

	return 0;
err1:
	platform_device_put(afeb9260_snd_device);
	return err;
}

static void __exit afeb9260_soc_exit(void)
{
	platform_device_unregister(afeb9260_snd_device);
}

module_init(afeb9260_soc_init);
module_exit(afeb9260_soc_exit);

MODULE_AUTHOR("Sergey Lapin <slapin@ossfans.org>");
MODULE_DESCRIPTION("ALSA SoC for AFEB9260");
MODULE_LICENSE("GPL");

