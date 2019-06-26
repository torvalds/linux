// SPDX-License-Identifier: GPL-2.0-only
/*
 * omap-twl4030.c  --  SoC audio for TI SoC based boards with twl4030 codec
 *
 * Copyright (C) 2012 Texas Instruments Incorporated - http://www.ti.com
 * All rights reserved.
 *
 * Author: Peter Ujfalusi <peter.ujfalusi@ti.com>
 *
 * This driver replaces the following machine drivers:
 * omap3beagle (Author: Steve Sakoman <steve@sakoman.com>)
 * omap3evm (Author: Anuj Aggarwal <anuj.aggarwal@ti.com>)
 * overo (Author: Steve Sakoman <steve@sakoman.com>)
 * igep0020 (Author: Enric Balletbo i Serra <eballetbo@iseebcn.com>)
 * zoom2 (Author: Misael Lopez Cruz <misael.lopez@ti.com>)
 * sdp3430 (Author: Misael Lopez Cruz <misael.lopez@ti.com>)
 */

#include <linux/platform_device.h>
#include <linux/platform_data/omap-twl4030.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/jack.h>

#include "omap-mcbsp.h"

struct omap_twl4030 {
	int jack_detect;	/* board can detect jack events */
	struct snd_soc_jack hs_jack;
};

static int omap_twl4030_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	unsigned int fmt;

	switch (params_channels(params)) {
	case 2: /* Stereo I2S mode */
		fmt =	SND_SOC_DAIFMT_I2S |
			SND_SOC_DAIFMT_NB_NF |
			SND_SOC_DAIFMT_CBM_CFM;
		break;
	case 4: /* Four channel TDM mode */
		fmt =	SND_SOC_DAIFMT_DSP_A |
			SND_SOC_DAIFMT_IB_NF |
			SND_SOC_DAIFMT_CBM_CFM;
		break;
	default:
		return -EINVAL;
	}

	return snd_soc_runtime_set_dai_fmt(rtd, fmt);
}

static const struct snd_soc_ops omap_twl4030_ops = {
	.hw_params = omap_twl4030_hw_params,
};

static const struct snd_soc_dapm_widget dapm_widgets[] = {
	SND_SOC_DAPM_SPK("Earpiece Spk", NULL),
	SND_SOC_DAPM_SPK("Handsfree Spk", NULL),
	SND_SOC_DAPM_HP("Headset Stereophone", NULL),
	SND_SOC_DAPM_SPK("Ext Spk", NULL),
	SND_SOC_DAPM_SPK("Carkit Spk", NULL),

	SND_SOC_DAPM_MIC("Main Mic", NULL),
	SND_SOC_DAPM_MIC("Sub Mic", NULL),
	SND_SOC_DAPM_MIC("Headset Mic", NULL),
	SND_SOC_DAPM_MIC("Carkit Mic", NULL),
	SND_SOC_DAPM_MIC("Digital0 Mic", NULL),
	SND_SOC_DAPM_MIC("Digital1 Mic", NULL),
	SND_SOC_DAPM_LINE("Line In", NULL),
};

static const struct snd_soc_dapm_route audio_map[] = {
	/* Headset Stereophone:  HSOL, HSOR */
	{"Headset Stereophone", NULL, "HSOL"},
	{"Headset Stereophone", NULL, "HSOR"},
	/* External Speakers: HFL, HFR */
	{"Handsfree Spk", NULL, "HFL"},
	{"Handsfree Spk", NULL, "HFR"},
	/* External Speakers: PredrivL, PredrivR */
	{"Ext Spk", NULL, "PREDRIVEL"},
	{"Ext Spk", NULL, "PREDRIVER"},
	/* Carkit speakers:  CARKITL, CARKITR */
	{"Carkit Spk", NULL, "CARKITL"},
	{"Carkit Spk", NULL, "CARKITR"},
	/* Earpiece */
	{"Earpiece Spk", NULL, "EARPIECE"},

	/* External Mics: MAINMIC, SUBMIC with bias */
	{"MAINMIC", NULL, "Main Mic"},
	{"Main Mic", NULL, "Mic Bias 1"},
	{"SUBMIC", NULL, "Sub Mic"},
	{"Sub Mic", NULL, "Mic Bias 2"},
	/* Headset Mic: HSMIC with bias */
	{"HSMIC", NULL, "Headset Mic"},
	{"Headset Mic", NULL, "Headset Mic Bias"},
	/* Digital Mics: DIGIMIC0, DIGIMIC1 with bias */
	{"DIGIMIC0", NULL, "Digital0 Mic"},
	{"Digital0 Mic", NULL, "Mic Bias 1"},
	{"DIGIMIC1", NULL, "Digital1 Mic"},
	{"Digital1 Mic", NULL, "Mic Bias 2"},
	/* Carkit In: CARKITMIC */
	{"CARKITMIC", NULL, "Carkit Mic"},
	/* Aux In: AUXL, AUXR */
	{"AUXL", NULL, "Line In"},
	{"AUXR", NULL, "Line In"},
};

