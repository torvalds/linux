// SPDX-License-Identifier: GPL-2.0
// Copyright(c) 2019 Intel Corporation.

/*
 * Intel Cometlake I2S Machine driver for RT1011 + RT5682 codec
 */

#include <linux/input.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/dmi.h>
#include <linux/slab.h>
#include <linux/acpi.h>
#include <sound/core.h>
#include <sound/jack.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/rt5682.h>
#include <sound/soc-acpi.h>
#include "../../codecs/rt1011.h"
#include "../../codecs/rt5682.h"
#include "../../codecs/hdac_hdmi.h"
#include "hda_dsp_common.h"

/* The platform clock outputs 24Mhz clock to codec as I2S MCLK */
#define CML_PLAT_CLK	24000000
#define CML_RT1011_CODEC_DAI "rt1011-aif"
#define CML_RT5682_CODEC_DAI "rt5682-aif1"
#define NAME_SIZE 32

static struct snd_soc_jack hdmi_jack[3];

struct hdmi_pcm {
	struct list_head head;
	struct snd_soc_dai *codec_dai;
	int device;
};

struct card_private {
	char codec_name[SND_ACPI_I2C_ID_LEN];
	struct snd_soc_jack headset;
	struct list_head hdmi_pcm_list;
	bool common_hdmi_codec_drv;
};

static const struct snd_kcontrol_new cml_controls[] = {
	SOC_DAPM_PIN_SWITCH("Headphone Jack"),
	SOC_DAPM_PIN_SWITCH("Headset Mic"),
	SOC_DAPM_PIN_SWITCH("TL Ext Spk"),
	SOC_DAPM_PIN_SWITCH("TR Ext Spk"),
	SOC_DAPM_PIN_SWITCH("WL Ext Spk"),
	SOC_DAPM_PIN_SWITCH("WR Ext Spk"),
};

static const struct snd_soc_dapm_widget cml_rt1011_rt5682_widgets[] = {
	SND_SOC_DAPM_SPK("TL Ext Spk", NULL),
	SND_SOC_DAPM_SPK("TR Ext Spk", NULL),
	SND_SOC_DAPM_SPK("WL Ext Spk", NULL),
	SND_SOC_DAPM_SPK("WR Ext Spk", NULL),
	SND_SOC_DAPM_HP("Headphone Jack", NULL),
	SND_SOC_DAPM_MIC("Headset Mic", NULL),
	SND_SOC_DAPM_MIC("SoC DMIC", NULL),
};

static const struct snd_soc_dapm_route cml_rt1011_rt5682_map[] = {
	/*speaker*/
	{"TL Ext Spk", NULL, "TL SPO"},
	{"TR Ext Spk", NULL, "TR SPO"},
	{"WL Ext Spk", NULL, "WL SPO"},
	{"WR Ext Spk", NULL, "WR SPO"},

	/* HP jack connectors - unknown if we have jack detection */
	{ "Headphone Jack", NULL, "HPOL" },
	{ "Headphone Jack", NULL, "HPOR" },

	/* other jacks */
	{ "IN1P", NULL, "Headset Mic" },

	/* DMIC */
	{"DMic", NULL, "SoC DMIC"},
};

static int cml_rt5682_codec_init(struct snd_soc_pcm_runtime *rtd)
{
	struct card_private *ctx = snd_soc_card_get_drvdata(rtd->card);
	struct snd_soc_component *component = rtd->codec_dai->component;
	struct snd_soc_jack *jack;
	int ret;

	/* need to enable ASRC function for 24MHz mclk rate */
	rt5682_sel_asrc_clk_src(component, RT5682_DA_STEREO1_FILTER |
					RT5682_AD_STEREO1_FILTER,
					RT5682_CLK_SEL_I2S1_ASRC);

	/*
	 * Headset buttons map to the google Reference headset.
	 * These can be configured by userspace.
	 */
	ret = snd_soc_card_jack_new(rtd->card, "Headset Jack",
				    SND_JACK_HEADSET | SND_JACK_BTN_0 |
				    SND_JACK_BTN_1 | SND_JACK_BTN_2 |
				    SND_JACK_BTN_3,
				    &ctx->headset, NULL, 0);
	if (ret) {
		dev_err(rtd->dev, "Headset Jack creation failed: %d\n", ret);
		return ret;
	}

	jack = &ctx->headset;

	snd_jack_set_key(jack->jack, SND_JACK_BTN_0, KEY_PLAYPAUSE);
	snd_jack_set_key(jack->jack, SND_JACK_BTN_1, KEY_VOICECOMMAND);
	snd_jack_set_key(jack->jack, SND_JACK_BTN_2, KEY_VOLUMEUP);
	snd_jack_set_key(jack->jack, SND_JACK_BTN_3, KEY_VOLUMEDOWN);
	ret = snd_soc_component_set_jack(component, jack, NULL);
	if (ret)
		dev_err(rtd->dev, "Headset Jack call-back failed: %d\n", ret);

	return ret;
};

