/*
 * Copyright (C) 2015 - 2016 Samsung Electronics Co., Ltd.
 *
 * Authors: Inha Song <ideal.song@samsung.com>
 *          Sylwester Nawrocki <s.nawrocki@samsung.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/clk.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

#include "i2s.h"
#include "../codecs/wm5110.h"

/*
 * The source clock is XCLKOUT with its mux set to the external fixed rate
 * oscillator (XXTI).
 */
#define MCLK_RATE	24000000U

#define TM2_DAI_AIF1	0
#define TM2_DAI_AIF2	1

struct tm2_machine_priv {
	struct snd_soc_codec *codec;
	unsigned int sysclk_rate;
	struct gpio_desc *gpio_mic_bias;
};

static int tm2_start_sysclk(struct snd_soc_card *card)
{
	struct tm2_machine_priv *priv = snd_soc_card_get_drvdata(card);
	struct snd_soc_codec *codec = priv->codec;
	int ret;

	ret = snd_soc_codec_set_pll(codec, WM5110_FLL1_REFCLK,
				    ARIZONA_FLL_SRC_MCLK1,
				    MCLK_RATE,
				    priv->sysclk_rate);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to set FLL1 source: %d\n", ret);
		return ret;
	}

	ret = snd_soc_codec_set_pll(codec, WM5110_FLL1,
				    ARIZONA_FLL_SRC_MCLK1,
				    MCLK_RATE,
				    priv->sysclk_rate);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to start FLL1: %d\n", ret);
		return ret;
	}

	ret = snd_soc_codec_set_sysclk(codec, ARIZONA_CLK_SYSCLK,
				       ARIZONA_CLK_SRC_FLL1,
				       priv->sysclk_rate,
				       SND_SOC_CLOCK_IN);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to set SYSCLK source: %d\n", ret);
		return ret;
	}

	return 0;
}

static int tm2_stop_sysclk(struct snd_soc_card *card)
{
	struct tm2_machine_priv *priv = snd_soc_card_get_drvdata(card);
	struct snd_soc_codec *codec = priv->codec;
	int ret;

	ret = snd_soc_codec_set_pll(codec, WM5110_FLL1, 0, 0, 0);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to stop FLL1: %d\n", ret);
		return ret;
	}

	ret = snd_soc_codec_set_sysclk(codec, ARIZONA_CLK_SYSCLK,
				       ARIZONA_CLK_SRC_FLL1, 0, 0);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to stop SYSCLK: %d\n", ret);
		return ret;
	}

	return 0;
}

static int tm2_aif1_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_codec *codec = rtd->codec;
	struct tm2_machine_priv *priv = snd_soc_card_get_drvdata(rtd->card);

	switch (params_rate(params)) {
	case 4000:
	case 8000:
	case 12000:
	case 16000:
	case 24000:
	case 32000:
	case 48000:
	case 96000:
	case 192000:
		/* Highest possible SYSCLK frequency: 147.456MHz */
		priv->sysclk_rate = 147456000U;
		break;
	case 11025:
	case 22050:
	case 44100:
	case 88200:
	case 176400:
		/* Highest possible SYSCLK frequency: 135.4752 MHz */
		priv->sysclk_rate = 135475200U;
		break;
	default:
		dev_err(codec->dev, "Not supported sample rate: %d\n",
			params_rate(params));
		return -EINVAL;
	}

	return tm2_start_sysclk(rtd->card);
}

static struct snd_soc_ops tm2_aif1_ops = {
	.hw_params = tm2_aif1_hw_params,
};

static int tm2_aif2_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_codec *codec = rtd->codec;
	unsigned int asyncclk_rate;
	int ret;

	switch (params_rate(params)) {
	case 8000:
	case 12000:
	case 16000:
		/* Highest possible ASYNCCLK frequency: 49.152MHz */
		asyncclk_rate = 49152000U;
		break;
	case 11025:
		/* Highest possible ASYNCCLK frequency: 45.1584 MHz */
		asyncclk_rate = 45158400U;
		break;
	default:
		dev_err(codec->dev, "Not supported sample rate: %d\n",
			params_rate(params));
		return -EINVAL;
	}

	ret = snd_soc_codec_set_pll(codec, WM5110_FLL2_REFCLK,
				    ARIZONA_FLL_SRC_MCLK1,
				    MCLK_RATE,
				    asyncclk_rate);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to set FLL2 source: %d\n", ret);
		return ret;
	}

	ret = snd_soc_codec_set_pll(codec, WM5110_FLL2,
				    ARIZONA_FLL_SRC_MCLK1,
				    MCLK_RATE,
				    asyncclk_rate);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to start FLL2: %d\n", ret);
		return ret;
	}

	ret = snd_soc_codec_set_sysclk(codec, ARIZONA_CLK_ASYNCCLK,
				       ARIZONA_CLK_SRC_FLL2,
				       asyncclk_rate,
				       SND_SOC_CLOCK_IN);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to set ASYNCCLK source: %d\n", ret);
		return ret;
	}

	return 0;
}

