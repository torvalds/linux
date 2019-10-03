// SPDX-License-Identifier: GPL-2.0-only
/*
 * Rockchip machine ASoC driver for boards using a MAX90809 CODEC.
 *
 * Copyright (c) 2014, ROCKCHIP CORPORATION.  All rights reserved.
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <sound/core.h>
#include <sound/jack.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

#include "rockchip_i2s.h"
#include "../codecs/ts3a227e.h"

#define DRV_NAME "rockchip-snd-max98090"

static struct snd_soc_jack headset_jack;

/* Headset jack detection DAPM pins */
static struct snd_soc_jack_pin headset_jack_pins[] = {
	{
		.pin = "Headphone",
		.mask = SND_JACK_HEADPHONE,
	},
	{
		.pin = "Headset Mic",
		.mask = SND_JACK_MICROPHONE,
	},

};

static const struct snd_soc_dapm_widget rk_dapm_widgets[] = {
	SND_SOC_DAPM_HP("Headphone", NULL),
	SND_SOC_DAPM_MIC("Headset Mic", NULL),
	SND_SOC_DAPM_MIC("Int Mic", NULL),
	SND_SOC_DAPM_SPK("Speaker", NULL),
};

static const struct snd_soc_dapm_route rk_audio_map[] = {
	{"IN34", NULL, "Headset Mic"},
	{"Headset Mic", NULL, "MICBIAS"},
	{"DMICL", NULL, "Int Mic"},
	{"Headphone", NULL, "HPL"},
	{"Headphone", NULL, "HPR"},
	{"Speaker", NULL, "SPKL"},
	{"Speaker", NULL, "SPKR"},
};

static const struct snd_kcontrol_new rk_mc_controls[] = {
	SOC_DAPM_PIN_SWITCH("Headphone"),
	SOC_DAPM_PIN_SWITCH("Headset Mic"),
	SOC_DAPM_PIN_SWITCH("Int Mic"),
	SOC_DAPM_PIN_SWITCH("Speaker"),
};

static int rk_jack_event(struct notifier_block *nb, unsigned long event,
			 void *data)
{
	struct snd_soc_jack *jack = (struct snd_soc_jack *)data;
	struct snd_soc_dapm_context *dapm = &jack->card->dapm;

	if (event & SND_JACK_MICROPHONE)
		snd_soc_dapm_force_enable_pin(dapm, "MICBIAS");
	else
		snd_soc_dapm_disable_pin(dapm, "MICBIAS");

	snd_soc_dapm_sync(dapm);

	return 0;
}

static struct notifier_block rk_jack_nb = {
	.notifier_call = rk_jack_event,
};

static int rk_init(struct snd_soc_pcm_runtime *runtime)
{
	/*
	 * The jack has already been created in the rk_98090_headset_init()
	 * function.
	 */
	snd_soc_jack_notifier_register(&headset_jack, &rk_jack_nb);

	return 0;
}

static int rk_aif1_hw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *params)
{
	int ret = 0;
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	int mclk;

	switch (params_rate(params)) {
	case 8000:
	case 16000:
	case 24000:
	case 32000:
	case 48000:
	case 64000:
	case 96000:
		mclk = 12288000;
		break;
	case 11025:
	case 22050:
	case 44100:
	case 88200:
		mclk = 11289600;
		break;
	default:
		return -EINVAL;
	}

	ret = snd_soc_dai_set_sysclk(cpu_dai, 0, mclk,
				     SND_SOC_CLOCK_OUT);
	if (ret < 0) {
		dev_err(codec_dai->dev, "Can't set codec clock %d\n", ret);
		return ret;
	}

	ret = snd_soc_dai_set_sysclk(codec_dai, 0, mclk,
				     SND_SOC_CLOCK_IN);
	if (ret < 0) {
		dev_err(codec_dai->dev, "Can't set codec clock %d\n", ret);
		return ret;
	}

	return ret;
}

static int rk_aif1_startup(struct snd_pcm_substream *substream)
{
	/*
	 * Set period size to 240 because pl330 has issue
	 * dealing with larger period in stress testing.
	 */
	return snd_pcm_hw_constraint_minmax(substream->runtime,
			SNDRV_PCM_HW_PARAM_PERIOD_SIZE, 240, 240);
}

static const struct snd_soc_ops rk_aif1_ops = {
	.hw_params = rk_aif1_hw_params,
	.startup = rk_aif1_startup,
};