static int cml_rt5682_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	int clk_id, clk_freq, pll_out, ret;

	clk_id = RT5682_PLL1_S_MCLK;
	clk_freq = CML_PLAT_CLK;

	pll_out = params_rate(params) * 512;

	ret = snd_soc_dai_set_pll(codec_dai, 0, clk_id, clk_freq, pll_out);
	if (ret < 0)
		dev_warn(rtd->dev, "snd_soc_dai_set_pll err = %d\n", ret);

	/* Configure sysclk for codec */
	ret = snd_soc_dai_set_sysclk(codec_dai, RT5682_SCLK_S_PLL1,
				     pll_out, SND_SOC_CLOCK_IN);
	if (ret < 0)
		dev_warn(rtd->dev, "snd_soc_dai_set_sysclk err = %d\n", ret);

	/*
	 * slot_width should be equal or large than data length, set them
	 * be the same
	 */
	ret = snd_soc_dai_set_tdm_slot(codec_dai, 0x0, 0x0, 2,
				       params_width(params));
	if (ret < 0)
		dev_warn(rtd->dev, "set TDM slot err:%d\n", ret);
	return ret;
}

static int cml_rt1011_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai;
	struct snd_soc_card *card = rtd->card;
	int srate, i, ret = 0;

	srate = params_rate(params);

	for (i = 0; i < rtd->num_codecs; i++) {
		codec_dai = rtd->codec_dais[i];

		/* 100 Fs to drive 24 bit data */
		ret = snd_soc_dai_set_pll(codec_dai, 0, RT1011_PLL1_S_BCLK,
					  100 * srate, 256 * srate);
		if (ret < 0) {
			dev_err(card->dev, "codec_dai clock not set\n");
			return ret;
		}

		ret = snd_soc_dai_set_sysclk(codec_dai,
					     RT1011_FS_SYS_PRE_S_PLL1,
					     256 * srate, SND_SOC_CLOCK_IN);
		if (ret < 0) {
			dev_err(card->dev, "codec_dai clock not set\n");
			return ret;
		}

		/*
		 * Codec TDM is configured as 24 bit capture/ playback.
		 * 2 CH PB is done over 4 codecs - 2 Woofers and 2 Tweeters.
		 * The Left woofer and tweeter plays the Left playback data
		 * and  similar by the Right.
		 * Hence 2 codecs (1 T and 1 W pair) share same Rx slot.
		 * The feedback is captured for each codec individually.
		 * Hence all 4 codecs use 1 Tx slot each for feedback.
		 */
		if (!strcmp(codec_dai->component->name, "i2c-10EC1011:00")) {
			ret = snd_soc_dai_set_tdm_slot(codec_dai,
						       0x4, 0x1, 4, 24);
			if (ret < 0)
				break;
		}
		if (!strcmp(codec_dai->component->name, "i2c-10EC1011:02")) {
			ret = snd_soc_dai_set_tdm_slot(codec_dai,
						       0x1, 0x1, 4, 24);
			if (ret < 0)
				break;
		}
		/* TDM Rx slot 2 is used for Right Woofer & Tweeters pair */
		if (!strcmp(codec_dai->component->name, "i2c-10EC1011:01")) {
			ret = snd_soc_dai_set_tdm_slot(codec_dai,
						       0x8, 0x2, 4, 24);
			if (ret < 0)
				break;
		}
		if (!strcmp(codec_dai->component->name, "i2c-10EC1011:03")) {
			ret = snd_soc_dai_set_tdm_slot(codec_dai,
						       0x2, 0x2, 4, 24);
			if (ret < 0)
				break;
		}
	}
	if (ret < 0)
		dev_err(rtd->dev,
			"set codec TDM slot for %s failed with error %d\n",
			codec_dai->component->name, ret);
	return ret;
}

static struct snd_soc_ops cml_rt5682_ops = {
	.hw_params = cml_rt5682_hw_params,
};

static const struct snd_soc_ops cml_rt1011_ops = {
	.hw_params = cml_rt1011_hw_params,
};