static int tm2_aif2_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_codec *codec = rtd->codec;
	int ret;

	/* disable FLL2 */
	ret = snd_soc_codec_set_pll(codec, WM5110_FLL2, ARIZONA_FLL_SRC_MCLK1,
				    0, 0);
	if (ret < 0)
		dev_err(codec->dev, "Failed to stop FLL2: %d\n", ret);

	return ret;
}

static struct snd_soc_ops tm2_aif2_ops = {
	.hw_params = tm2_aif2_hw_params,
	.hw_free = tm2_aif2_hw_free,
};

static int tm2_mic_bias(struct snd_soc_dapm_widget *w,
				struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_card *card = w->dapm->card;
	struct tm2_machine_priv *priv = snd_soc_card_get_drvdata(card);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		gpiod_set_value_cansleep(priv->gpio_mic_bias,  1);
		break;
	case SND_SOC_DAPM_POST_PMD:
		gpiod_set_value_cansleep(priv->gpio_mic_bias,  0);
		break;
	}

	return 0;
}

static int tm2_set_bias_level(struct snd_soc_card *card,
				struct snd_soc_dapm_context *dapm,
				enum snd_soc_bias_level level)
{
	struct snd_soc_pcm_runtime *rtd;

	rtd = snd_soc_get_pcm_runtime(card, card->dai_link[0].name);

	if (dapm->dev != rtd->codec_dai->dev)
		return 0;

	switch (level) {
	case SND_SOC_BIAS_STANDBY:
		if (card->dapm.bias_level == SND_SOC_BIAS_OFF)
			tm2_start_sysclk(card);
		break;
	case SND_SOC_BIAS_OFF:
		tm2_stop_sysclk(card);
		break;
	default:
		break;
	}

	return 0;
}

static struct snd_soc_aux_dev tm2_speaker_amp_dev;

static int tm2_late_probe(struct snd_soc_card *card)
{
	struct tm2_machine_priv *priv = snd_soc_card_get_drvdata(card);
	struct snd_soc_dai_link_component dlc = { 0 };
	unsigned int ch_map[] = { 0, 1 };
	struct snd_soc_dai *amp_pdm_dai;
	struct snd_soc_pcm_runtime *rtd;
	struct snd_soc_dai *aif1_dai;
	struct snd_soc_dai *aif2_dai;
	int ret;

	rtd = snd_soc_get_pcm_runtime(card, card->dai_link[TM2_DAI_AIF1].name);
	aif1_dai = rtd->codec_dai;
	priv->codec = rtd->codec;

	ret = snd_soc_dai_set_sysclk(aif1_dai, ARIZONA_CLK_SYSCLK, 0, 0);
	if (ret < 0) {
		dev_err(aif1_dai->dev, "Failed to set SYSCLK: %d\n", ret);
		return ret;
	}

	rtd = snd_soc_get_pcm_runtime(card, card->dai_link[TM2_DAI_AIF2].name);
	aif2_dai = rtd->codec_dai;

	ret = snd_soc_dai_set_sysclk(aif2_dai, ARIZONA_CLK_ASYNCCLK, 0, 0);
	if (ret < 0) {
		dev_err(aif2_dai->dev, "Failed to set ASYNCCLK: %d\n", ret);
		return ret;
	}

	dlc.of_node = tm2_speaker_amp_dev.codec_of_node;
	amp_pdm_dai = snd_soc_find_dai(&dlc);
	if (!amp_pdm_dai)
		return -ENODEV;

	/* Set the MAX98504 V/I sense PDM Tx DAI channel mapping */
	ret = snd_soc_dai_set_channel_map(amp_pdm_dai, ARRAY_SIZE(ch_map),
					  ch_map, 0, NULL);
	if (ret < 0)
		return ret;

	ret = snd_soc_dai_set_tdm_slot(amp_pdm_dai, 0x3, 0x0, 2, 16);
	if (ret < 0)
		return ret;

	return 0;
}

