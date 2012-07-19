/*
 * linux/sound/soc/pxa/ttc_dkb.c
 *
 * Copyright (C) 2012 Marvell International Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
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
	struct snd_soc_codec *codec = rtd->codec;
	struct snd_soc_dapm_context *dapm = &codec->dapm;

	/* connected pins */
	snd_soc_dapm_enable_pin(dapm, "Ext Speaker");
	snd_soc_dapm_enable_pin(dapm, "Ext Mic 1");
	snd_soc_dapm_enable_pin(dapm, "Ext Mic 3");
	snd_soc_dapm_disable_pin(dapm, "Headset Mic 2");
	snd_soc_dapm_disable_pin(dapm, "Headset Stereophone");

	/* Headset jack detection */
	snd_soc_jack_new(codec, "Headphone Jack", SND_JACK_HEADPHONE
			| SND_JACK_BTN_0 | SND_JACK_BTN_1 | SND_JACK_BTN_2,
			&hs_jack);
	snd_soc_jack_add_pins(&hs_jack, ARRAY_SIZE(hs_jack_pins),
			      hs_jack_pins);
	snd_soc_jack_new(codec, "Microphone Jack", SND_JACK_MICROPHONE,
			 &mic_jack);
	snd_soc_jack_add_pins(&mic_jack, ARRAY_SIZE(mic_jack_pins),
			      mic_jack_pins);

	/* headphone, microphone detection & headset short detection */
	pm860x_hs_jack_detect(codec, &hs_jack, SND_JACK_HEADPHONE,
			      SND_JACK_BTN_0, SND_JACK_BTN_1, SND_JACK_BTN_2);
	pm860x_mic_jack_detect(codec, &hs_jack, SND_JACK_MICROPHONE);

	return 0;
}

/* ttc/td-dkb digital audio interface glue - connects codec <--> CPU */
static struct snd_soc_dai_link ttc_pm860x_hifi_dai[] = {
{
	 .name = "88pm860x i2s",
	 .stream_name = "audio playback",
	 .codec_name = "88pm860x-codec",
	 .platform_name = "mmp-pcm-audio",
	 .cpu_dai_name = "pxa-ssp-dai.1",
	 .codec_dai_name = "88pm860x-i2s",
	 .dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
			SND_SOC_DAIFMT_CBM_CFM,
	 .init = ttc_pm860x_init,
},
};

/* ttc/td audio machine driver */
static struct snd_soc_card ttc_dkb_card = {
	.name = "ttc-dkb-hifi",
	.dai_link = ttc_pm860x_hifi_dai,
	.num_links = ARRAY_SIZE(ttc_pm860x_hifi_dai),

	.dapm_widgets = ttc_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(ttc_dapm_widgets),
	.dapm_routes = ttc_audio_map,
	.num_dapm_routes = ARRAY_SIZE(ttc_audio_map),
};

static int __devinit ttc_dkb_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card = &ttc_dkb_card;
	int ret;

	card->dev = &pdev->dev;

	ret = snd_soc_register_card(card);
	if (ret)
		dev_err(&pdev->dev, "snd_soc_register_card() failed: %d\n",
			ret);

	return ret;
}

static int __devexit ttc_dkb_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);

	snd_soc_unregister_card(card);

	return 0;
}

static struct platform_driver ttc_dkb_driver = {
	.driver		= {
		.name	= "ttc-dkb-audio",
		.owner	= THIS_MODULE,
	},
	.probe		= ttc_dkb_probe,
	.remove		= __devexit_p(ttc_dkb_remove),
};

module_platform_driver(ttc_dkb_driver);

/* Module information */
MODULE_AUTHOR("Qiao Zhou, <zhouqiao@marvell.com>");
MODULE_DESCRIPTION("ALSA SoC TTC DKB");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:ttc-dkb-audio");