static int sof_card_late_probe(struct snd_soc_card *card)
{
	struct card_private *ctx = snd_soc_card_get_drvdata(card);
	struct snd_soc_component *component = NULL;
	char jack_name[NAME_SIZE];
	struct hdmi_pcm *pcm;
	int ret, i = 0;

	pcm = list_first_entry(&ctx->hdmi_pcm_list, struct hdmi_pcm,
			       head);
	component = pcm->codec_dai->component;

	if (ctx->common_hdmi_codec_drv)
		return hda_dsp_hdmi_build_controls(card, component);

	list_for_each_entry(pcm, &ctx->hdmi_pcm_list, head) {
		component = pcm->codec_dai->component;
		snprintf(jack_name, sizeof(jack_name),
			 "HDMI/DP, pcm=%d Jack", pcm->device);
		ret = snd_soc_card_jack_new(card, jack_name,
					    SND_JACK_AVOUT, &hdmi_jack[i],
					    NULL, 0);
		if (ret)
			return ret;

		ret = hdac_hdmi_jack_init(pcm->codec_dai, pcm->device,
					  &hdmi_jack[i]);
		if (ret < 0)
			return ret;

		i++;
	}
	if (!component)
		return -EINVAL;

	return hdac_hdmi_jack_port_init(component, &card->dapm);
}

static int hdmi_init(struct snd_soc_pcm_runtime *rtd)
{
	struct card_private *ctx = snd_soc_card_get_drvdata(rtd->card);
	struct snd_soc_dai *dai = rtd->codec_dai;
	struct hdmi_pcm *pcm;

	pcm = devm_kzalloc(rtd->card->dev, sizeof(*pcm), GFP_KERNEL);
	if (!pcm)
		return -ENOMEM;

	pcm->device = dai->id;
	pcm->codec_dai = dai;

	list_add_tail(&pcm->head, &ctx->hdmi_pcm_list);

	return 0;
}

/* Cometlake digital audio interface glue - connects codec <--> CPU */

SND_SOC_DAILINK_DEF(ssp0_pin,
	DAILINK_COMP_ARRAY(COMP_CPU("SSP0 Pin")));
SND_SOC_DAILINK_DEF(ssp0_codec,
	DAILINK_COMP_ARRAY(COMP_CODEC("i2c-10EC5682:00",
				CML_RT5682_CODEC_DAI)));

SND_SOC_DAILINK_DEF(ssp1_pin,
	DAILINK_COMP_ARRAY(COMP_CPU("SSP1 Pin")));
SND_SOC_DAILINK_DEF(ssp1_codec,
	DAILINK_COMP_ARRAY(
	/* WL */ COMP_CODEC("i2c-10EC1011:00", CML_RT1011_CODEC_DAI),
	/* WR */ COMP_CODEC("i2c-10EC1011:01", CML_RT1011_CODEC_DAI),
	/* TL */ COMP_CODEC("i2c-10EC1011:02", CML_RT1011_CODEC_DAI),
	/* TR */ COMP_CODEC("i2c-10EC1011:03", CML_RT1011_CODEC_DAI)));

SND_SOC_DAILINK_DEF(dmic_pin,
	DAILINK_COMP_ARRAY(COMP_CPU("DMIC01 Pin")));

SND_SOC_DAILINK_DEF(dmic16k_pin,
	DAILINK_COMP_ARRAY(COMP_CPU("DMIC16k Pin")));

SND_SOC_DAILINK_DEF(dmic_codec,
	DAILINK_COMP_ARRAY(COMP_CODEC("dmic-codec", "dmic-hifi")));

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

SND_SOC_DAILINK_DEF(platform,
	DAILINK_COMP_ARRAY(COMP_PLATFORM("0000:00:1f.3")));