SND_SOC_DAILINK_DEFS(hifi,
	DAILINK_COMP_ARRAY(COMP_EMPTY()),
	DAILINK_COMP_ARRAY(COMP_CODEC(NULL, "HiFi")),
	DAILINK_COMP_ARRAY(COMP_EMPTY()));

static struct snd_soc_dai_link rk_dailink = {
	.name = "max98090",
	.stream_name = "Audio",
	.init = rk_init,
	.ops = &rk_aif1_ops,
	/* set max98090 as slave */
	.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
		SND_SOC_DAIFMT_CBS_CFS,
	SND_SOC_DAILINK_REG(hifi),
};

static int rk_98090_headset_init(struct snd_soc_component *component);

static struct snd_soc_aux_dev rk_98090_headset_dev = {
	.dlc = COMP_EMPTY(),
	.init = rk_98090_headset_init,
};

static struct snd_soc_card snd_soc_card_rk = {
	.name = "ROCKCHIP-I2S",
	.owner = THIS_MODULE,
	.dai_link = &rk_dailink,
	.num_links = 1,
	.aux_dev = &rk_98090_headset_dev,
	.num_aux_devs = 1,
	.dapm_widgets = rk_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(rk_dapm_widgets),
	.dapm_routes = rk_audio_map,
	.num_dapm_routes = ARRAY_SIZE(rk_audio_map),
	.controls = rk_mc_controls,
	.num_controls = ARRAY_SIZE(rk_mc_controls),
};

static int rk_98090_headset_init(struct snd_soc_component *component)
{
	int ret;

	/* Enable Headset and 4 Buttons Jack detection */
	ret = snd_soc_card_jack_new(&snd_soc_card_rk, "Headset Jack",
				    SND_JACK_HEADSET |
				    SND_JACK_BTN_0 | SND_JACK_BTN_1 |
				    SND_JACK_BTN_2 | SND_JACK_BTN_3,
				    &headset_jack,
				    headset_jack_pins,
				    ARRAY_SIZE(headset_jack_pins));
	if (ret)
		return ret;

	ret = ts3a227e_enable_jack_detect(component, &headset_jack);

	return ret;
}

static int snd_rk_mc_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct snd_soc_card *card = &snd_soc_card_rk;
	struct device_node *np = pdev->dev.of_node;

	/* register the soc card */
	card->dev = &pdev->dev;

	rk_dailink.codecs->of_node = of_parse_phandle(np,
			"rockchip,audio-codec", 0);
	if (!rk_dailink.codecs->of_node) {
		dev_err(&pdev->dev,
			"Property 'rockchip,audio-codec' missing or invalid\n");
		return -EINVAL;
	}

	rk_dailink.cpus->of_node = of_parse_phandle(np,
			"rockchip,i2s-controller", 0);
	if (!rk_dailink.cpus->of_node) {
		dev_err(&pdev->dev,
			"Property 'rockchip,i2s-controller' missing or invalid\n");
		return -EINVAL;
	}

	rk_dailink.platforms->of_node = rk_dailink.cpus->of_node;

	rk_98090_headset_dev.dlc.of_node = of_parse_phandle(np,
			"rockchip,headset-codec", 0);
	if (!rk_98090_headset_dev.dlc.of_node) {
		dev_err(&pdev->dev,
			"Property 'rockchip,headset-codec' missing/invalid\n");
		return -EINVAL;
	}

	ret = snd_soc_of_parse_card_name(card, "rockchip,model");
	if (ret) {
		dev_err(&pdev->dev,
			"Soc parse card name failed %d\n", ret);
		return ret;
	}

	ret = devm_snd_soc_register_card(&pdev->dev, card);
	if (ret) {
		dev_err(&pdev->dev,
			"Soc register card failed %d\n", ret);
		return ret;
	}

	return ret;
}

static const struct of_device_id rockchip_max98090_of_match[] = {
	{ .compatible = "rockchip,rockchip-audio-max98090", },
	{},
};

MODULE_DEVICE_TABLE(of, rockchip_max98090_of_match);

static struct platform_driver snd_rk_mc_driver = {
	.probe = snd_rk_mc_probe,
	.driver = {
		.name = DRV_NAME,
		.pm = &snd_soc_pm_ops,
		.of_match_table = rockchip_max98090_of_match,
	},
};

module_platform_driver(snd_rk_mc_driver);

MODULE_AUTHOR("jianqun <jay.xu@rock-chips.com>");
MODULE_DESCRIPTION("Rockchip max98090 machine ASoC driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" DRV_NAME);
