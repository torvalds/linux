// SPDX-License-Identifier: GPL-2.0+
/*
 * Machine driver for AMD Vangogh platform using either
 * NAU8821 & CS35L41 or NAU8821 & MAX98388 codecs.
 *
 * Copyright 2021 Advanced Micro Devices, Inc.
 */

#include <linux/acpi.h>
#include <linux/dmi.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/input-event-codes.h>
#include <linux/module.h>
#include <sound/jack.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>

#include "../../codecs/nau8821.h"
#include "acp5x.h"

#define DRV_NAME			"acp5x_mach"
#define DUAL_CHANNEL			2
#define ACP5X_NAU8821_BCLK		3072000
#define ACP5X_NAU8821_FREQ_OUT		12288000
#define ACP5X_NAU8821_COMP_NAME 	"i2c-NVTN2020:00"
#define ACP5X_NAU8821_DAI_NAME		"nau8821-hifi"
#define ACP5X_CS35L41_COMP_LNAME	"spi-VLV1776:00"
#define ACP5X_CS35L41_COMP_RNAME	"spi-VLV1776:01"
#define ACP5X_CS35L41_DAI_NAME		"cs35l41-pcm"
#define ACP5X_MAX98388_COMP_LNAME	"i2c-ADS8388:00"
#define ACP5X_MAX98388_COMP_RNAME	"i2c-ADS8388:01"
#define ACP5X_MAX98388_DAI_NAME		"max98388-aif1"

static struct snd_soc_jack vg_headset;

SND_SOC_DAILINK_DEF(platform,  DAILINK_COMP_ARRAY(COMP_PLATFORM("acp5x_i2s_dma.0")));
SND_SOC_DAILINK_DEF(acp5x_i2s, DAILINK_COMP_ARRAY(COMP_CPU("acp5x_i2s_playcap.0")));
SND_SOC_DAILINK_DEF(acp5x_bt,  DAILINK_COMP_ARRAY(COMP_CPU("acp5x_i2s_playcap.1")));
SND_SOC_DAILINK_DEF(nau8821,   DAILINK_COMP_ARRAY(COMP_CODEC(ACP5X_NAU8821_COMP_NAME,
							     ACP5X_NAU8821_DAI_NAME)));

static struct snd_soc_jack_pin acp5x_nau8821_jack_pins[] = {
	{
		.pin	= "Headphone",
		.mask	= SND_JACK_HEADPHONE,
	},
	{
		.pin	= "Headset Mic",
		.mask	= SND_JACK_MICROPHONE,
	},
};

static const struct snd_kcontrol_new acp5x_8821_controls[] = {
	SOC_DAPM_PIN_SWITCH("Headphone"),
	SOC_DAPM_PIN_SWITCH("Headset Mic"),
	SOC_DAPM_PIN_SWITCH("Int Mic"),
};

static int platform_clock_control(struct snd_soc_dapm_widget *w,
				  struct snd_kcontrol *k, int event)
{
	struct snd_soc_dapm_context *dapm = w->dapm;
	struct snd_soc_card *card = dapm->card;
	struct snd_soc_dai *dai;
	int ret = 0;

	dai = snd_soc_card_get_codec_dai(card, ACP5X_NAU8821_DAI_NAME);
	if (!dai) {
		dev_err(card->dev, "Codec dai not found\n");
		return -EIO;
	}

	if (SND_SOC_DAPM_EVENT_OFF(event)) {
		ret = snd_soc_dai_set_sysclk(dai, NAU8821_CLK_INTERNAL, 0, SND_SOC_CLOCK_IN);
		if (ret < 0) {
			dev_err(card->dev, "set sysclk err = %d\n", ret);
			return -EIO;
		}
	} else {
		ret = snd_soc_dai_set_sysclk(dai, NAU8821_CLK_FLL_BLK, 0, SND_SOC_CLOCK_IN);
		if (ret < 0)
			dev_err(dai->dev, "can't set BLK clock %d\n", ret);
		ret = snd_soc_dai_set_pll(dai, 0, 0, ACP5X_NAU8821_BCLK, ACP5X_NAU8821_FREQ_OUT);
		if (ret < 0)
			dev_err(dai->dev, "can't set FLL: %d\n", ret);
	}

