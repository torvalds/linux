/*
 *  bxt-pcm512x.c - ASoc Machine driver for Intel Baytrail and
 *             Cherrytrail-based platforms, with TI PCM512x codec
 *
 *  Copyright (C) 2016 Intel Corporation
 *  Author: Pierre-Louis Bossart <pierre-louis.bossart@linux.intel.com>
 *
 *  ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */
#include <linux/module.h>
#include <linux/acpi.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <asm/platform_sst_audio.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-acpi.h>
#include <sound/jack.h>
#include "../../codecs/hdac_hdmi.h"
#include "../../codecs/pcm512x.h"
#include "../atom/sst-atom-controls.h"

#ifdef CONFIG_SND_SOC_HDAC_HDMI
static struct snd_soc_jack broxton_hdmi[3];

struct bxt_hdmi_pcm {
	struct list_head head;
	struct snd_soc_dai *codec_dai;
	int device;
};

static int broxton_hdmi_init(struct snd_soc_pcm_runtime *rtd)
{
	struct bxt_card_private *ctx = snd_soc_card_get_drvdata(rtd->card);
	struct snd_soc_dai *dai = rtd->codec_dai;
	struct bxt_hdmi_pcm *pcm;

	pcm = devm_kzalloc(rtd->card->dev, sizeof(*pcm), GFP_KERNEL);
	if (!pcm)
		return -ENOMEM;

	pcm->device = dai->id;
	pcm->codec_dai = dai;

	list_add_tail(&pcm->head, &ctx->hdmi_pcm_list);

	return 0;
}

#define NAME_SIZE	32
static int bxt_card_late_probe(struct snd_soc_card *card)
{
	struct bxt_card_private *ctx = snd_soc_card_get_drvdata(card);
	struct bxt_hdmi_pcm *pcm;
	struct snd_soc_component *component = NULL;
	int err, i = 0;
	char jack_name[NAME_SIZE];

	list_for_each_entry(pcm, &ctx->hdmi_pcm_list, head) {
		component = pcm->codec_dai->component;
		snprintf(jack_name, sizeof(jack_name),
			 "HDMI/DP, pcm=%d Jack", pcm->device);
		err = snd_soc_card_jack_new(card, jack_name,
					    SND_JACK_AVOUT, &broxton_hdmi[i],
					    NULL, 0);

		if (err)
			return err;

		err = hdac_hdmi_jack_init(pcm->codec_dai, pcm->device,
					  &broxton_hdmi[i]);
		if (err < 0)
			return err;

		i++;
	}

	if (!component)
		return -EINVAL;

	return hdac_hdmi_jack_port_init(component, &card->dapm);
}
#else
static int bxt_card_late_probe(struct snd_soc_card *card)
{
	return 0;
}
#endif

struct bxt_card_private {
	struct list_head hdmi_pcm_list;
};

static const struct snd_soc_dapm_widget dapm_widgets[] = {
	SND_SOC_DAPM_SPK("Ext Spk", NULL),
};

static const struct snd_soc_dapm_route audio_map[] = {
	/* Speaker */
	{"Ext Spk", NULL, "OUTR"},
	{"Ext Spk", NULL, "OUTL"},
};

static int codec_fixup(struct snd_soc_pcm_runtime *rtd,
		       struct snd_pcm_hw_params *params)
{
	struct snd_interval *rate = hw_param_interval(params,
					SNDRV_PCM_HW_PARAM_RATE);
	struct snd_interval *channels = hw_param_interval(params,
					SNDRV_PCM_HW_PARAM_CHANNELS);
	struct snd_mask *fmt = hw_param_mask(params, SNDRV_PCM_HW_PARAM_FORMAT);

	/* The ADSP will covert the FE rate to 48k, stereo */
	rate->min = 48000;
	rate->max = 48000;
	channels->min = 2;
	channels->max = 2;

	/* set SSP5 to 24 bit */
	snd_mask_none(fmt);
	snd_mask_set(fmt, SNDRV_PCM_FORMAT_S24_LE);

	return 0;
}

static int aif1_startup(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_component *codec = rtd->codec_dai->component;

	snd_soc_component_update_bits(codec, PCM512x_GPIO_CONTROL_1,
				      0x08, 0x08);

	return snd_pcm_hw_constraint_single(substream->runtime,
			SNDRV_PCM_HW_PARAM_RATE, 48000);
}

static void aif1_shutdown(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_component *codec = rtd->codec_dai->component;

	snd_soc_component_update_bits(codec, PCM512x_GPIO_CONTROL_1,
				      0x08, 0x00);
}

