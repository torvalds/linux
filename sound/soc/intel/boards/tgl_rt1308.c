// SPDX-License-Identifier: GPL-2.0
//
// Copyright(c) 2019 Intel Corporation. All rights reserved.

/*
 * tgl_rt1308.c - ASoc Machine driver for Intel platforms
 * with RT1308 codec.
 */

#include <linux/acpi.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include <sound/core.h>
#include <sound/jack.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-acpi.h>

#include "../../codecs/rt1308.h"
#include "../../codecs/hdac_hdmi.h"
#include "hda_dsp_common.h"

struct tgl_card_private {
	struct list_head hdmi_pcm_list;
	bool common_hdmi_codec_drv;
};

#if IS_ENABLED(CONFIG_SND_SOC_HDAC_HDMI)
static struct snd_soc_jack tgl_hdmi[4];

struct tgl_hdmi_pcm {
	struct list_head head;
	struct snd_soc_dai *codec_dai;
	int device;
};

static int tgl_hdmi_init(struct snd_soc_pcm_runtime *rtd)
{
	struct tgl_card_private *ctx = snd_soc_card_get_drvdata(rtd->card);
	struct snd_soc_dai *dai = rtd->codec_dai;
	struct tgl_hdmi_pcm *pcm;

	pcm = devm_kzalloc(rtd->card->dev, sizeof(*pcm), GFP_KERNEL);
	if (!pcm)
		return -ENOMEM;

	/* dai_link id is 1:1 mapped to the PCM device */
	pcm->device = rtd->dai_link->id;
	pcm->codec_dai = dai;

	list_add_tail(&pcm->head, &ctx->hdmi_pcm_list);

	return 0;
}

#define NAME_SIZE	32
static int tgl_card_late_probe(struct snd_soc_card *card)
{
	struct tgl_card_private *ctx = snd_soc_card_get_drvdata(card);
	struct tgl_hdmi_pcm *pcm;
	struct snd_soc_component *component = NULL;
	int err, i = 0;
	char jack_name[NAME_SIZE];

	pcm = list_first_entry(&ctx->hdmi_pcm_list, struct tgl_hdmi_pcm,
			       head);
	component = pcm->codec_dai->component;

	if (ctx->common_hdmi_codec_drv)
		return hda_dsp_hdmi_build_controls(card, component);

	list_for_each_entry(pcm, &ctx->hdmi_pcm_list, head) {
		component = pcm->codec_dai->component;
		snprintf(jack_name, sizeof(jack_name),
			 "HDMI/DP, pcm=%d Jack", pcm->device);
		err = snd_soc_card_jack_new(card, jack_name,
					    SND_JACK_AVOUT, &tgl_hdmi[i],
					    NULL, 0);

		if (err)
			return err;

		err = hdac_hdmi_jack_init(pcm->codec_dai, pcm->device,
					  &tgl_hdmi[i]);
		if (err < 0)
			return err;

		i++;
	}

	if (!component)
		return -EINVAL;

	return hdac_hdmi_jack_port_init(component, &card->dapm);
}
#else
static int tgl_card_late_probe(struct snd_soc_card *card)
{
	return 0;
}
#endif

static int tgl_rt1308_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_card *card = rtd->card;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	int clk_id, clk_freq, pll_out;
	int err;

	clk_id = RT1308_PLL_S_MCLK;
	clk_freq = 38400000;

	pll_out = params_rate(params) * 512;

	/* Set rt1308 pll */
	err = snd_soc_dai_set_pll(codec_dai, 0, clk_id, clk_freq, pll_out);
	if (err < 0) {
		dev_err(card->dev, "Failed to set RT1308 PLL: %d\n", err);
		return err;
	}

	/* Set rt1308 sysclk */
	err = snd_soc_dai_set_sysclk(codec_dai, RT1308_FS_SYS_S_PLL, pll_out,
				     SND_SOC_CLOCK_IN);
	if (err < 0) {
		dev_err(card->dev, "Failed to set RT1308 SYSCLK: %d\n", err);
		return err;
	}

	return 0;
}

/* machine stream operations */
static struct snd_soc_ops tgl_rt1308_ops = {
	.hw_params = tgl_rt1308_hw_params,
};

static const struct snd_soc_dapm_widget tgl_rt1308_dapm_widgets[] = {
	SND_SOC_DAPM_SPK("Speakers", NULL),
	SND_SOC_DAPM_MIC("SoC DMIC", NULL),
};

static const struct snd_kcontrol_new tgl_rt1308_controls[] = {
	SOC_DAPM_PIN_SWITCH("Speakers"),
};

static const struct snd_soc_dapm_route tgl_rt1308_dapm_routes[] = {
	{ "Speakers", NULL, "SPOL" },
	{ "Speakers", NULL, "SPOR" },

	/* digital mics */
	{"DMic", NULL, "SoC DMIC"},
};

SND_SOC_DAILINK_DEF(ssp2_pin,
	DAILINK_COMP_ARRAY(COMP_CPU("SSP2 Pin")));

SND_SOC_DAILINK_DEF(ssp2_codec,
	DAILINK_COMP_ARRAY(COMP_CODEC("i2c-10EC1308:00", "rt1308-aif")));

SND_SOC_DAILINK_DEF(platform,
	DAILINK_COMP_ARRAY(COMP_PLATFORM("0000:00:1f.3")));

SND_SOC_DAILINK_DEF(dmic_pin,
	DAILINK_COMP_ARRAY(COMP_CPU("DMIC01 Pin")));
SND_SOC_DAILINK_DEF(dmic_codec,
	DAILINK_COMP_ARRAY(COMP_CODEC("dmic-codec", "dmic-hifi")));