	return ret;
}

static int acp5x_8821_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_component *component = asoc_rtd_to_codec(rtd, 0)->component;
	int ret;

	/*
	 * Headset buttons map to the google Reference headset.
	 * These can be configured by userspace.
	 */
	ret = snd_soc_card_jack_new_pins(rtd->card, "Headset Jack",
					 SND_JACK_HEADSET | SND_JACK_BTN_0,
					 &vg_headset, acp5x_nau8821_jack_pins,
					 ARRAY_SIZE(acp5x_nau8821_jack_pins));
	if (ret) {
		dev_err(rtd->dev, "Headset Jack creation failed %d\n", ret);
		return ret;
	}

	snd_jack_set_key(vg_headset.jack, SND_JACK_BTN_0, KEY_MEDIA);
	nau8821_enable_jack_detect(component, &vg_headset);

	return ret;
}

static const unsigned int rates[] = {
	48000,
};

static const struct snd_pcm_hw_constraint_list constraints_rates = {
	.count = ARRAY_SIZE(rates),
	.list  = rates,
	.mask = 0,
};

static const unsigned int channels[] = {
	2,
};

static const struct snd_pcm_hw_constraint_list constraints_channels = {
	.count = ARRAY_SIZE(channels),
	.list = channels,
	.mask = 0,
};

static const unsigned int acp5x_nau8821_format[] = {32};

static struct snd_pcm_hw_constraint_list constraints_sample_bits = {
	.list = acp5x_nau8821_format,
	.count = ARRAY_SIZE(acp5x_nau8821_format),
};

static int acp5x_8821_startup(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct acp5x_platform_info *machine = snd_soc_card_get_drvdata(rtd->card);

	machine->play_i2s_instance = I2S_SP_INSTANCE;
	machine->cap_i2s_instance = I2S_SP_INSTANCE;

	runtime->hw.channels_max = DUAL_CHANNEL;
	snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_CHANNELS,
				   &constraints_channels);
	snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_RATE,
				   &constraints_rates);
	snd_pcm_hw_constraint_list(substream->runtime, 0,
				   SNDRV_PCM_HW_PARAM_SAMPLE_BITS,
				   &constraints_sample_bits);

	return 0;
}

static int acp5x_nau8821_hw_params(struct snd_pcm_substream *substream,
				   struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct snd_soc_card *card = rtd->card;
	struct snd_soc_dai *dai = snd_soc_card_get_codec_dai(card, ACP5X_NAU8821_DAI_NAME);
	int ret, bclk;

	if (!dai)
		return -EINVAL;

	ret = snd_soc_dai_set_sysclk(dai, NAU8821_CLK_FLL_BLK, 0, SND_SOC_CLOCK_IN);
	if (ret < 0)
		dev_err(card->dev, "can't set FS clock %d\n", ret);

	bclk = snd_soc_params_to_bclk(params);
	if (bclk < 0) {
		dev_err(dai->dev, "Fail to get BCLK rate: %d\n", bclk);
		return bclk;
	}

	ret = snd_soc_dai_set_pll(dai, 0, 0, bclk, params_rate(params) * 256);
	if (ret < 0)
		dev_err(card->dev, "can't set FLL: %d\n", ret);

	return ret;
}

static const struct snd_soc_ops acp5x_8821_ops = {
	.startup = acp5x_8821_startup,
	.hw_params = acp5x_nau8821_hw_params,
};