static const struct snd_kcontrol_new tm2_controls[] = {
	SOC_DAPM_PIN_SWITCH("HP"),
	SOC_DAPM_PIN_SWITCH("SPK"),
	SOC_DAPM_PIN_SWITCH("RCV"),
	SOC_DAPM_PIN_SWITCH("VPS"),
	SOC_DAPM_PIN_SWITCH("HDMI"),

	SOC_DAPM_PIN_SWITCH("Main Mic"),
	SOC_DAPM_PIN_SWITCH("Sub Mic"),
	SOC_DAPM_PIN_SWITCH("Third Mic"),

	SOC_DAPM_PIN_SWITCH("Headset Mic"),
};

static const struct snd_soc_dapm_widget tm2_dapm_widgets[] = {
	SND_SOC_DAPM_HP("HP", NULL),
	SND_SOC_DAPM_SPK("SPK", NULL),
	SND_SOC_DAPM_SPK("RCV", NULL),
	SND_SOC_DAPM_LINE("VPS", NULL),
	SND_SOC_DAPM_LINE("HDMI", NULL),

	SND_SOC_DAPM_MIC("Main Mic", tm2_mic_bias),
	SND_SOC_DAPM_MIC("Sub Mic", NULL),
	SND_SOC_DAPM_MIC("Third Mic", NULL),

	SND_SOC_DAPM_MIC("Headset Mic", NULL),
};

static const struct snd_soc_component_driver tm2_component = {
	.name	= "tm2-audio",
};

static struct snd_soc_dai_driver tm2_ext_dai[] = {
	{
		.name = "Voice call",
		.playback = {
			.channels_min = 1,
			.channels_max = 4,
			.rate_min = 8000,
			.rate_max = 48000,
			.rates = (SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000 |
					SNDRV_PCM_RATE_48000),
			.formats = SNDRV_PCM_FMTBIT_S16_LE,
		},
		.capture = {
			.channels_min = 1,
			.channels_max = 4,
			.rate_min = 8000,
			.rate_max = 48000,
			.rates = (SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000 |
					SNDRV_PCM_RATE_48000),
			.formats = SNDRV_PCM_FMTBIT_S16_LE,
		},
	},
	{
		.name = "Bluetooth",
		.playback = {
			.channels_min = 1,
			.channels_max = 4,
			.rate_min = 8000,
			.rate_max = 16000,
			.rates = (SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000),
			.formats = SNDRV_PCM_FMTBIT_S16_LE,
		},
		.capture = {
			.channels_min = 1,
			.channels_max = 2,
			.rate_min = 8000,
			.rate_max = 16000,
			.rates = (SNDRV_PCM_RATE_8000 | SNDRV_PCM_RATE_16000),
			.formats = SNDRV_PCM_FMTBIT_S16_LE,
		},
	},
};

static struct snd_soc_dai_link tm2_dai_links[] = {
	{
		.name		= "WM5110 AIF1",
		.stream_name	= "HiFi Primary",
		.cpu_dai_name   = SAMSUNG_I2S_DAI,
		.codec_dai_name = "wm5110-aif1",
		.ops		= &tm2_aif1_ops,
		.dai_fmt	= SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
				  SND_SOC_DAIFMT_CBM_CFM,
	}, {
		.name		= "WM5110 Voice",
		.stream_name	= "Voice call",
		.cpu_dai_name   = SAMSUNG_I2S_DAI,
		.codec_dai_name = "wm5110-aif2",
		.ops		= &tm2_aif2_ops,
		.dai_fmt	= SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
				  SND_SOC_DAIFMT_CBM_CFM,
		.ignore_suspend = 1,
	}, {
		.name		= "WM5110 BT",
		.stream_name	= "Bluetooth",
		.cpu_dai_name   = SAMSUNG_I2S_DAI,
		.codec_dai_name = "wm5110-aif3",
		.dai_fmt	= SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
				  SND_SOC_DAIFMT_CBM_CFM,
		.ignore_suspend = 1,
	}
};

static struct snd_soc_card tm2_card = {
	.owner			= THIS_MODULE,

	.dai_link		= tm2_dai_links,
	.num_links		= ARRAY_SIZE(tm2_dai_links),
	.controls		= tm2_controls,
	.num_controls		= ARRAY_SIZE(tm2_controls),
	.dapm_widgets		= tm2_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(tm2_dapm_widgets),
	.aux_dev		= &tm2_speaker_amp_dev,
	.num_aux_devs		= 1,