/* Headset jack detection DAPM pins */
static struct snd_soc_jack_pin hs_jack_pins[] = {
	{
		.pin = "Headset Mic",
		.mask = SND_JACK_MICROPHONE,
	},
	{
		.pin = "Headset Stereophone",
		.mask = SND_JACK_HEADPHONE,
	},
};

/* Headset jack detection gpios */
static struct snd_soc_jack_gpio hs_jack_gpios[] = {
	{
		.name = "hsdet-gpio",
		.report = SND_JACK_HEADSET,
		.debounce_time = 200,
	},
};

static inline void twl4030_disconnect_pin(struct snd_soc_dapm_context *dapm,
					  int connected, char *pin)
{
	if (!connected)
		snd_soc_dapm_disable_pin(dapm, pin);
}

static int omap_twl4030_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_card *card = rtd->card;
	struct snd_soc_dapm_context *dapm = &card->dapm;
	struct omap_tw4030_pdata *pdata = dev_get_platdata(card->dev);
	struct omap_twl4030 *priv = snd_soc_card_get_drvdata(card);
	int ret = 0;

	/* Headset jack detection only if it is supported */
	if (priv->jack_detect > 0) {
		hs_jack_gpios[0].gpio = priv->jack_detect;

		ret = snd_soc_card_jack_new(rtd->card, "Headset Jack",
					    SND_JACK_HEADSET, &priv->hs_jack,
					    hs_jack_pins,
					    ARRAY_SIZE(hs_jack_pins));
		if (ret)
			return ret;

		ret = snd_soc_jack_add_gpios(&priv->hs_jack,
					     ARRAY_SIZE(hs_jack_gpios),
					     hs_jack_gpios);
		if (ret)
			return ret;
	}

	/*
	 * NULL pdata means we booted with DT. In this case the routing is
	 * provided and the card is fully routed, no need to mark pins.
	 */
	if (!pdata || !pdata->custom_routing)
		return ret;

	/* Disable not connected paths if not used */
	twl4030_disconnect_pin(dapm, pdata->has_ear, "Earpiece Spk");
	twl4030_disconnect_pin(dapm, pdata->has_hf, "Handsfree Spk");
	twl4030_disconnect_pin(dapm, pdata->has_hs, "Headset Stereophone");
	twl4030_disconnect_pin(dapm, pdata->has_predriv, "Ext Spk");
	twl4030_disconnect_pin(dapm, pdata->has_carkit, "Carkit Spk");

	twl4030_disconnect_pin(dapm, pdata->has_mainmic, "Main Mic");
	twl4030_disconnect_pin(dapm, pdata->has_submic, "Sub Mic");
	twl4030_disconnect_pin(dapm, pdata->has_hsmic, "Headset Mic");
	twl4030_disconnect_pin(dapm, pdata->has_carkitmic, "Carkit Mic");
	twl4030_disconnect_pin(dapm, pdata->has_digimic0, "Digital0 Mic");
	twl4030_disconnect_pin(dapm, pdata->has_digimic1, "Digital1 Mic");
	twl4030_disconnect_pin(dapm, pdata->has_linein, "Line In");

	return ret;
}

/* Digital audio interface glue - connects codec <--> CPU */
SND_SOC_DAILINK_DEFS(hifi,
	DAILINK_COMP_ARRAY(COMP_CPU("omap-mcbsp.2")),
	DAILINK_COMP_ARRAY(COMP_CODEC("twl4030-codec", "twl4030-hifi")));

SND_SOC_DAILINK_DEFS(voice,
	DAILINK_COMP_ARRAY(COMP_CPU("omap-mcbsp.3")),
	DAILINK_COMP_ARRAY(COMP_CODEC("twl4030-codec", "twl4030-voice")));