static int acp5x_cs35l41_startup(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct acp5x_platform_info *machine = snd_soc_card_get_drvdata(rtd->card);
	struct snd_pcm_runtime *runtime = substream->runtime;

	machine->play_i2s_instance = I2S_HS_INSTANCE;

	runtime->hw.channels_max = DUAL_CHANNEL;
	snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_CHANNELS,
				   &constraints_channels);
	snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_RATE,
				   &constraints_rates);

	return 0;
}

static int acp5x_cs35l41_hw_params(struct snd_pcm_substream *substream,
				   struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	unsigned int bclk, rate = params_rate(params);
	struct snd_soc_component *comp;
	int ret, i;

	switch (rate) {
	case 48000:
		bclk = 1536000;
		break;
	default:
		bclk = 0;
		break;
	}

	for_each_rtd_components(rtd, i, comp) {
		if (!(strcmp(comp->name, ACP5X_CS35L41_COMP_LNAME)) ||
		    !(strcmp(comp->name, ACP5X_CS35L41_COMP_RNAME))) {
			if (!bclk) {
				dev_err(comp->dev, "Invalid sample rate: 0x%x\n", rate);
				return -EINVAL;
			}

			ret = snd_soc_component_set_sysclk(comp, 0, 0, bclk, SND_SOC_CLOCK_IN);
			if (ret) {
				dev_err(comp->dev, "failed to set SYSCLK: %d\n", ret);
				return ret;
			}
		}
	}

	return 0;
}

static const struct snd_soc_ops acp5x_cs35l41_play_ops = {
	.startup = acp5x_cs35l41_startup,
	.hw_params = acp5x_cs35l41_hw_params,
};

static struct snd_soc_codec_conf acp5x_cs35l41_conf[] = {
	{
		.dlc = COMP_CODEC_CONF(ACP5X_CS35L41_COMP_LNAME),
		.name_prefix = "Left",
	},
	{
		.dlc = COMP_CODEC_CONF(ACP5X_CS35L41_COMP_RNAME),
		.name_prefix = "Right",
	},
};

SND_SOC_DAILINK_DEF(cs35l41, DAILINK_COMP_ARRAY(COMP_CODEC(ACP5X_CS35L41_COMP_LNAME,
							   ACP5X_CS35L41_DAI_NAME),
						COMP_CODEC(ACP5X_CS35L41_COMP_RNAME,
							   ACP5X_CS35L41_DAI_NAME)));

static struct snd_soc_dai_link acp5x_8821_35l41_dai[] = {
	{
		.name = "acp5x-8821-play",
		.stream_name = "Playback/Capture",
		.dai_fmt = SND_SOC_DAIFMT_I2S |
			   SND_SOC_DAIFMT_NB_NF |
			   SND_SOC_DAIFMT_CBC_CFC,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
		.ops = &acp5x_8821_ops,
		.init = acp5x_8821_init,
		SND_SOC_DAILINK_REG(acp5x_i2s, nau8821, platform),
	},
	{
		.name = "acp5x-CS35L41-Stereo",
		.stream_name = "CS35L41 Stereo Playback",
		.dai_fmt = SND_SOC_DAIFMT_I2S |
			   SND_SOC_DAIFMT_NB_NF |
			   SND_SOC_DAIFMT_CBC_CFC,
		.dpcm_playback = 1,
		.playback_only = 1,
		.ops = &acp5x_cs35l41_play_ops,
		SND_SOC_DAILINK_REG(acp5x_bt, cs35l41, platform),
	},
};