static struct snd_soc_dai_link cml_rt1011_rt5682_dailink[] = {
	/* Back End DAI links */
	{
		/* SSP0 - Codec */
		.name = "SSP0-Codec",
		.id = 0,
		.init = cml_rt5682_codec_init,
		.ignore_pmdown_time = 1,
		.ops = &cml_rt5682_ops,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
		.no_pcm = 1,
		SND_SOC_DAILINK_REG(ssp0_pin, ssp0_codec, platform),
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
		SND_SOC_DAILINK_REG(dmic16k_pin, dmic_codec, platform),
	},
	{
		.name = "iDisp1",
		.id = 3,
		.init = hdmi_init,
		.dpcm_playback = 1,
		.no_pcm = 1,
		SND_SOC_DAILINK_REG(idisp1_pin, idisp1_codec, platform),
	},
	{
		.name = "iDisp2",
		.id = 4,
		.init = hdmi_init,
		.dpcm_playback = 1,
		.no_pcm = 1,
		SND_SOC_DAILINK_REG(idisp2_pin, idisp2_codec, platform),
	},
	{
		.name = "iDisp3",
		.id = 5,
		.init = hdmi_init,
		.dpcm_playback = 1,
		.no_pcm = 1,
		SND_SOC_DAILINK_REG(idisp3_pin, idisp3_codec, platform),
	},
	{
		/*
		 * SSP1 - Codec : added to end of list ensuring
		 * reuse of common topologies for other end points
		 * and changing only SSP1's codec
		 */
		.name = "SSP1-Codec",
		.id = 6,
		.dpcm_playback = 1,
		.dpcm_capture = 1, /* Capture stream provides Feedback */
		.no_pcm = 1,
		.ops = &cml_rt1011_ops,
		SND_SOC_DAILINK_REG(ssp1_pin, ssp1_codec, platform),
	},
};

static struct snd_soc_codec_conf rt1011_conf[] = {
	{
		.dlc = COMP_CODEC_CONF("i2c-10EC1011:00"),
		.name_prefix = "WL",
	},
	{
		.dlc = COMP_CODEC_CONF("i2c-10EC1011:01"),
		.name_prefix = "WR",
	},
	{
		.dlc = COMP_CODEC_CONF("i2c-10EC1011:02"),
		.name_prefix = "TL",
	},
	{
		.dlc = COMP_CODEC_CONF("i2c-10EC1011:03"),
		.name_prefix = "TR",
	},
};

/* Cometlake audio machine driver for RT1011 and RT5682 */
static struct snd_soc_card snd_soc_card_cml = {
	.name = "cml_rt1011_rt5682",
	.dai_link = cml_rt1011_rt5682_dailink,
	.num_links = ARRAY_SIZE(cml_rt1011_rt5682_dailink),
	.codec_conf = rt1011_conf,
	.num_configs = ARRAY_SIZE(rt1011_conf),
	.dapm_widgets = cml_rt1011_rt5682_widgets,
	.num_dapm_widgets = ARRAY_SIZE(cml_rt1011_rt5682_widgets),
	.dapm_routes = cml_rt1011_rt5682_map,
	.num_dapm_routes = ARRAY_SIZE(cml_rt1011_rt5682_map),
	.controls = cml_controls,
	.num_controls = ARRAY_SIZE(cml_controls),
	.fully_routed = true,
	.late_probe = sof_card_late_probe,
};

static int snd_cml_rt1011_probe(struct platform_device *pdev)
{
	struct card_private *ctx;
	struct snd_soc_acpi_mach *mach;
	const char *platform_name;
	int ret;

	ctx = devm_kzalloc(&pdev->dev, sizeof(*ctx), GFP_ATOMIC);
	if (!ctx)
		return -ENOMEM;

	INIT_LIST_HEAD(&ctx->hdmi_pcm_list);
	mach = (&pdev->dev)->platform_data;
	snd_soc_card_cml.dev = &pdev->dev;
	platform_name = mach->mach_params.platform;

	/* set platform name for each dailink */
	ret = snd_soc_fixup_dai_links_platform_name(&snd_soc_card_cml,
						    platform_name);
	if (ret)
		return ret;

	ctx->common_hdmi_codec_drv = mach->mach_params.common_hdmi_codec_drv;

	snd_soc_card_set_drvdata(&snd_soc_card_cml, ctx);

	return devm_snd_soc_register_card(&pdev->dev, &snd_soc_card_cml);
}

static struct platform_driver snd_cml_rt1011_rt5682_driver = {
	.probe = snd_cml_rt1011_probe,
	.driver = {
		.name = "cml_rt1011_rt5682",
		.pm = &snd_soc_pm_ops,
	},
};
module_platform_driver(snd_cml_rt1011_rt5682_driver);

/* Module information */
MODULE_DESCRIPTION("Cometlake Audio Machine driver - RT1011 and RT5682 in I2S mode");
MODULE_AUTHOR("Naveen Manohar <naveen.m@intel.com>");
MODULE_AUTHOR("Sathya Prakash M R <sathya.prakash.m.r@intel.com>");
MODULE_AUTHOR("Shuming Fan <shumingf@realtek.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:cml_rt1011_rt5682");
