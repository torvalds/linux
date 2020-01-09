// SPDX-License-Identifier: GPL-2.0
//
// Copyright(c) 2019 Intel Corporation. All rights reserved.

/*
 * tgl_rt711_rt1308.c - ASoc Machine driver for Intel platforms
 * with RT711 codec over SoundWire and RT1308 amplifier over I2S
 */

#include <linux/acpi.h>
#include <linux/async.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/gpio.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/io.h>
#include <linux/dmi.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/soundwire/sdw.h>
#include <linux/soundwire/sdw_type.h>
#include <sound/jack.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-acpi.h>
#include "../../codecs/rt1308.h"
#include "../../codecs/hdac_hdmi.h"
#include "hda_dsp_common.h"

#define MAX_NO_PROPS 2

enum {
	SOF_RT711_JD_SRC_JD1 = 1,
	SOF_RT711_JD_SRC_JD2 = 2,
};

#define SOF_RT711_JDSRC(quirk)		((quirk) & GENMASK(1, 0))

static unsigned long sof_rt711_rt1308_quirk = SOF_RT711_JD_SRC_JD1;

struct mc_private {
	struct list_head hdmi_pcm_list;
	bool common_hdmi_codec_drv;
	struct snd_soc_jack sdw_headset;
};

#if IS_ENABLED(CONFIG_SND_HDA_CODEC_HDMI)
static struct snd_soc_jack hdmi[4];

struct hdmi_pcm {
	struct list_head head;
	struct snd_soc_dai *codec_dai;
	int device;
};

static int hdmi_init(struct snd_soc_pcm_runtime *rtd)
{
	struct mc_private *ctx = snd_soc_card_get_drvdata(rtd->card);
	struct snd_soc_dai *dai = rtd->codec_dai;
	struct hdmi_pcm *pcm;

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
static int card_late_probe(struct snd_soc_card *card)
{
	struct mc_private *ctx = snd_soc_card_get_drvdata(card);
	struct hdmi_pcm *pcm;
	struct snd_soc_component *component = NULL;
	int err, i = 0;
	char jack_name[NAME_SIZE];

	pcm = list_first_entry(&ctx->hdmi_pcm_list, struct hdmi_pcm,
			       head);
	component = pcm->codec_dai->component;

	if (ctx->common_hdmi_codec_drv)
		return hda_dsp_hdmi_build_controls(card, component);

	list_for_each_entry(pcm, &ctx->hdmi_pcm_list, head) {
		component = pcm->codec_dai->component;
		snprintf(jack_name, sizeof(jack_name),
			 "HDMI/DP, pcm=%d Jack", pcm->device);
		err = snd_soc_card_jack_new(card, jack_name,
					    SND_JACK_AVOUT, &hdmi[i],
					    NULL, 0);

		if (err)
			return err;

		err = hdac_hdmi_jack_init(pcm->codec_dai, pcm->device,
					  &hdmi[i]);
		if (err < 0)
			return err;

		i++;
	}

	if (!component)
		return -EINVAL;

	return hdac_hdmi_jack_port_init(component, &card->dapm);
}
#else
static int card_late_probe(struct snd_soc_card *card)
{
	return 0;
}
#endif

static struct snd_soc_jack_pin sdw_jack_pins[] = {
	{
		.pin    = "Headphone",
		.mask   = SND_JACK_HEADPHONE,
	},
	{
		.pin    = "Headset Mic",
		.mask   = SND_JACK_MICROPHONE,
	},
};

static int headset_init(struct snd_soc_pcm_runtime *rtd)
{
	struct mc_private *ctx = snd_soc_card_get_drvdata(rtd->card);
	struct snd_soc_component *component = rtd->codec_dai->component;
	struct snd_soc_jack *jack;
	int ret;

	ret = snd_soc_card_jack_new(rtd->card, "Headset Jack",
				    SND_JACK_HEADSET | SND_JACK_BTN_0 |
				    SND_JACK_BTN_1 | SND_JACK_BTN_2 |
				    SND_JACK_BTN_3,
				    &ctx->sdw_headset,
				    sdw_jack_pins,
				    ARRAY_SIZE(sdw_jack_pins));
	if (ret) {
		dev_err(rtd->card->dev, "Headset Jack creation failed: %d\n",
			ret);
		return ret;
	}

	jack = &ctx->sdw_headset;

	snd_jack_set_key(jack->jack, SND_JACK_BTN_0, KEY_VOLUMEUP);
	snd_jack_set_key(jack->jack, SND_JACK_BTN_1, KEY_PLAYPAUSE);
	snd_jack_set_key(jack->jack, SND_JACK_BTN_2, KEY_VOLUMEDOWN);
	snd_jack_set_key(jack->jack, SND_JACK_BTN_3, KEY_VOICECOMMAND);

	ret = snd_soc_component_set_jack(component, jack, NULL);

	if (ret)
		dev_err(rtd->card->dev, "Headset Jack call-back failed: %d\n",
			ret);

	return ret;
}

static int rt1308_hw_params(struct snd_pcm_substream *substream,
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
static struct snd_soc_ops rt1308_ops = {
	.hw_params = rt1308_hw_params,
};

static int sof_rt711_rt1308_quirk_cb(const struct dmi_system_id *id)
{
	sof_rt711_rt1308_quirk = (unsigned long)id->driver_data;
	return 1;
}

static const struct dmi_system_id sof_sdw_rt711_rt1308_quirk_table[] = {
	{
		.callback = sof_rt711_rt1308_quirk_cb,
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "IntelCorporation"),
			DMI_MATCH(DMI_PRODUCT_NAME, "Tiger Lake Client"),
		},
		.driver_data = (void *)(SOF_RT711_JD_SRC_JD1),
	},
	{}
};