static const struct snd_soc_dapm_widget acp5x_8821_35l41_widgets[] = {
	SND_SOC_DAPM_HP("Headphone", NULL),
	SND_SOC_DAPM_MIC("Headset Mic", NULL),
	SND_SOC_DAPM_MIC("Int Mic", NULL),
	SND_SOC_DAPM_SUPPLY("Platform Clock", SND_SOC_NOPM, 0, 0,
			    platform_clock_control,
			    SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
};

static const struct snd_soc_dapm_route acp5x_8821_35l41_audio_route[] = {
	/* HP jack connectors - unknown if we have jack detection */
	{ "Headphone", NULL, "HPOL" },
	{ "Headphone", NULL, "HPOR" },
	{ "MICL", NULL, "Headset Mic" },
	{ "MICR", NULL, "Headset Mic" },
	{ "DMIC", NULL, "Int Mic" },

	{ "Headphone", NULL, "Platform Clock" },
	{ "Headset Mic", NULL, "Platform Clock" },
	{ "Int Mic", NULL, "Platform Clock" },
};

static struct snd_soc_card acp5x_8821_35l41_card = {
	.name = "acp5x",
	.owner = THIS_MODULE,
	.dai_link = acp5x_8821_35l41_dai,
	.num_links = ARRAY_SIZE(acp5x_8821_35l41_dai),
	.dapm_widgets = acp5x_8821_35l41_widgets,
	.num_dapm_widgets = ARRAY_SIZE(acp5x_8821_35l41_widgets),
	.dapm_routes = acp5x_8821_35l41_audio_route,
	.num_dapm_routes = ARRAY_SIZE(acp5x_8821_35l41_audio_route),
	.codec_conf = acp5x_cs35l41_conf,
	.num_configs = ARRAY_SIZE(acp5x_cs35l41_conf),
	.controls = acp5x_8821_controls,
	.num_controls = ARRAY_SIZE(acp5x_8821_controls),
};

static int acp5x_max98388_startup(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct acp5x_platform_info *machine = snd_soc_card_get_drvdata(rtd->card);
	struct snd_pcm_runtime *runtime = substream->runtime;

	machine->play_i2s_instance = I2S_HS_INSTANCE;

	runtime->hw.channels_max = DUAL_CHANNEL;
	snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_CHANNELS,
				   &constraints_channels);
	snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_RATE,
				   &constraints_rates);
	return 0;
}

static const struct snd_soc_ops acp5x_max98388_play_ops = {
	.startup = acp5x_max98388_startup,
};

static struct snd_soc_codec_conf acp5x_max98388_conf[] = {
	{
		.dlc = COMP_CODEC_CONF(ACP5X_MAX98388_COMP_LNAME),
		.name_prefix = "Left",
	},
	{
		.dlc = COMP_CODEC_CONF(ACP5X_MAX98388_COMP_RNAME),
		.name_prefix = "Right",
	},
};

SND_SOC_DAILINK_DEF(max98388, DAILINK_COMP_ARRAY(COMP_CODEC(ACP5X_MAX98388_COMP_LNAME,
							    ACP5X_MAX98388_DAI_NAME),
						 COMP_CODEC(ACP5X_MAX98388_COMP_RNAME,
							    ACP5X_MAX98388_DAI_NAME)));

static struct snd_soc_dai_link acp5x_8821_98388_dai[] = {
	{
		.name = "acp5x-8821-play",
		.stream_name = "Playback/Capture",
		.dai_fmt = SND_SOC_DAIFMT_I2S |
			   SND_SOC_DAIFMT_NB_NF |
			   SND_SOC_DAIFMT_CBC_CFC,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
		.ops = &acp5x_8821_ops,
		.init = acp5x_8821_init,
		SND_SOC_DAILINK_REG(acp5x_i2s, nau8821, platform),
	},
	{
		.name = "acp5x-max98388-play",
		.stream_name = "MAX98388 Playback",
		.dai_fmt = SND_SOC_DAIFMT_I2S |
			   SND_SOC_DAIFMT_NB_NF |
			   SND_SOC_DAIFMT_CBC_CFC,
		.dpcm_playback = 1,
		.playback_only = 1,
		.ops = &acp5x_max98388_play_ops,
		SND_SOC_DAILINK_REG(acp5x_bt, max98388, platform),
	},
};

