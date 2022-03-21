// SPDX-License-Identifier: GPL-2.0+
/*
 * Machine driver for AMD Vangogh platform using NAU8821 & CS35L41
 * codecs.
 *
 * Copyright 2021 Advanced Micro Devices, Inc.
 */

#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <linux/module.h>
#include <linux/io.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>

#include <sound/jack.h>
#include <linux/clk.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/io.h>
#include <linux/acpi.h>
#include <linux/dmi.h>

#include "../../codecs/nau8821.h"
#include "../../codecs/cs35l41.h"

#include "acp5x.h"

#define DRV_NAME "acp5x_mach"
#define DUAL_CHANNEL		2
#define ACP5X_NUVOTON_CODEC_DAI	"nau8821-hifi"
#define VG_JUPITER 1
#define ACP5X_NUVOTON_BCLK 3072000
#define ACP5X_NAU8821_FREQ_OUT 12288000

static unsigned long acp5x_machine_id;
static struct snd_soc_jack vg_headset;

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

static int acp5x_8821_init(struct snd_soc_pcm_runtime *rtd)
{
	int ret;
	struct snd_soc_card *card = rtd->card;
	struct snd_soc_component *component =
					asoc_rtd_to_codec(rtd, 0)->component;

	/*
	 * Headset buttons map to the google Reference headset.
	 * These can be configured by userspace.
	 */
	ret = snd_soc_card_jack_new(card, "Headset Jack",
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

static int acp5x_cs35l41_init(struct snd_soc_pcm_runtime *rtd)
{
	return 0;
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
	struct snd_soc_card *card = rtd->card;
	struct acp5x_platform_info *machine = snd_soc_card_get_drvdata(card);

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
	struct snd_soc_dai *codec_dai =
			snd_soc_card_get_codec_dai(card,
						   ACP5X_NUVOTON_CODEC_DAI);
	int ret;

	ret = snd_soc_dai_set_sysclk(codec_dai, NAU8821_CLK_FLL_BLK, 0,
				     SND_SOC_CLOCK_IN);
	if (ret < 0)
		dev_err(card->dev, "can't set FS clock %d\n", ret);
	ret = snd_soc_dai_set_pll(codec_dai, 0, 0, snd_soc_params_to_bclk(params),
				  params_rate(params) * 256);
	if (ret < 0)
		dev_err(card->dev, "can't set FLL: %d\n", ret);

	return ret;
}

static int acp5x_cs35l41_startup(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	struct snd_soc_card *card = rtd->card;
	struct acp5x_platform_info *machine = snd_soc_card_get_drvdata(card);

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
	struct snd_soc_card *card = rtd->card;
	struct snd_soc_dai *codec_dai;
	int ret, i;
	unsigned int num_codecs = rtd->num_codecs;
	unsigned int bclk_val;

	ret = 0;
	for (i = 0; i < num_codecs; i++) {
		codec_dai = asoc_rtd_to_codec(rtd, i);
		if ((strcmp(codec_dai->name, "spi-VLV1776:00") == 0) ||
		    (strcmp(codec_dai->name, "spi-VLV1776:01") == 0)) {
			switch (params_rate(params)) {
			case 48000:
				bclk_val = 1536000;
				break;
			default:
				dev_err(card->dev, "Invalid Samplerate:0x%x\n",
					params_rate(params));
				return -EINVAL;
			}
			ret = snd_soc_component_set_sysclk(codec_dai->component,
							   0, 0, bclk_val, SND_SOC_CLOCK_IN);
			if (ret < 0) {
				dev_err(card->dev, "failed to set sysclk for CS35l41 dai\n");
				return ret;
			}
		}
	}

	return ret;
}

static const struct snd_soc_ops acp5x_8821_ops = {
	.startup = acp5x_8821_startup,
	.hw_params = acp5x_nau8821_hw_params,
};

static const struct snd_soc_ops acp5x_cs35l41_play_ops = {
	.startup = acp5x_cs35l41_startup,
	.hw_params = acp5x_cs35l41_hw_params,
};

static struct snd_soc_codec_conf cs35l41_conf[] = {
	{
		.dlc = COMP_CODEC_CONF("spi-VLV1776:00"),
		.name_prefix = "Left",
	},
	{
		.dlc = COMP_CODEC_CONF("spi-VLV1776:01"),
		.name_prefix = "Right",
	},
};

SND_SOC_DAILINK_DEF(acp5x_i2s,
		    DAILINK_COMP_ARRAY(COMP_CPU("acp5x_i2s_playcap.0")));

SND_SOC_DAILINK_DEF(acp5x_bt,
		    DAILINK_COMP_ARRAY(COMP_CPU("acp5x_i2s_playcap.1")));

SND_SOC_DAILINK_DEF(nau8821,
		    DAILINK_COMP_ARRAY(COMP_CODEC("i2c-NVTN2020:00",
						  "nau8821-hifi")));

SND_SOC_DAILINK_DEF(cs35l41,
		    DAILINK_COMP_ARRAY(COMP_CODEC("spi-VLV1776:00", "cs35l41-pcm"),
				       COMP_CODEC("spi-VLV1776:01", "cs35l41-pcm")));

SND_SOC_DAILINK_DEF(platform,
		    DAILINK_COMP_ARRAY(COMP_PLATFORM("acp5x_i2s_dma.0")));

static struct snd_soc_dai_link acp5x_dai[] = {
	{
		.name = "acp5x-8821-play",
		.stream_name = "Playback/Capture",
		.dai_fmt = SND_SOC_DAIFMT_I2S  | SND_SOC_DAIFMT_NB_NF |
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
		.dai_fmt = SND_SOC_DAIFMT_I2S  | SND_SOC_DAIFMT_NB_NF |
			   SND_SOC_DAIFMT_CBC_CFC,
		.dpcm_playback = 1,
		.playback_only = 1,
		.ops = &acp5x_cs35l41_play_ops,
		.init = acp5x_cs35l41_init,
		SND_SOC_DAILINK_REG(acp5x_bt, cs35l41, platform),
	},
};

static int platform_clock_control(struct snd_soc_dapm_widget *w,
				  struct snd_kcontrol *k, int  event)
{
	struct snd_soc_dapm_context *dapm = w->dapm;
	struct snd_soc_card *card = dapm->card;
	struct snd_soc_dai *codec_dai;
	int ret = 0;

	codec_dai = snd_soc_card_get_codec_dai(card, ACP5X_NUVOTON_CODEC_DAI);
	if (!codec_dai) {
		dev_err(card->dev, "Codec dai not found\n");
		return -EIO;
	}

	if (SND_SOC_DAPM_EVENT_OFF(event)) {
		ret = snd_soc_dai_set_sysclk(codec_dai, NAU8821_CLK_INTERNAL,
					     0, SND_SOC_CLOCK_IN);
		if (ret < 0) {
			dev_err(card->dev, "set sysclk err = %d\n", ret);
			return -EIO;
		}
	} else {
		ret = snd_soc_dai_set_sysclk(codec_dai, NAU8821_CLK_FLL_BLK, 0,
					     SND_SOC_CLOCK_IN);
		if (ret < 0)
			dev_err(codec_dai->dev, "can't set BLK clock %d\n", ret);
		ret = snd_soc_dai_set_pll(codec_dai, 0, 0, ACP5X_NUVOTON_BCLK,
					  ACP5X_NAU8821_FREQ_OUT);
		if (ret < 0)
			dev_err(codec_dai->dev, "can't set FLL: %d\n", ret);
	}
	return ret;
}

static const struct snd_kcontrol_new acp5x_8821_controls[] = {
	SOC_DAPM_PIN_SWITCH("Headphone"),
	SOC_DAPM_PIN_SWITCH("Headset Mic"),
	SOC_DAPM_PIN_SWITCH("Int Mic"),
};

static const struct snd_soc_dapm_widget acp5x_8821_widgets[] = {
	SND_SOC_DAPM_HP("Headphone", NULL),
	SND_SOC_DAPM_MIC("Headset Mic", NULL),
	SND_SOC_DAPM_MIC("Int Mic", NULL),
	SND_SOC_DAPM_SUPPLY("Platform Clock", SND_SOC_NOPM, 0, 0,
			    platform_clock_control, SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
};

static const struct snd_soc_dapm_route acp5x_8821_audio_route[] = {
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

static struct snd_soc_card acp5x_card = {
	.name = "acp5x",
	.owner = THIS_MODULE,
	.dai_link = acp5x_dai,
	.num_links = ARRAY_SIZE(acp5x_dai),
	.dapm_widgets = acp5x_8821_widgets,
	.num_dapm_widgets = ARRAY_SIZE(acp5x_8821_widgets),
	.dapm_routes = acp5x_8821_audio_route,
	.num_dapm_routes = ARRAY_SIZE(acp5x_8821_audio_route),
	.codec_conf = cs35l41_conf,
	.num_configs = ARRAY_SIZE(cs35l41_conf),
	.controls = acp5x_8821_controls,
	.num_controls = ARRAY_SIZE(acp5x_8821_controls),
};

static int acp5x_vg_quirk_cb(const struct dmi_system_id *id)
{
	acp5x_machine_id = VG_JUPITER;
	return 1;
}

static const struct dmi_system_id acp5x_vg_quirk_table[] = {
	{
		.callback = acp5x_vg_quirk_cb,
		.matches = {
			DMI_EXACT_MATCH(DMI_BOARD_VENDOR, "Valve"),
			DMI_EXACT_MATCH(DMI_PRODUCT_NAME, "Jupiter"),
		}
	},
	{}
};

static int acp5x_probe(struct platform_device *pdev)
{
	int ret;
	struct acp5x_platform_info *machine;
	struct snd_soc_card *card;

	machine = devm_kzalloc(&pdev->dev, sizeof(struct acp5x_platform_info),
			       GFP_KERNEL);
	if (!machine)
		return -ENOMEM;

	dmi_check_system(acp5x_vg_quirk_table);
	switch (acp5x_machine_id) {
	case VG_JUPITER:
		card = &acp5x_card;
		acp5x_card.dev = &pdev->dev;
		break;
	default:
		return -ENODEV;
	}
	platform_set_drvdata(pdev, card);
	snd_soc_card_set_drvdata(card, machine);

	ret = devm_snd_soc_register_card(&pdev->dev, card);
	if (ret) {
		return dev_err_probe(&pdev->dev, ret,
				     "snd_soc_register_card(%s) failed\n",
				     acp5x_card.name);
	}
	return 0;
}

static struct platform_driver acp5x_mach_driver = {
	.driver = {
		.name = "acp5x_mach",
		.pm = &snd_soc_pm_ops,
	},
	.probe = acp5x_probe,
};

module_platform_driver(acp5x_mach_driver);

MODULE_AUTHOR("Vijendar.Mukunda@amd.com");
MODULE_DESCRIPTION("NAU8821 & CS35L41 audio support");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" DRV_NAME);