/*
 * Note this MUST be called before snd_soc_register_card(), so that the props
 * are in place before the codec component driver's probe function parses them.
 */
static int sof_rt711_add_codec_device_props(const char *sdw_dev_name)
{
	struct property_entry props[MAX_NO_PROPS] = {};
	struct device *sdw_dev;
	int ret, cnt = 0;
	unsigned int quirk;

	sdw_dev = bus_find_device_by_name(&sdw_bus_type, NULL, sdw_dev_name);
	if (!sdw_dev)
		return -EPROBE_DEFER;

	if (SOF_RT711_JDSRC(sof_rt711_rt1308_quirk)) {
		quirk = SOF_RT711_JDSRC(sof_rt711_rt1308_quirk);
		props[cnt++] = PROPERTY_ENTRY_U32("realtek,jd-src", quirk);
	}

	ret = device_add_properties(sdw_dev, props);
	put_device(sdw_dev);

	return ret;
}

static const struct snd_soc_dapm_widget widgets[] = {
	SND_SOC_DAPM_HP("Headphone", NULL),
	SND_SOC_DAPM_MIC("Headset Mic", NULL),
	SND_SOC_DAPM_SPK("Speakers", NULL),
	SND_SOC_DAPM_MIC("SoC DMIC", NULL),
};

static const struct snd_soc_dapm_route map[] = {
	{ "Headphone", NULL, "HP" },
	{ "MIC2", NULL, "Headset Mic" },

	{ "Speakers", NULL, "SPOL" },
	{ "Speakers", NULL, "SPOR" },
	{ "DMic", NULL, "SoC DMIC"},
};

static const struct snd_kcontrol_new controls[] = {
	SOC_DAPM_PIN_SWITCH("Headphone"),
	SOC_DAPM_PIN_SWITCH("Headset Mic"),
	SOC_DAPM_PIN_SWITCH("Speakers"),
};

SND_SOC_DAILINK_DEF(sdw0_pin2,
	DAILINK_COMP_ARRAY(COMP_CPU("SDW0 Pin2")));
SND_SOC_DAILINK_DEF(sdw0_pin3,
	DAILINK_COMP_ARRAY(COMP_CPU("SDW0 Pin3")));
SND_SOC_DAILINK_DEF(sdw0_codec,
	DAILINK_COMP_ARRAY(COMP_CODEC("sdw:0:25d:711:0", "rt711-aif1")));

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

#if IS_ENABLED(CONFIG_SND_HDA_CODEC_HDMI)
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
#endif

