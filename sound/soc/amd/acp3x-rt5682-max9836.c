// SPDX-License-Identifier: GPL-2.0+
//
// Machine driver for AMD ACP Audio engine using DA7219 & MAX98357 codec.
//
//Copyright 2016 Advanced Micro Devices, Inc.

#include <sound/core.h>
#include <sound/soc.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc-dapm.h>
#include <sound/jack.h>
#include <linux/clk.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/io.h>
#include <linux/acpi.h>

#include "raven/acp3x.h"
#include "../codecs/rt5682.h"

#define PCO_PLAT_CLK 48000000
#define RT5682_PLL_FREQ (48000 * 512)
#define DUAL_CHANNEL		2

static struct snd_soc_jack pco_jack;
static struct clk *rt5682_dai_wclk;
static struct clk *rt5682_dai_bclk;
static struct gpio_desc *dmic_sel;

static int acp3x_5682_init(struct snd_soc_pcm_runtime *rtd)
{
	int ret;
	struct snd_soc_card *card = rtd->card;
	struct snd_soc_dai *codec_dai = asoc_rtd_to_codec(rtd, 0);
	struct snd_soc_component *component = codec_dai->component;

	dev_info(rtd->dev, "codec dai name = %s\n", codec_dai->name);

	/* set rt5682 dai fmt */
	ret =  snd_soc_dai_set_fmt(codec_dai, SND_SOC_DAIFMT_I2S
			| SND_SOC_DAIFMT_NB_NF
			| SND_SOC_DAIFMT_CBM_CFM);
	if (ret < 0) {
		dev_err(rtd->card->dev,
				"Failed to set rt5682 dai fmt: %d\n", ret);
		return ret;
	}

	/* set codec PLL */
	ret = snd_soc_dai_set_pll(codec_dai, RT5682_PLL2, RT5682_PLL2_S_MCLK,
				  PCO_PLAT_CLK, RT5682_PLL_FREQ);
	if (ret < 0) {
		dev_err(rtd->dev, "can't set rt5682 PLL: %d\n", ret);
		return ret;
	}

	/* Set codec sysclk */
	ret = snd_soc_dai_set_sysclk(codec_dai, RT5682_SCLK_S_PLL2,
			RT5682_PLL_FREQ, SND_SOC_CLOCK_IN);
	if (ret < 0) {
		dev_err(rtd->dev,
			"Failed to set rt5682 SYSCLK: %d\n", ret);
		return ret;
	}

	/* Set tdm/i2s1 master bclk ratio */
	ret = snd_soc_dai_set_bclk_ratio(codec_dai, 64);
	if (ret < 0) {
		dev_err(rtd->dev,
			"Failed to set rt5682 tdm bclk ratio: %d\n", ret);
		return ret;
	}

	rt5682_dai_wclk = clk_get(component->dev, "rt5682-dai-wclk");
	rt5682_dai_bclk = clk_get(component->dev, "rt5682-dai-bclk");

	ret = snd_soc_card_jack_new(card, "Headset Jack",
				SND_JACK_HEADSET | SND_JACK_LINEOUT |
				SND_JACK_BTN_0 | SND_JACK_BTN_1 |
				SND_JACK_BTN_2 | SND_JACK_BTN_3,
				&pco_jack, NULL, 0);
	if (ret) {
		dev_err(card->dev, "HP jack creation failed %d\n", ret);
		return ret;
	}

	snd_jack_set_key(pco_jack.jack, SND_JACK_BTN_0, KEY_PLAYPAUSE);
	snd_jack_set_key(pco_jack.jack, SND_JACK_BTN_1, KEY_VOICECOMMAND);
	snd_jack_set_key(pco_jack.jack, SND_JACK_BTN_2, KEY_VOLUMEUP);
	snd_jack_set_key(pco_jack.jack, SND_JACK_BTN_3, KEY_VOLUMEDOWN);

	ret = snd_soc_component_set_jack(component, &pco_jack, NULL);
	if (ret) {
		dev_err(rtd->dev, "Headset Jack call-back failed: %d\n", ret);
		return ret;
	}

	return ret;
}

static int rt5682_clk_enable(struct snd_pcm_substream *substream)
{
	int ret = 0;
	struct snd_soc_pcm_runtime *rtd = substream->private_data;

	/* RT5682 will support only 48K output with 48M mclk */
	clk_set_rate(rt5682_dai_wclk, 48000);
	clk_set_rate(rt5682_dai_bclk, 48000 * 64);
	ret = clk_prepare_enable(rt5682_dai_wclk);
	if (ret < 0) {
		dev_err(rtd->dev, "can't enable wclk %d\n", ret);
		return ret;
	}

	return ret;
}

