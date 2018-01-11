/*
 *  bytcht-da7213.c - ASoc Machine driver for Intel Baytrail and
 *             Cherrytrail-based platforms, with Dialog DA7213 codec
 *
 *  Copyright (C) 2017 Intel Corporation
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
#include "../../codecs/da7213.h"
#include "../atom/sst-atom-controls.h"

static const struct snd_kcontrol_new controls[] = {
	SOC_DAPM_PIN_SWITCH("Headphone Jack"),
	SOC_DAPM_PIN_SWITCH("Headset Mic"),
	SOC_DAPM_PIN_SWITCH("Mic"),
	SOC_DAPM_PIN_SWITCH("Aux In"),
};

static const struct snd_soc_dapm_widget dapm_widgets[] = {
	SND_SOC_DAPM_HP("Headphone Jack", NULL),
	SND_SOC_DAPM_MIC("Headset Mic", NULL),
	SND_SOC_DAPM_MIC("Mic", NULL),
	SND_SOC_DAPM_LINE("Aux In", NULL),
};

static const struct snd_soc_dapm_route audio_map[] = {
	{"Headphone Jack", NULL, "HPL"},
	{"Headphone Jack", NULL, "HPR"},

	{"AUXL", NULL, "Aux In"},
	{"AUXR", NULL, "Aux In"},

	/* Assume Mic1 is linked to Headset and Mic2 to on-board mic */
	{"MIC1", NULL, "Headset Mic"},
	{"MIC2", NULL, "Mic"},

	/* SOC-codec link */
	{"ssp2 Tx", NULL, "codec_out0"},
	{"ssp2 Tx", NULL, "codec_out1"},
	{"codec_in0", NULL, "ssp2 Rx"},
	{"codec_in1", NULL, "ssp2 Rx"},

	{"Playback", NULL, "ssp2 Tx"},
	{"ssp2 Rx", NULL, "Capture"},
};

static int codec_fixup(struct snd_soc_pcm_runtime *rtd,
		       struct snd_pcm_hw_params *params)
{
	int ret;
	struct snd_interval *rate = hw_param_interval(params,
			SNDRV_PCM_HW_PARAM_RATE);
	struct snd_interval *channels = hw_param_interval(params,
						SNDRV_PCM_HW_PARAM_CHANNELS);

	/* The DSP will convert the FE rate to 48k, stereo, 24bits */
	rate->min = rate->max = 48000;
	channels->min = channels->max = 2;

	/* set SSP2 to 24-bit */
	params_set_format(params, SNDRV_PCM_FORMAT_S24_LE);

	/*
	 * Default mode for SSP configuration is TDM 4 slot, override config
	 * with explicit setting to I2S 2ch 24-bit. The word length is set with
	 * dai_set_tdm_slot() since there is no other API exposed
	 */
	ret = snd_soc_dai_set_fmt(rtd->cpu_dai,
				  SND_SOC_DAIFMT_I2S     |
				  SND_SOC_DAIFMT_NB_NF   |
				  SND_SOC_DAIFMT_CBS_CFS);
	if (ret < 0) {
		dev_err(rtd->dev, "can't set format to I2S, err %d\n", ret);
		return ret;
	}

	ret = snd_soc_dai_set_tdm_slot(rtd->cpu_dai, 0x3, 0x3, 2, 24);
	if (ret < 0) {
		dev_err(rtd->dev, "can't set I2S config, err %d\n", ret);
		return ret;
	}

	return 0;
}

static int aif1_startup(struct snd_pcm_substream *substream)
{
	return snd_pcm_hw_constraint_single(substream->runtime,
			SNDRV_PCM_HW_PARAM_RATE, 48000);
}

static int aif1_hw_params(struct snd_pcm_substream *substream,
			  struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	int ret;

	ret = snd_soc_dai_set_sysclk(codec_dai, DA7213_CLKSRC_MCLK,
				     19200000, SND_SOC_CLOCK_IN);
	if (ret < 0)
		dev_err(codec_dai->dev, "can't set codec sysclk configuration\n");

	ret = snd_soc_dai_set_pll(codec_dai, 0,
			DA7213_SYSCLK_PLL_SRM, 0, DA7213_PLL_FREQ_OUT_98304000);
	if (ret < 0) {
		dev_err(codec_dai->dev, "failed to start PLL: %d\n", ret);
		return -EIO;
	}

	return ret;
}