struct snd_soc_dai_link dailink[] = {
	{
		.name = "SDW0-Playback",
		.id = 0,
		.init = headset_init,
		.no_pcm = 1,
		.dpcm_playback = 1,
		.nonatomic = true,
		SND_SOC_DAILINK_REG(sdw0_pin2, sdw0_codec, platform),
	},
	{
		.name = "SDW0-Capture",
		.id = 1,
		.no_pcm = 1,
		.dpcm_capture = 1,
		.nonatomic = true,
		SND_SOC_DAILINK_REG(sdw0_pin3, sdw0_codec, platform),
	},
	{
		.name		= "SSP2-Codec",
		.id		= 2,
		.no_pcm		= 1,
		.ops		= &rt1308_ops,
		.dpcm_playback = 1,
		.nonatomic = true,
		SND_SOC_DAILINK_REG(ssp2_pin, ssp2_codec, platform),
	},
	{
		.name = "dmic01",
		.id = 3,
		.ignore_suspend = 1,
		.dpcm_capture = 1,
		.no_pcm = 1,
		SND_SOC_DAILINK_REG(dmic_pin, dmic_codec, platform),
	},
	{
		.name = "dmic16k",
		.id = 4,
		.ignore_suspend = 1,
		.dpcm_capture = 1,
		.no_pcm = 1,
		SND_SOC_DAILINK_REG(dmic16k, dmic_codec, platform),
	},
#if IS_ENABLED(CONFIG_SND_HDA_CODEC_HDMI)
	{
		.name = "iDisp1",
		.id = 5,
		.init = hdmi_init,
		.dpcm_playback = 1,
		.no_pcm = 1,
		SND_SOC_DAILINK_REG(idisp1_pin, idisp1_codec, platform),
	},
	{
		.name = "iDisp2",
		.id = 6,
		.init = hdmi_init,
		.dpcm_playback = 1,
		.no_pcm = 1,
		SND_SOC_DAILINK_REG(idisp2_pin, idisp2_codec, platform),
	},
	{
		.name = "iDisp3",
		.id = 7,
		.init = hdmi_init,
		.dpcm_playback = 1,
		.no_pcm = 1,
		SND_SOC_DAILINK_REG(idisp3_pin, idisp3_codec, platform),
	},
	{
		.name = "iDisp4",
		.id = 8,
		.init = hdmi_init,
		.dpcm_playback = 1,
		.no_pcm = 1,
		SND_SOC_DAILINK_REG(idisp4_pin, idisp4_codec, platform),
	},
#endif
};

/* SoC card */
static struct snd_soc_card card_rt711_rt1308 = {
	.name = "rt711-rt1308",
	.dai_link = dailink,
	.num_links = ARRAY_SIZE(dailink),
	.controls = controls,
	.num_controls = ARRAY_SIZE(controls),
	.dapm_widgets = widgets,
	.num_dapm_widgets = ARRAY_SIZE(widgets),
	.dapm_routes = map,
	.num_dapm_routes = ARRAY_SIZE(map),
	.late_probe = card_late_probe,
};

static int mc_probe(struct platform_device *pdev)
{
	struct mc_private *ctx;
	struct snd_soc_acpi_mach *mach;
	const char *platform_name;
	struct snd_soc_card *card = &card_rt711_rt1308;
	int ret;

	dev_dbg(&pdev->dev, "Entry %s\n", __func__);

	ctx = devm_kzalloc(&pdev->dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	dmi_check_system(sof_sdw_rt711_rt1308_quirk_table);

#if IS_ENABLED(CONFIG_SND_HDA_CODEC_HDMI)
	INIT_LIST_HEAD(&ctx->hdmi_pcm_list);
#endif

	card->dev = &pdev->dev;

	/* override platform name, if required */
	mach = (&pdev->dev)->platform_data;
	platform_name = mach->mach_params.platform;

	ret = snd_soc_fixup_dai_links_platform_name(card, platform_name);
	if (ret)
		return ret;

	ctx->common_hdmi_codec_drv = mach->mach_params.common_hdmi_codec_drv;

	snd_soc_card_set_drvdata(card, ctx);

	sof_rt711_add_codec_device_props("sdw:0:25d:711:0");

	return devm_snd_soc_register_card(&pdev->dev, card);
}

static struct platform_driver rt711_rt1308_driver = {
	.driver = {
		.name   = "rt711_rt1308",
		.pm = &snd_soc_pm_ops,
	},
	.probe          = mc_probe,
};

module_platform_driver(rt711_rt1308_driver);

MODULE_AUTHOR("Xiuli Pan");
MODULE_DESCRIPTION("ASoC SoundWire RT711 + RT1308 Machine driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:rt711_rt1308");
