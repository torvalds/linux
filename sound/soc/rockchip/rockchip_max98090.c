// SPDX-License-Identifier: GPL-2.0-only
/*
 * Rockchip machine ASoC driver for boards using a MAX90809 CODEC.
 *
 * Copyright (c) 2014, ROCKCHIP CORPORATION.  All rights reserved.
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
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

#define RK_MAX98090_WIDGETS \
	SND_SOC_DAPM_HP("Headphone", NULL), \
	SND_SOC_DAPM_MIC("Headset Mic", NULL), \
	SND_SOC_DAPM_MIC("Int Mic", NULL), \
	SND_SOC_DAPM_SPK("Speaker", NULL)

#define RK_HDMI_WIDGETS \
	SND_SOC_DAPM_LINE("HDMI", NULL)

static const struct snd_soc_dapm_widget rk_max98090_dapm_widgets[] = {
	RK_MAX98090_WIDGETS,
};

static const struct snd_soc_dapm_widget rk_hdmi_dapm_widgets[] = {
	RK_HDMI_WIDGETS,
};

static const struct snd_soc_dapm_widget rk_max98090_hdmi_dapm_widgets[] = {
	RK_MAX98090_WIDGETS,
	RK_HDMI_WIDGETS,
};

#define RK_MAX98090_AUDIO_MAP \
	{"IN34", NULL, "Headset Mic"}, \
	{"Headset Mic", NULL, "MICBIAS"}, \
	{"DMICL", NULL, "Int Mic"}, \
	{"Headphone", NULL, "HPL"}, \
	{"Headphone", NULL, "HPR"}, \
	{"Speaker", NULL, "SPKL"}, \
	{"Speaker", NULL, "SPKR"}

#define RK_HDMI_AUDIO_MAP \
	{"HDMI", NULL, "TX"}

static const struct snd_soc_dapm_route rk_max98090_audio_map[] = {
	RK_MAX98090_AUDIO_MAP,
};

static const struct snd_soc_dapm_route rk_hdmi_audio_map[] = {
	RK_HDMI_AUDIO_MAP,
};

static const struct snd_soc_dapm_route rk_max98090_hdmi_audio_map[] = {
	RK_MAX98090_AUDIO_MAP,
	RK_HDMI_AUDIO_MAP,
};

#define RK_MAX98090_CONTROLS \
	SOC_DAPM_PIN_SWITCH("Headphone"), \
	SOC_DAPM_PIN_SWITCH("Headset Mic"), \
	SOC_DAPM_PIN_SWITCH("Int Mic"), \
	SOC_DAPM_PIN_SWITCH("Speaker")

#define RK_HDMI_CONTROLS \
	SOC_DAPM_PIN_SWITCH("HDMI")

static const struct snd_kcontrol_new rk_max98090_controls[] = {
	RK_MAX98090_CONTROLS,
};

static const struct snd_kcontrol_new rk_hdmi_controls[] = {
	RK_HDMI_CONTROLS,
};

static const struct snd_kcontrol_new rk_max98090_hdmi_controls[] = {
	RK_MAX98090_CONTROLS,
	RK_HDMI_CONTROLS,
};

static int rk_jack_event(struct notifier_block *nb, unsigned long event,
			 void *data)
{
	struct snd_soc_jack *jack = (struct snd_soc_jack *)data;
	struct snd_soc_dapm_context *dapm = &jack->card->dapm;

	if (event & SND_JACK_MICROPHONE) {
		snd_soc_dapm_force_enable_pin(dapm, "MICBIAS");
		snd_soc_dapm_force_enable_pin(dapm, "SHDN");
	} else {
		snd_soc_dapm_disable_pin(dapm, "MICBIAS");
		snd_soc_dapm_disable_pin(dapm, "SHDN");
	}

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
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct snd_soc_dai *cpu_dai = snd_soc_rtd_to_cpu(rtd, 0);
	struct snd_soc_dai *codec_dai = snd_soc_rtd_to_codec(rtd, 0);
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
	if (ret) {
		dev_err(cpu_dai->dev, "Can't set cpu dai clock %d\n", ret);
		return ret;
	}

	ret = snd_soc_dai_set_sysclk(codec_dai, 0, mclk,
				     SND_SOC_CLOCK_IN);

	/* HDMI codec dai does not need to set sysclk. */
	if (!strcmp(rtd->dai_link->name, "HDMI"))
		return 0;

	if (ret) {
		dev_err(codec_dai->dev, "Can't set codec dai clock %d\n", ret);
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

SND_SOC_DAILINK_DEFS(analog,
		     DAILINK_COMP_ARRAY(COMP_EMPTY()),
		     DAILINK_COMP_ARRAY(COMP_CODEC(NULL, "HiFi")),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));