static void rt5682_clk_disable(void)
{
	clk_disable_unprepare(rt5682_dai_wclk);
}

static const unsigned int channels[] = {
	DUAL_CHANNEL,
};

static const unsigned int rates[] = {
	48000,
};

static const struct snd_pcm_hw_constraint_list constraints_rates = {
	.count = ARRAY_SIZE(rates),
	.list  = rates,
	.mask = 0,
};

static const struct snd_pcm_hw_constraint_list constraints_channels = {
	.count = ARRAY_SIZE(channels),
	.list = channels,
	.mask = 0,
};

static int acp3x_5682_startup(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_card *card = rtd->card;
	struct acp3x_platform_info *machine = snd_soc_card_get_drvdata(card);

	machine->play_i2s_instance = I2S_SP_INSTANCE;
	machine->cap_i2s_instance = I2S_SP_INSTANCE;

	runtime->hw.channels_max = DUAL_CHANNEL;
	snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_CHANNELS,
				   &constraints_channels);
	snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_RATE,
				   &constraints_rates);
	return rt5682_clk_enable(substream);
}

static int acp3x_max_startup(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_card *card = rtd->card;
	struct acp3x_platform_info *machine = snd_soc_card_get_drvdata(card);

	machine->play_i2s_instance = I2S_BT_INSTANCE;

	runtime->hw.channels_max = DUAL_CHANNEL;
	snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_CHANNELS,
				   &constraints_channels);
	snd_pcm_hw_constraint_list(runtime, 0, SNDRV_PCM_HW_PARAM_RATE,
				   &constraints_rates);
	return rt5682_clk_enable(substream);
}

static int acp3x_ec_dmic0_startup(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_card *card = rtd->card;
	struct snd_soc_dai *codec_dai = asoc_rtd_to_codec(rtd, 0);
	struct acp3x_platform_info *machine = snd_soc_card_get_drvdata(card);

	machine->cap_i2s_instance = I2S_BT_INSTANCE;
	snd_soc_dai_set_bclk_ratio(codec_dai, 64);
	if (dmic_sel)
		gpiod_set_value(dmic_sel, 0);

	return rt5682_clk_enable(substream);
}

static int acp3x_ec_dmic1_startup(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_card *card = rtd->card;
	struct snd_soc_dai *codec_dai = asoc_rtd_to_codec(rtd, 0);
	struct acp3x_platform_info *machine = snd_soc_card_get_drvdata(card);

	machine->cap_i2s_instance = I2S_BT_INSTANCE;
	snd_soc_dai_set_bclk_ratio(codec_dai, 64);
	if (dmic_sel)
		gpiod_set_value(dmic_sel, 1);

	return rt5682_clk_enable(substream);
}

static void rt5682_shutdown(struct snd_pcm_substream *substream)
{
	rt5682_clk_disable();
}

static const struct snd_soc_ops acp3x_5682_ops = {
	.startup = acp3x_5682_startup,
	.shutdown = rt5682_shutdown,
};

static const struct snd_soc_ops acp3x_max_play_ops = {
	.startup = acp3x_max_startup,
	.shutdown = rt5682_shutdown,
};

static const struct snd_soc_ops acp3x_ec_cap0_ops = {
	.startup = acp3x_ec_dmic0_startup,
	.shutdown = rt5682_shutdown,
};

static const struct snd_soc_ops acp3x_ec_cap1_ops = {
	.startup = acp3x_ec_dmic1_startup,
	.shutdown = rt5682_shutdown,
};

SND_SOC_DAILINK_DEF(acp3x_i2s,
	DAILINK_COMP_ARRAY(COMP_CPU("acp3x_i2s_playcap.0")));
SND_SOC_DAILINK_DEF(acp3x_bt,
	DAILINK_COMP_ARRAY(COMP_CPU("acp3x_i2s_playcap.2")));

SND_SOC_DAILINK_DEF(rt5682,
	DAILINK_COMP_ARRAY(COMP_CODEC("i2c-10EC5682:00", "rt5682-aif1")));
SND_SOC_DAILINK_DEF(max,
	DAILINK_COMP_ARRAY(COMP_CODEC("MX98357A:00", "HiFi")));
SND_SOC_DAILINK_DEF(cros_ec,
	DAILINK_COMP_ARRAY(COMP_CODEC("GOOG0013:00", "EC Codec I2S RX")));

SND_SOC_DAILINK_DEF(platform,
	DAILINK_COMP_ARRAY(COMP_PLATFORM("acp3x_rv_i2s_dma.0")));