static struct snd_soc_dai_link omap_twl4030_dai_links[] = {
	{
		.name = "TWL4030 HiFi",
		.stream_name = "TWL4030 HiFi",
		.init = omap_twl4030_init,
		.ops = &omap_twl4030_ops,
		SND_SOC_DAILINK_REG(hifi),
	},
	{
		.name = "TWL4030 Voice",
		.stream_name = "TWL4030 Voice",
		.dai_fmt = SND_SOC_DAIFMT_DSP_A | SND_SOC_DAIFMT_IB_NF |
			   SND_SOC_DAIFMT_CBM_CFM,
		SND_SOC_DAILINK_REG(voice),
	},
};

/* Audio machine driver */
static struct snd_soc_card omap_twl4030_card = {
	.owner = THIS_MODULE,
	.dai_link = omap_twl4030_dai_links,
	.num_links = ARRAY_SIZE(omap_twl4030_dai_links),

	.dapm_widgets = dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(dapm_widgets),
	.dapm_routes = audio_map,
	.num_dapm_routes = ARRAY_SIZE(audio_map),
};

static int omap_twl4030_probe(struct platform_device *pdev)
{
	struct omap_tw4030_pdata *pdata = dev_get_platdata(&pdev->dev);
	struct device_node *node = pdev->dev.of_node;
	struct snd_soc_card *card = &omap_twl4030_card;
	struct omap_twl4030 *priv;
	int ret = 0;

	card->dev = &pdev->dev;

	priv = devm_kzalloc(&pdev->dev, sizeof(struct omap_twl4030), GFP_KERNEL);
	if (priv == NULL)
		return -ENOMEM;

	if (node) {
		struct device_node *dai_node;
		struct property *prop;

		if (snd_soc_of_parse_card_name(card, "ti,model")) {
			dev_err(&pdev->dev, "Card name is not provided\n");
			return -ENODEV;
		}

		dai_node = of_parse_phandle(node, "ti,mcbsp", 0);
		if (!dai_node) {
			dev_err(&pdev->dev, "McBSP node is not provided\n");
			return -EINVAL;
		}
		omap_twl4030_dai_links[0].cpus->dai_name  = NULL;
		omap_twl4030_dai_links[0].cpus->of_node = dai_node;

		dai_node = of_parse_phandle(node, "ti,mcbsp-voice", 0);
		if (!dai_node) {
			card->num_links = 1;
		} else {
			omap_twl4030_dai_links[1].cpus->dai_name  = NULL;
			omap_twl4030_dai_links[1].cpus->of_node = dai_node;
		}

		priv->jack_detect = of_get_named_gpio(node,
						      "ti,jack-det-gpio", 0);

		/* Optional: audio routing can be provided */
		prop = of_find_property(node, "ti,audio-routing", NULL);
		if (prop) {
			ret = snd_soc_of_parse_audio_routing(card,
							    "ti,audio-routing");
			if (ret)
				return ret;

			card->fully_routed = 1;
		}
	} else if (pdata) {
		if (pdata->card_name) {
			card->name = pdata->card_name;
		} else {
			dev_err(&pdev->dev, "Card name is not provided\n");
			return -ENODEV;
		}

		if (!pdata->voice_connected)
			card->num_links = 1;

		priv->jack_detect = pdata->jack_detect;
	} else {
		dev_err(&pdev->dev, "Missing pdata\n");
		return -ENODEV;
	}

	snd_soc_card_set_drvdata(card, priv);
	ret = devm_snd_soc_register_card(&pdev->dev, card);
	if (ret) {
		dev_err(&pdev->dev, "devm_snd_soc_register_card() failed: %d\n",
			ret);
		return ret;
	}

	return 0;
}

static const struct of_device_id omap_twl4030_of_match[] = {
	{.compatible = "ti,omap-twl4030", },
	{ },
};
MODULE_DEVICE_TABLE(of, omap_twl4030_of_match);

static struct platform_driver omap_twl4030_driver = {
	.driver = {
		.name = "omap-twl4030",
		.pm = &snd_soc_pm_ops,
		.of_match_table = omap_twl4030_of_match,
	},
	.probe = omap_twl4030_probe,
};

module_platform_driver(omap_twl4030_driver);

MODULE_AUTHOR("Peter Ujfalusi <peter.ujfalusi@ti.com>");
MODULE_DESCRIPTION("ALSA SoC for TI SoC based boards with twl4030 codec");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:omap-twl4030");