static int aif1_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	int ret;

	ret = snd_soc_dai_set_pll(codec_dai, 0,
				  DA7213_SYSCLK_MCLK, 0, 0);
	if (ret < 0) {
		dev_err(codec_dai->dev, "failed to stop PLL: %d\n", ret);
		return -EIO;
	}

	return ret;
}

static const struct snd_soc_ops aif1_ops = {
	.startup = aif1_startup,
};

static const struct snd_soc_ops ssp2_ops = {
	.hw_params = aif1_hw_params,
	.hw_free = aif1_hw_free,

};

static struct snd_soc_dai_link dailink[] = {
	[MERR_DPCM_AUDIO] = {
		.name = "Audio Port",
		.stream_name = "Audio",
		.cpu_dai_name = "media-cpu-dai",
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.platform_name = "sst-mfld-platform",
		.nonatomic = true,
		.dynamic = 1,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
		.ops = &aif1_ops,
	},
	[MERR_DPCM_DEEP_BUFFER] = {
		.name = "Deep-Buffer Audio Port",
		.stream_name = "Deep-Buffer Audio",
		.cpu_dai_name = "deepbuffer-cpu-dai",
		.codec_dai_name = "snd-soc-dummy-dai",
		.codec_name = "snd-soc-dummy",
		.platform_name = "sst-mfld-platform",
		.nonatomic = true,
		.dynamic = 1,
		.dpcm_playback = 1,
		.ops = &aif1_ops,
	},
	/* CODEC<->CODEC link */
	/* back ends */
	{
		.name = "SSP2-Codec",
		.id = 0,
		.cpu_dai_name = "ssp2-port",
		.platform_name = "sst-mfld-platform",
		.no_pcm = 1,
		.codec_dai_name = "da7213-hifi",
		.codec_name = "i2c-DLGS7213:00",
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF
						| SND_SOC_DAIFMT_CBS_CFS,
		.be_hw_params_fixup = codec_fixup,
		.nonatomic = true,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
		.ops = &ssp2_ops,
	},
};

/* SoC card */
static struct snd_soc_card bytcht_da7213_card = {
	.name = "bytcht-da7213",
	.owner = THIS_MODULE,
	.dai_link = dailink,
	.num_links = ARRAY_SIZE(dailink),
	.controls = controls,
	.num_controls = ARRAY_SIZE(controls),
	.dapm_widgets = dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(dapm_widgets),
	.dapm_routes = audio_map,
	.num_dapm_routes = ARRAY_SIZE(audio_map),
};

static char codec_name[SND_ACPI_I2C_ID_LEN];

static int bytcht_da7213_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card;
	struct snd_soc_acpi_mach *mach;
	const char *i2c_name = NULL;
	int dai_index = 0;
	int ret_val = 0;
	int i;

	mach = (&pdev->dev)->platform_data;
	card = &bytcht_da7213_card;
	card->dev = &pdev->dev;

	/* fix index of codec dai */
	for (i = 0; i < ARRAY_SIZE(dailink); i++) {
		if (!strcmp(dailink[i].codec_name, "i2c-DLGS7213:00")) {
			dai_index = i;
			break;
		}
	}

	/* fixup codec name based on HID */
	i2c_name = snd_soc_acpi_find_name_from_hid(mach->id);
	if (i2c_name) {
		snprintf(codec_name, sizeof(codec_name),
			"%s%s", "i2c-", i2c_name);
		dailink[dai_index].codec_name = codec_name;
	}

	ret_val = devm_snd_soc_register_card(&pdev->dev, card);
	if (ret_val) {
		dev_err(&pdev->dev,
			"snd_soc_register_card failed %d\n", ret_val);
		return ret_val;
	}
	platform_set_drvdata(pdev, card);
	return ret_val;
}

static struct platform_driver bytcht_da7213_driver = {
	.driver = {
		.name = "bytcht_da7213",
	},
	.probe = bytcht_da7213_probe,
};
module_platform_driver(bytcht_da7213_driver);

MODULE_DESCRIPTION("ASoC Intel(R) Baytrail/Cherrytrail+DA7213 Machine driver");
MODULE_AUTHOR("Pierre-Louis Bossart");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:bytcht_da7213");