SND_SOC_DAILINK_DEF(dmic16k,
	DAILINK_COMP_ARRAY(COMP_CPU("DMIC16k Pin")));

SND_SOC_DAILINK_DEF(idisp1_pin,
	DAILINK_COMP_ARRAY(COMP_CPU("iDisp1 Pin")));
SND_SOC_DAILINK_DEF(idisp1_codec,
	DAILINK_COMP_ARRAY(COMP_CODEC("ehdaudio0D2", "intel-hdmi-hifi1")));

SND_SOC_DAILINK_DEF(idisp2_pin,
	DAILINK_COMP_ARRAY(COMP_CPU("iDisp2 Pin")));
SND_SOC_DAILINK_DEF(idisp2_codec,
	DAILINK_COMP_ARRAY(COMP_CODEC("ehdaudio0D2", "intel-hdmi-hifi2")));

SND_SOC_DAILINK_DEF(idisp3_pin,
	DAILINK_COMP_ARRAY(COMP_CPU("iDisp3 Pin")));
SND_SOC_DAILINK_DEF(idisp3_codec,
	DAILINK_COMP_ARRAY(COMP_CODEC("ehdaudio0D2", "intel-hdmi-hifi3")));

SND_SOC_DAILINK_DEF(idisp4_pin,
	DAILINK_COMP_ARRAY(COMP_CPU("iDisp4 Pin")));
SND_SOC_DAILINK_DEF(idisp4_codec,
	DAILINK_COMP_ARRAY(COMP_CODEC("ehdaudio0D2", "intel-hdmi-hifi4")));

static struct snd_soc_dai_link tgl_rt1308_dailink[] = {
	{
		.name		= "SSP2-Codec",
		.id		= 0,
		.no_pcm		= 1,
		.ops		= &tgl_rt1308_ops,
		.dpcm_playback = 1,
		.nonatomic = true,
		SND_SOC_DAILINK_REG(ssp2_pin, ssp2_codec, platform),
	},
	{
		.name = "dmic01",
		.id = 1,
		.ignore_suspend = 1,
		.dpcm_capture = 1,
		.no_pcm = 1,
		SND_SOC_DAILINK_REG(dmic_pin, dmic_codec, platform),
	},
	{
		.name = "dmic16k",
		.id = 2,
		.ignore_suspend = 1,
		.dpcm_capture = 1,
		.no_pcm = 1,
		SND_SOC_DAILINK_REG(dmic16k, dmic_codec, platform),
	},
#if IS_ENABLED(CONFIG_SND_SOC_HDAC_HDMI)
	{
		.name = "iDisp1",
		.id = 3,
		.init = tgl_hdmi_init,
		.dpcm_playback = 1,
		.no_pcm = 1,
		SND_SOC_DAILINK_REG(idisp1_pin, idisp1_codec, platform),
	},
	{
		.name = "iDisp2",
		.id = 4,
		.init = tgl_hdmi_init,
		.dpcm_playback = 1,
		.no_pcm = 1,
		SND_SOC_DAILINK_REG(idisp2_pin, idisp2_codec, platform),
	},
	{
		.name = "iDisp3",
		.id = 5,
		.init = tgl_hdmi_init,
		.dpcm_playback = 1,
		.no_pcm = 1,
		SND_SOC_DAILINK_REG(idisp3_pin, idisp3_codec, platform),
	},
	{
		.name = "iDisp4",
		.id = 6,
		.init = tgl_hdmi_init,
		.dpcm_playback = 1,
		.no_pcm = 1,
		SND_SOC_DAILINK_REG(idisp4_pin, idisp4_codec, platform),
	},
#endif
};

/* audio machine driver */
static struct snd_soc_card tgl_rt1308_card = {
	.name         = "tgl_rt1308",
	.owner        = THIS_MODULE,
	.dai_link     = tgl_rt1308_dailink,
	.num_links = ARRAY_SIZE(tgl_rt1308_dailink),
	.controls = tgl_rt1308_controls,
	.num_controls = ARRAY_SIZE(tgl_rt1308_controls),
	.dapm_widgets = tgl_rt1308_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(tgl_rt1308_dapm_widgets),
	.dapm_routes = tgl_rt1308_dapm_routes,
	.num_dapm_routes = ARRAY_SIZE(tgl_rt1308_dapm_routes),
	.late_probe = tgl_card_late_probe,
};

static int tgl_rt1308_probe(struct platform_device *pdev)
{
	struct snd_soc_acpi_mach *mach;
	struct tgl_card_private *ctx;
	struct snd_soc_card *card = &tgl_rt1308_card;
	int ret;

	card->dev = &pdev->dev;

	ctx = devm_kzalloc(&pdev->dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	if (IS_ENABLED(CONFIG_SND_SOC_HDAC_HDMI))
		INIT_LIST_HEAD(&ctx->hdmi_pcm_list);

	mach = (&pdev->dev)->platform_data;

	ret = snd_soc_fixup_dai_links_platform_name(card,
						    mach->mach_params.platform);
	if (ret)
		return ret;

	ctx->common_hdmi_codec_drv = mach->mach_params.common_hdmi_codec_drv;

	snd_soc_card_set_drvdata(card, ctx);

	return devm_snd_soc_register_card(&pdev->dev, card);
}

static struct platform_driver tgl_rt1308_driver = {
	.driver = {
		.name   = "tgl_rt1308",
		.pm = &snd_soc_pm_ops,
	},
	.probe          = tgl_rt1308_probe,
};

module_platform_driver(tgl_rt1308_driver);

MODULE_AUTHOR("Xiuli Pan");
MODULE_DESCRIPTION("ASoC Intel(R) Tiger Lake + RT1308 Machine driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:tgl_rt1308");
