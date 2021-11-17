// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * linux/sound/soc/pxa/ttc_dkb.c
 *
 * Copyright (C) 2012 Marvell International Ltd.
 */
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/jack.h>
#include <asm/mach-types.h>
#include <sound/pcm_params.h>
#include "../codecs/88pm860x-codec.h"

static struct snd_soc_jack hs_jack, mic_jack;

static struct snd_soc_jack_pin hs_jack_pins[] = {
	{ .pin = "Headset Stereophone",	.mask = SND_JACK_HEADPHONE, },
};

static struct snd_soc_jack_pin mic_jack_pins[] = {
	{ .pin = "Headset Mic 2",	.mask = SND_JACK_MICROPHONE, },
};

/* ttc machine dapm widgets */
static const struct snd_soc_dapm_widget ttc_dapm_widgets[] = {
	SND_SOC_DAPM_HP("Headset Stereophone", NULL),
	SND_SOC_DAPM_LINE("Lineout Out 1", NULL),
	SND_SOC_DAPM_LINE("Lineout Out 2", NULL),
	SND_SOC_DAPM_SPK("Ext Speaker", NULL),
	SND_SOC_DAPM_MIC("Ext Mic 1", NULL),
	SND_SOC_DAPM_MIC("Headset Mic 2", NULL),
	SND_SOC_DAPM_MIC("Ext Mic 3", NULL),
};

/* ttc machine audio map */
static const struct snd_soc_dapm_route ttc_audio_map[] = {
	{"Headset Stereophone", NULL, "HS1"},
	{"Headset Stereophone", NULL, "HS2"},

	{"Ext Speaker", NULL, "LSP"},
	{"Ext Speaker", NULL, "LSN"},

	{"Lineout Out 1", NULL, "LINEOUT1"},
	{"Lineout Out 2", NULL, "LINEOUT2"},

	{"MIC1P", NULL, "Mic1 Bias"},
	{"MIC1N", NULL, "Mic1 Bias"},
	{"Mic1 Bias", NULL, "Ext Mic 1"},

	{"MIC2P", NULL, "Mic1 Bias"},
	{"MIC2N", NULL, "Mic1 Bias"},
	{"Mic1 Bias", NULL, "Headset Mic 2"},

	{"MIC3P", NULL, "Mic3 Bias"},
	{"MIC3N", NULL, "Mic3 Bias"},
	{"Mic3 Bias", NULL, "Ext Mic 3"},
};

static int ttc_pm860x_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_component *component = asoc_rtd_to_codec(rtd, 0)->component;

	/* Headset jack detection */
	snd_soc_card_jack_new(rtd->card, "Headphone Jack", SND_JACK_HEADPHONE |
			      SND_JACK_BTN_0 | SND_JACK_BTN_1 | SND_JACK_BTN_2,
			      &hs_jack, hs_jack_pins, ARRAY_SIZE(hs_jack_pins));
	snd_soc_card_jack_new(rtd->card, "Microphone Jack", SND_JACK_MICROPHONE,
			      &mic_jack, mic_jack_pins,
			      ARRAY_SIZE(mic_jack_pins));

	/* headphone, microphone detection & headset short detection */
	pm860x_hs_jack_detect(component, &hs_jack, SND_JACK_HEADPHONE,
			      SND_JACK_BTN_0, SND_JACK_BTN_1, SND_JACK_BTN_2);
	pm860x_mic_jack_detect(component, &hs_jack, SND_JACK_MICROPHONE);

	return 0;
}

/* ttc/td-dkb digital audio interface glue - connects codec <--> CPU */
SND_SOC_DAILINK_DEFS(i2s,
	DAILINK_COMP_ARRAY(COMP_CPU("pxa-ssp-dai.1")),
	DAILINK_COMP_ARRAY(COMP_CODEC("88pm860x-codec", "88pm860x-i2s")),
	DAILINK_COMP_ARRAY(COMP_PLATFORM("mmp-pcm-audio")));

static struct snd_soc_dai_link ttc_pm860x_hifi_dai[] = {
{
	 .name = "88pm860x i2s",
	 .stream_name = "audio playback",
	 .dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
			SND_SOC_DAIFMT_CBM_CFM,
	 .init = ttc_pm860x_init,
	 SND_SOC_DAILINK_REG(i2s),
},
};

/* ttc/td audio machine driver */
static struct snd_soc_card ttc_dkb_card = {
	.name = "ttc-dkb-hifi",
	.owner = THIS_MODULE,
	.dai_link = ttc_pm860x_hifi_dai,
	.num_links = ARRAY_SIZE(ttc_pm860x_hifi_dai),

	.dapm_widgets = ttc_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(ttc_dapm_widgets),
	.dapm_routes = ttc_audio_map,
	.num_dapm_routes = ARRAY_SIZE(ttc_audio_map),
};

static int ttc_dkb_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card = &ttc_dkb_card;
	int ret;

	card->dev = &pdev->dev;

	ret = devm_snd_soc_register_card(&pdev->dev, card);
	if (ret)
		dev_err(&pdev->dev, "snd_soc_register_card() failed: %d\n",
			ret);

	return ret;
}

static struct platform_driver ttc_dkb_driver = {
	.driver		= {
		.name	= "ttc-dkb-audio",
		.pm     = &snd_soc_pm_ops,
	},
	.probe		= ttc_dkb_probe,
};

module_platform_driver(ttc_dkb_driver);

/* Module information */
MODULE_AUTHOR("Qiao Zhou, <zhouqiao@marvell.com>");
MODULE_DESCRIPTION("ALSA SoC TTC DKB");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:ttc-dkb-audio");
