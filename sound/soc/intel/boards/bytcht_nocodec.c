// SPDX-License-Identifier: GPL-2.0-only
/*
 *  bytcht_nocodec.c - ASoc Machine driver for MinnowBoard Max and Up
 *  to make I2S signals observable on the Low-Speed connector. Audio codec
 *  is not managed by ASoC/DAPM
 *
 *  Copyright (C) 2015-2017 Intel Corp
 *
 *  ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */

#include <linux/module.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include "../atom/sst-atom-controls.h"

static const struct snd_soc_dapm_widget widgets[] = {
	SND_SOC_DAPM_MIC("Mic", NULL),
	SND_SOC_DAPM_SPK("Speaker", NULL),
};

static const struct snd_kcontrol_new controls[] = {
	SOC_DAPM_PIN_SWITCH("Mic"),
	SOC_DAPM_PIN_SWITCH("Speaker"),
};

static const struct snd_soc_dapm_route audio_map[] = {
	{"ssp2 Tx", NULL, "codec_out0"},
	{"ssp2 Tx", NULL, "codec_out1"},
	{"codec_in0", NULL, "ssp2 Rx"},
	{"codec_in1", NULL, "ssp2 Rx"},

	{"ssp2 Rx", NULL, "Mic"},
	{"Speaker", NULL, "ssp2 Tx"},
};

static int codec_fixup(struct snd_soc_pcm_runtime *rtd,
			    struct snd_pcm_hw_params *params)
{
	struct snd_interval *rate = hw_param_interval(params,
			SNDRV_PCM_HW_PARAM_RATE);
	struct snd_interval *channels = hw_param_interval(params,
						SNDRV_PCM_HW_PARAM_CHANNELS);
	int ret;

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
	ret = snd_soc_dai_set_fmt(asoc_rtd_to_cpu(rtd, 0),
				  SND_SOC_DAIFMT_I2S     |
				  SND_SOC_DAIFMT_NB_NF   |
				  SND_SOC_DAIFMT_CBS_CFS);

	if (ret < 0) {
		dev_err(rtd->dev, "can't set format to I2S, err %d\n", ret);
		return ret;
	}

	ret = snd_soc_dai_set_tdm_slot(asoc_rtd_to_cpu(rtd, 0), 0x3, 0x3, 2, 24);
	if (ret < 0) {
		dev_err(rtd->dev, "can't set I2S config, err %d\n", ret);
		return ret;
	}

	return 0;
}

static const unsigned int rates_48000[] = {
	48000,
};

static const struct snd_pcm_hw_constraint_list constraints_48000 = {
	.count = ARRAY_SIZE(rates_48000),
	.list  = rates_48000,
};

static int aif1_startup(struct snd_pcm_substream *substream)
{
	return snd_pcm_hw_constraint_list(substream->runtime, 0,
			SNDRV_PCM_HW_PARAM_RATE,
			&constraints_48000);
}

static struct snd_soc_ops aif1_ops = {
	.startup = aif1_startup,
};

SND_SOC_DAILINK_DEF(dummy,
	DAILINK_COMP_ARRAY(COMP_DUMMY()));

SND_SOC_DAILINK_DEF(media,
	DAILINK_COMP_ARRAY(COMP_CPU("media-cpu-dai")));

SND_SOC_DAILINK_DEF(deepbuffer,
	DAILINK_COMP_ARRAY(COMP_CPU("deepbuffer-cpu-dai")));

SND_SOC_DAILINK_DEF(ssp2_port,
	DAILINK_COMP_ARRAY(COMP_CPU("ssp2-port")));

SND_SOC_DAILINK_DEF(platform,
	DAILINK_COMP_ARRAY(COMP_PLATFORM("sst-mfld-platform")));

static struct snd_soc_dai_link dais[] = {
	[MERR_DPCM_AUDIO] = {
		.name = "Audio Port",
		.stream_name = "Audio",
		.ignore_suspend = 1,
		.nonatomic = true,
		.dynamic = 1,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
		.ops = &aif1_ops,
		SND_SOC_DAILINK_REG(media, dummy, platform),
	},
	[MERR_DPCM_DEEP_BUFFER] = {
		.name = "Deep-Buffer Audio Port",
		.stream_name = "Deep-Buffer Audio",
		.ignore_suspend = 1,
		.nonatomic = true,
		.dynamic = 1,
		.dpcm_playback = 1,
		.ops = &aif1_ops,
		SND_SOC_DAILINK_REG(deepbuffer, dummy, platform),
	},
	/* CODEC<->CODEC link */
	/* back ends */
	{
		.name = "SSP2-LowSpeed Connector",
		.id = 0,
		.no_pcm = 1,
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF
						| SND_SOC_DAIFMT_CBS_CFS,
		.be_hw_params_fixup = codec_fixup,
		.ignore_suspend = 1,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
		SND_SOC_DAILINK_REG(ssp2_port, dummy, platform),
	},
};

/* SoC card */
static struct snd_soc_card bytcht_nocodec_card = {
	.name = "bytcht-nocodec",
	.owner = THIS_MODULE,
	.dai_link = dais,
	.num_links = ARRAY_SIZE(dais),
	.dapm_widgets = widgets,
	.num_dapm_widgets = ARRAY_SIZE(widgets),
	.dapm_routes = audio_map,
	.num_dapm_routes = ARRAY_SIZE(audio_map),
	.controls = controls,
	.num_controls = ARRAY_SIZE(controls),
	.fully_routed = true,
};

static int snd_bytcht_nocodec_mc_probe(struct platform_device *pdev)
{
	int ret_val = 0;

	/* register the soc card */
	bytcht_nocodec_card.dev = &pdev->dev;

	ret_val = devm_snd_soc_register_card(&pdev->dev, &bytcht_nocodec_card);

	if (ret_val) {
		dev_err(&pdev->dev, "devm_snd_soc_register_card failed %d\n",
			ret_val);
		return ret_val;
	}
	platform_set_drvdata(pdev, &bytcht_nocodec_card);
	return ret_val;
}

static struct platform_driver snd_bytcht_nocodec_mc_driver = {
	.driver = {
		.name = "bytcht_nocodec",
	},
	.probe = snd_bytcht_nocodec_mc_probe,
};
module_platform_driver(snd_bytcht_nocodec_mc_driver);

MODULE_DESCRIPTION("ASoC Intel(R) Baytrail/Cherrytrail Nocodec Machine driver");
MODULE_AUTHOR("Pierre-Louis Bossart <pierre-louis.bossart at linux.intel.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:bytcht_nocodec");