static int init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_component *codec = rtd->codec_dai->component;

	snd_soc_component_update_bits(codec, PCM512x_GPIO_EN, 0x08, 0x08);
	snd_soc_component_update_bits(codec, PCM512x_GPIO_OUTPUT_4, 0x0f, 0x02);
	snd_soc_component_update_bits(codec, PCM512x_GPIO_CONTROL_1,
				      0x08, 0x08);

	return 0;
}

static const struct snd_soc_ops aif1_ops = {
	.startup = aif1_startup,
	.shutdown = aif1_shutdown,
};

static struct snd_soc_dai_link dailink[] = {
	/* CODEC<->CODEC link */
	/* back ends */
	{
		.name = "SSP5-Codec",
		.id = 0,
		.cpu_dai_name = "SSP5 Pin",
		.platform_name = "sof-audio",
		.no_pcm = 1,
		.codec_dai_name = "pcm512x-hifi",
		.codec_name = "i2c-104C5122:00",
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF
						| SND_SOC_DAIFMT_CBS_CFS,
		.init = init,
		.be_hw_params_fixup = codec_fixup,
		.nonatomic = true,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
	},
#ifdef CONFIG_SND_SOC_HDAC_HDMI
	{
		.name = "iDisp1",
		.id = 1,
		.cpu_dai_name = "iDisp1 Pin",
		.codec_name = "ehdaudio0D2",
		.codec_dai_name = "intel-hdmi-hifi1",
		.platform_name = "0000:00:0e.0",
		.init = broxton_hdmi_init,
		.dpcm_playback = 1,
		.no_pcm = 1,
	},
	{
		.name = "iDisp2",
		.id = 2,
		.cpu_dai_name = "iDisp2 Pin",
		.codec_name = "ehdaudio0D2",
		.codec_dai_name = "intel-hdmi-hifi2",
		.platform_name = "0000:00:0e.0",
		.init = broxton_hdmi_init,
		.dpcm_playback = 1,
		.no_pcm = 1,
	},
	{
		.name = "iDisp3",
		.id = 3,
		.cpu_dai_name = "iDisp3 Pin",
		.codec_name = "ehdaudio0D2",
		.codec_dai_name = "intel-hdmi-hifi3",
		.platform_name = "0000:00:0e.0",
		.init = broxton_hdmi_init,
		.dpcm_playback = 1,
		.no_pcm = 1,
	},
#endif
};


/* SoC card */
static struct snd_soc_card bxt_pcm512x_card = {
	.name = "bxt-pcm512x",
	.owner = THIS_MODULE,
	.dai_link = dailink,
	.num_links = ARRAY_SIZE(dailink),
	.dapm_widgets = dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(dapm_widgets),
	.dapm_routes = audio_map,
	.num_dapm_routes = ARRAY_SIZE(audio_map),
	.late_probe = bxt_card_late_probe,
};

 /* i2c-<HID>:00 with HID being 8 chars */
static char codec_name[SND_ACPI_I2C_ID_LEN];

static int bxt_pcm512x_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card;
	struct snd_soc_acpi_mach *mach;
	struct bxt_card_private *ctx;
	const char *i2c_name = NULL;
	int dai_index = 0;
	int ret_val = 0, i;

	ctx = devm_kzalloc(&pdev->dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	if (IS_ENABLED(CONFIG_SND_SOC_HDAC_HDMI))
		INIT_LIST_HEAD(&ctx->hdmi_pcm_list);

	mach = (&pdev->dev)->platform_data;
	card = &bxt_pcm512x_card;
	card->dev = &pdev->dev;

	/* fix index of codec dai */
	for (i = 0; i < ARRAY_SIZE(dailink); i++) {
		if (!strcmp(dailink[i].codec_name, "i2c-104C5122:00")) {
			dai_index = i;
			break;
		}
	}

	/* fixup codec name based on HID */
	i2c_name = acpi_dev_get_first_match_name(mach->id, NULL, -1);
	if (i2c_name) {
		snprintf(codec_name, sizeof(codec_name),
			 "%s%s", "i2c-", i2c_name);
		dailink[dai_index].codec_name = codec_name;
	}

	snd_soc_card_set_drvdata(card, ctx);

	ret_val = devm_snd_soc_register_card(&pdev->dev, card);
	if (ret_val) {
		dev_err(&pdev->dev,
			"snd_soc_register_card failed %d\n", ret_val);
		return ret_val;
	}
	platform_set_drvdata(pdev, card);
	return ret_val;
}

static struct platform_driver bxt_pcm521x_driver = {
	.driver = {
		.name = "bxt-pcm512x",
	},
	.probe = bxt_pcm512x_probe,
};
module_platform_driver(bxt_pcm521x_driver);

MODULE_DESCRIPTION("ASoC Intel(R) Broxton + PCM512x Machine driver");
MODULE_AUTHOR("Pierre-Louis Bossart");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:bxt-pcm512x");