static const struct snd_soc_dapm_widget acp5x_8821_98388_widgets[] = {
	SND_SOC_DAPM_HP("Headphone", NULL),
	SND_SOC_DAPM_MIC("Headset Mic", NULL),
	SND_SOC_DAPM_MIC("Int Mic", NULL),
	SND_SOC_DAPM_SUPPLY("Platform Clock", SND_SOC_NOPM, 0, 0,
			    platform_clock_control,
			    SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_SPK("SPK", NULL),
};

static const struct snd_soc_dapm_route acp5x_8821_98388_route[] = {
	{ "Headphone", NULL, "HPOL" },
	{ "Headphone", NULL, "HPOR" },
	{ "MICL", NULL, "Headset Mic" },
	{ "MICR", NULL, "Headset Mic" },
	{ "DMIC", NULL, "Int Mic" },

	{ "Headphone", NULL, "Platform Clock" },
	{ "Headset Mic", NULL, "Platform Clock" },
	{ "Int Mic", NULL, "Platform Clock" },

	{ "SPK", NULL, "Left BE_OUT" },
	{ "SPK", NULL, "Right BE_OUT" },
};

static struct snd_soc_card acp5x_8821_98388_card = {
	.name = "acp5x-max98388",
	.owner = THIS_MODULE,
	.dai_link = acp5x_8821_98388_dai,
	.num_links = ARRAY_SIZE(acp5x_8821_98388_dai),
	.dapm_widgets = acp5x_8821_98388_widgets,
	.num_dapm_widgets = ARRAY_SIZE(acp5x_8821_98388_widgets),
	.dapm_routes = acp5x_8821_98388_route,
	.num_dapm_routes = ARRAY_SIZE(acp5x_8821_98388_route),
	.codec_conf = acp5x_max98388_conf,
	.num_configs = ARRAY_SIZE(acp5x_max98388_conf),
	.controls = acp5x_8821_controls,
	.num_controls = ARRAY_SIZE(acp5x_8821_controls),
};

static const struct dmi_system_id acp5x_vg_quirk_table[] = {
	{
		.matches = {
			DMI_EXACT_MATCH(DMI_BOARD_VENDOR, "Valve"),
			DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "Jupiter"),
		},
		.driver_data = (void *)&acp5x_8821_35l41_card,
	},
	{
		.matches = {
			DMI_EXACT_MATCH(DMI_BOARD_VENDOR, "Valve"),
			DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "Galileo"),
		},
		.driver_data = (void *)&acp5x_8821_98388_card,
	},
	{}
};

static int acp5x_probe(struct platform_device *pdev)
{
	const struct dmi_system_id *dmi_id;
	struct acp5x_platform_info *machine;
	struct device *dev = &pdev->dev;
	struct snd_soc_card *card;
	int ret;

	dmi_id = dmi_first_match(acp5x_vg_quirk_table);
	if (!dmi_id || !dmi_id->driver_data)
		return -ENODEV;

	machine = devm_kzalloc(dev, sizeof(*machine), GFP_KERNEL);
	if (!machine)
		return -ENOMEM;

	card = dmi_id->driver_data;
	card->dev = dev;
	platform_set_drvdata(pdev, card);
	snd_soc_card_set_drvdata(card, machine);

	ret = devm_snd_soc_register_card(dev, card);
	if (ret)
		return dev_err_probe(dev, ret, "Register card (%s) failed\n", card->name);

	return 0;
}

static struct platform_driver acp5x_mach_driver = {
	.driver = {
		.name = DRV_NAME,
		.pm = &snd_soc_pm_ops,
	},
	.probe = acp5x_probe,
};

module_platform_driver(acp5x_mach_driver);

MODULE_AUTHOR("Vijendar.Mukunda@amd.com");
MODULE_DESCRIPTION("NAU8821/CS35L41 & NAU8821/MAX98388 audio support");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" DRV_NAME);