SND_SOC_DAILINK_DEFS(hdmi,
		     DAILINK_COMP_ARRAY(COMP_EMPTY()),
		     DAILINK_COMP_ARRAY(COMP_CODEC(NULL, "i2s-hifi")),
		     DAILINK_COMP_ARRAY(COMP_EMPTY()));

enum {
	DAILINK_MAX98090,
	DAILINK_HDMI,
};

static struct snd_soc_jack rk_hdmi_jack;

static int rk_hdmi_init(struct snd_soc_pcm_runtime *runtime)
{
	struct snd_soc_card *card = runtime->card;
	struct snd_soc_component *component = snd_soc_rtd_to_codec(runtime, 0)->component;
	int ret;

	/* enable jack detection */
	ret = snd_soc_card_jack_new(card, "HDMI Jack", SND_JACK_LINEOUT,
				    &rk_hdmi_jack);
	if (ret) {
		dev_err(card->dev, "Can't new HDMI Jack %d\n", ret);
		return ret;
	}

	return snd_soc_component_set_jack(component, &rk_hdmi_jack, NULL);
}

/* max98090 dai_link */
static struct snd_soc_dai_link rk_max98090_dailinks[] = {
	{
		.name = "max98090",
		.stream_name = "Analog",
		.init = rk_init,
		.ops = &rk_aif1_ops,
		/* set max98090 as slave */
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
			SND_SOC_DAIFMT_CBC_CFC,
		SND_SOC_DAILINK_REG(analog),
	},
};

/* HDMI codec dai_link */
static struct snd_soc_dai_link rk_hdmi_dailinks[] = {
	{
		.name = "HDMI",
		.stream_name = "HDMI",
		.init = rk_hdmi_init,
		.ops = &rk_aif1_ops,
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
			SND_SOC_DAIFMT_CBC_CFC,
		SND_SOC_DAILINK_REG(hdmi),
	}
};

/* max98090 and HDMI codec dai_link */
static struct snd_soc_dai_link rk_max98090_hdmi_dailinks[] = {
	[DAILINK_MAX98090] = {
		.name = "max98090",
		.stream_name = "Analog",
		.init = rk_init,
		.ops = &rk_aif1_ops,
		/* set max98090 as slave */
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
			SND_SOC_DAIFMT_CBC_CFC,
		SND_SOC_DAILINK_REG(analog),
	},
	[DAILINK_HDMI] = {
		.name = "HDMI",
		.stream_name = "HDMI",
		.init = rk_hdmi_init,
		.ops = &rk_aif1_ops,
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
			SND_SOC_DAIFMT_CBC_CFC,
		SND_SOC_DAILINK_REG(hdmi),
	}
};

static int rk_98090_headset_init(struct snd_soc_component *component);

static struct snd_soc_aux_dev rk_98090_headset_dev = {
	.dlc = COMP_EMPTY(),
	.init = rk_98090_headset_init,
};

static struct snd_soc_card rockchip_max98090_card = {
	.name = "ROCKCHIP-I2S",
	.owner = THIS_MODULE,
	.dai_link = rk_max98090_dailinks,
	.num_links = ARRAY_SIZE(rk_max98090_dailinks),
	.aux_dev = &rk_98090_headset_dev,
	.num_aux_devs = 1,
	.dapm_widgets = rk_max98090_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(rk_max98090_dapm_widgets),
	.dapm_routes = rk_max98090_audio_map,
	.num_dapm_routes = ARRAY_SIZE(rk_max98090_audio_map),
	.controls = rk_max98090_controls,
	.num_controls = ARRAY_SIZE(rk_max98090_controls),
};

static struct snd_soc_card rockchip_hdmi_card = {
	.name = "ROCKCHIP-HDMI",
	.owner = THIS_MODULE,
	.dai_link = rk_hdmi_dailinks,
	.num_links = ARRAY_SIZE(rk_hdmi_dailinks),
	.dapm_widgets = rk_hdmi_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(rk_hdmi_dapm_widgets),
	.dapm_routes = rk_hdmi_audio_map,
	.num_dapm_routes = ARRAY_SIZE(rk_hdmi_audio_map),
	.controls = rk_hdmi_controls,
	.num_controls = ARRAY_SIZE(rk_hdmi_controls),
};