	.late_probe		= tm2_late_probe,
	.set_bias_level		= tm2_set_bias_level,
};

static int tm2_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct snd_soc_card *card = &tm2_card;
	struct tm2_machine_priv *priv;
	struct device_node *cpu_dai_node, *codec_dai_node;
	int ret, i;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	snd_soc_card_set_drvdata(card, priv);
	card->dev = dev;

	priv->gpio_mic_bias = devm_gpiod_get(dev, "mic-bias", GPIOD_OUT_HIGH);
	if (IS_ERR(priv->gpio_mic_bias)) {
		dev_err(dev, "Failed to get mic bias gpio\n");
		return PTR_ERR(priv->gpio_mic_bias);
	}

	ret = snd_soc_of_parse_card_name(card, "model");
	if (ret < 0) {
		dev_err(dev, "Card name is not specified\n");
		return ret;
	}

	ret = snd_soc_of_parse_audio_routing(card, "samsung,audio-routing");
	if (ret < 0) {
		dev_err(dev, "Audio routing is not specified or invalid\n");
		return ret;
	}

	card->aux_dev[0].codec_of_node = of_parse_phandle(dev->of_node,
							"audio-amplifier", 0);
	if (!card->aux_dev[0].codec_of_node) {
		dev_err(dev, "audio-amplifier property invalid or missing\n");
		return -EINVAL;
	}

	cpu_dai_node = of_parse_phandle(dev->of_node, "i2s-controller", 0);
	if (!cpu_dai_node) {
		dev_err(dev, "i2s-controllers property invalid or missing\n");
		ret = -EINVAL;
		goto amp_node_put;
	}

	codec_dai_node = of_parse_phandle(dev->of_node, "audio-codec", 0);
	if (!codec_dai_node) {
		dev_err(dev, "audio-codec property invalid or missing\n");
		ret = -EINVAL;
		goto cpu_dai_node_put;
	}

	for (i = 0; i < card->num_links; i++) {
		card->dai_link[i].cpu_name = NULL;
		card->dai_link[i].platform_name = NULL;
		card->dai_link[i].codec_of_node = codec_dai_node;
		card->dai_link[i].cpu_of_node = cpu_dai_node;
		card->dai_link[i].platform_of_node = cpu_dai_node;
	}

	ret = devm_snd_soc_register_component(dev, &tm2_component,
				tm2_ext_dai, ARRAY_SIZE(tm2_ext_dai));
	if (ret < 0) {
		dev_err(dev, "Failed to register component: %d\n", ret);
		goto codec_dai_node_put;
	}

	ret = devm_snd_soc_register_card(dev, card);
	if (ret < 0) {
		dev_err(dev, "Failed to register card: %d\n", ret);
		goto codec_dai_node_put;
	}

codec_dai_node_put:
	of_node_put(codec_dai_node);
cpu_dai_node_put:
	of_node_put(cpu_dai_node);
amp_node_put:
	of_node_put(card->aux_dev[0].codec_of_node);
	return ret;
}

static int tm2_pm_prepare(struct device *dev)
{
	struct snd_soc_card *card = dev_get_drvdata(dev);

	return tm2_stop_sysclk(card);
}

static void tm2_pm_complete(struct device *dev)
{
	struct snd_soc_card *card = dev_get_drvdata(dev);

	tm2_start_sysclk(card);
}

static const struct dev_pm_ops tm2_pm_ops = {
	.prepare	= tm2_pm_prepare,
	.suspend	= snd_soc_suspend,
	.resume		= snd_soc_resume,
	.complete	= tm2_pm_complete,
	.freeze		= snd_soc_suspend,
	.thaw		= snd_soc_resume,
	.poweroff	= snd_soc_poweroff,
	.restore	= snd_soc_resume,
};

static const struct of_device_id tm2_of_match[] = {
	{ .compatible = "samsung,tm2-audio" },
	{ },
};
MODULE_DEVICE_TABLE(of, tm2_of_match);

static struct platform_driver tm2_driver = {
	.driver = {
		.name		= "tm2-audio",
		.pm		= &tm2_pm_ops,
		.of_match_table	= tm2_of_match,
	},
	.probe	= tm2_probe,
};
module_platform_driver(tm2_driver);

MODULE_AUTHOR("Inha Song <ideal.song@samsung.com>");
MODULE_DESCRIPTION("ALSA SoC Exynos TM2 Audio Support");
MODULE_LICENSE("GPL v2");