static struct snd_soc_dai_link acp3x_dai_5682_98357[] = {
	{
		.name = "acp3x-5682-play",
		.stream_name = "Playback",
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF
				| SND_SOC_DAIFMT_CBM_CFM,
		.init = acp3x_5682_init,
		.dpcm_playback = 1,
		.dpcm_capture = 1,
		.ops = &acp3x_5682_ops,
		SND_SOC_DAILINK_REG(acp3x_i2s, rt5682, platform),
	},
	{
		.name = "acp3x-max98357-play",
		.stream_name = "HiFi Playback",
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF
				| SND_SOC_DAIFMT_CBM_CFM,
		.dpcm_playback = 1,
		.ops = &acp3x_max_play_ops,
		SND_SOC_DAILINK_REG(acp3x_bt, max, platform),
	},
	{
		.name = "acp3x-ec-dmic0-capture",
		.stream_name = "Capture DMIC0",
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF
				| SND_SOC_DAIFMT_CBS_CFS,
		.dpcm_capture = 1,
		.ops = &acp3x_ec_cap0_ops,
		SND_SOC_DAILINK_REG(acp3x_bt, cros_ec, platform),
	},
	{
		.name = "acp3x-ec-dmic1-capture",
		.stream_name = "Capture DMIC1",
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF
				| SND_SOC_DAIFMT_CBS_CFS,
		.dpcm_capture = 1,
		.ops = &acp3x_ec_cap1_ops,
		SND_SOC_DAILINK_REG(acp3x_bt, cros_ec, platform),
	},
};

static const struct snd_soc_dapm_widget acp3x_widgets[] = {
	SND_SOC_DAPM_HP("Headphone Jack", NULL),
	SND_SOC_DAPM_SPK("Spk", NULL),
	SND_SOC_DAPM_MIC("Headset Mic", NULL),
};

static const struct snd_soc_dapm_route acp3x_audio_route[] = {
	{"Headphone Jack", NULL, "HPOL"},
	{"Headphone Jack", NULL, "HPOR"},
	{"IN1P", NULL, "Headset Mic"},
	{"Spk", NULL, "Speaker"},
};

static const struct snd_kcontrol_new acp3x_mc_controls[] = {
	SOC_DAPM_PIN_SWITCH("Headphone Jack"),
	SOC_DAPM_PIN_SWITCH("Spk"),
	SOC_DAPM_PIN_SWITCH("Headset Mic"),
};

static struct snd_soc_card acp3x_card = {
	.name = "acp3xalc5682m98357",
	.owner = THIS_MODULE,
	.dai_link = acp3x_dai_5682_98357,
	.num_links = ARRAY_SIZE(acp3x_dai_5682_98357),
	.dapm_widgets = acp3x_widgets,
	.num_dapm_widgets = ARRAY_SIZE(acp3x_widgets),
	.dapm_routes = acp3x_audio_route,
	.num_dapm_routes = ARRAY_SIZE(acp3x_audio_route),
	.controls = acp3x_mc_controls,
	.num_controls = ARRAY_SIZE(acp3x_mc_controls),
};

static int acp3x_probe(struct platform_device *pdev)
{
	int ret;
	struct snd_soc_card *card;
	struct acp3x_platform_info *machine;

	machine = devm_kzalloc(&pdev->dev, sizeof(*machine), GFP_KERNEL);
	if (!machine)
		return -ENOMEM;

	card = &acp3x_card;
	acp3x_card.dev = &pdev->dev;
	platform_set_drvdata(pdev, card);
	snd_soc_card_set_drvdata(card, machine);

	dmic_sel = devm_gpiod_get(&pdev->dev, "dmic", GPIOD_OUT_LOW);
	if (IS_ERR(dmic_sel)) {
		dev_err(&pdev->dev, "DMIC gpio failed err=%ld\n",
			PTR_ERR(dmic_sel));
		return PTR_ERR(dmic_sel);
	}

	ret = devm_snd_soc_register_card(&pdev->dev, &acp3x_card);
	if (ret) {
		dev_err(&pdev->dev,
				"devm_snd_soc_register_card(%s) failed: %d\n",
				acp3x_card.name, ret);
		return ret;
	}
	return 0;
}

static const struct acpi_device_id acp3x_audio_acpi_match[] = {
	{ "AMDI5682", 0 },
	{},
};
MODULE_DEVICE_TABLE(acpi, acp3x_audio_acpi_match);

static struct platform_driver acp3x_audio = {
	.driver = {
		.name = "acp3x-alc5682-max98357",
		.acpi_match_table = ACPI_PTR(acp3x_audio_acpi_match),
		.pm = &snd_soc_pm_ops,
	},
	.probe = acp3x_probe,
};

module_platform_driver(acp3x_audio);

MODULE_AUTHOR("akshu.agrawal@amd.com");
MODULE_DESCRIPTION("ALC5682 & MAX98357 audio support");
MODULE_LICENSE("GPL v2");