static struct snd_soc_card rockchip_max98090_hdmi_card = {
	.name = "ROCKCHIP-MAX98090-HDMI",
	.owner = THIS_MODULE,
	.dai_link = rk_max98090_hdmi_dailinks,
	.num_links = ARRAY_SIZE(rk_max98090_hdmi_dailinks),
	.aux_dev = &rk_98090_headset_dev,
	.num_aux_devs = 1,
	.dapm_widgets = rk_max98090_hdmi_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(rk_max98090_hdmi_dapm_widgets),
	.dapm_routes = rk_max98090_hdmi_audio_map,
	.num_dapm_routes = ARRAY_SIZE(rk_max98090_hdmi_audio_map),
	.controls = rk_max98090_hdmi_controls,
	.num_controls = ARRAY_SIZE(rk_max98090_hdmi_controls),
};

static int rk_98090_headset_init(struct snd_soc_component *component)
{
	int ret;

	/* Enable Headset and 4 Buttons Jack detection */
	ret = snd_soc_card_jack_new_pins(component->card, "Headset Jack",
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

static int rk_parse_headset_from_of(struct device *dev, struct device_node *np)
{
	rk_98090_headset_dev.dlc.of_node = of_parse_phandle(
			np, "rockchip,headset-codec", 0);
	if (!rk_98090_headset_dev.dlc.of_node) {
		dev_err(dev,
			"Property 'rockchip,headset-codec' missing/invalid\n");
		return -EINVAL;
	}
	return 0;
}

static int snd_rk_mc_probe(struct platform_device *pdev)
{
	int ret = 0;
	struct snd_soc_card *card;
	struct device *dev = &pdev->dev;
	struct device_node *np = pdev->dev.of_node;
	struct device_node *np_cpu;
	struct device_node *np_audio, *np_hdmi;

	/* Parse DTS for I2S controller. */
	np_cpu = of_parse_phandle(np, "rockchip,i2s-controller", 0);

	if (!np_cpu) {
		dev_err(&pdev->dev,
			"Property 'rockchip,i2s-controller missing or invalid\n");
		return -EINVAL;
	}

	/*
	 * Find the card to use based on the presences of audio codec
	 * and hdmi codec in device property. Set their of_node accordingly.
	 */
	np_audio = of_parse_phandle(np, "rockchip,audio-codec", 0);
	np_hdmi = of_parse_phandle(np, "rockchip,hdmi-codec", 0);
	if (np_audio && np_hdmi) {
		card = &rockchip_max98090_hdmi_card;
		card->dai_link[DAILINK_MAX98090].codecs->of_node = np_audio;
		card->dai_link[DAILINK_HDMI].codecs->of_node = np_hdmi;
		card->dai_link[DAILINK_MAX98090].cpus->of_node = np_cpu;
		card->dai_link[DAILINK_MAX98090].platforms->of_node = np_cpu;
		card->dai_link[DAILINK_HDMI].cpus->of_node = np_cpu;
		card->dai_link[DAILINK_HDMI].platforms->of_node = np_cpu;
	} else if (np_audio) {
		card = &rockchip_max98090_card;
		card->dai_link[0].codecs->of_node = np_audio;
		card->dai_link[0].cpus->of_node = np_cpu;
		card->dai_link[0].platforms->of_node = np_cpu;
	} else if (np_hdmi) {
		card = &rockchip_hdmi_card;
		card->dai_link[0].codecs->of_node = np_hdmi;
		card->dai_link[0].cpus->of_node = np_cpu;
		card->dai_link[0].platforms->of_node = np_cpu;
	} else {
		dev_err(dev, "At least one of codecs should be specified\n");
		return -EINVAL;
	}

	card->dev = dev;

	/* Parse headset detection codec. */
	if (np_audio) {
		ret = rk_parse_headset_from_of(dev, np);
		if (ret)
			return ret;
	}

	/* Parse card name. */
	ret = snd_soc_of_parse_card_name(card, "rockchip,model");
	if (ret) {
		dev_err(&pdev->dev,
			"Soc parse card name failed %d\n", ret);
		return ret;
	}

	/* register the soc card */
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
