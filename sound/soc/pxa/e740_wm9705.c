// SPDX-License-Identifier: GPL-2.0-only
/*
 * e740-wm9705.c  --  SoC audio for e740
 *
 * Copyright 2007 (c) Ian Molton <spyro@f2s.com>
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/gpio/consumer.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>

#include <linux/platform_data/asoc-pxa.h>

#include <asm/mach-types.h>

static struct gpio_desc *gpiod_output_amp, *gpiod_input_amp;
static struct gpio_desc *gpiod_audio_power;

#define E740_AUDIO_OUT 1
#define E740_AUDIO_IN  2

static int e740_audio_power;

static void e740_sync_audio_power(int status)
{
	gpiod_set_value(gpiod_audio_power, !status);
	gpiod_set_value(gpiod_output_amp, (status & E740_AUDIO_OUT) ? 1 : 0);
	gpiod_set_value(gpiod_input_amp, (status & E740_AUDIO_IN) ? 1 : 0);
}

static int e740_mic_amp_event(struct snd_soc_dapm_widget *w,
				struct snd_kcontrol *kcontrol, int event)
{
	if (event & SND_SOC_DAPM_PRE_PMU)
		e740_audio_power |= E740_AUDIO_IN;
	else if (event & SND_SOC_DAPM_POST_PMD)
		e740_audio_power &= ~E740_AUDIO_IN;

	e740_sync_audio_power(e740_audio_power);

	return 0;
}

static int e740_output_amp_event(struct snd_soc_dapm_widget *w,
				struct snd_kcontrol *kcontrol, int event)
{
	if (event & SND_SOC_DAPM_PRE_PMU)
		e740_audio_power |= E740_AUDIO_OUT;
	else if (event & SND_SOC_DAPM_POST_PMD)
		e740_audio_power &= ~E740_AUDIO_OUT;

	e740_sync_audio_power(e740_audio_power);

	return 0;
}

static const struct snd_soc_dapm_widget e740_dapm_widgets[] = {
	SND_SOC_DAPM_HP("Headphone Jack", NULL),
	SND_SOC_DAPM_SPK("Speaker", NULL),
	SND_SOC_DAPM_MIC("Mic (Internal)", NULL),
	SND_SOC_DAPM_PGA_E("Output Amp", SND_SOC_NOPM, 0, 0, NULL, 0,
			e740_output_amp_event, SND_SOC_DAPM_PRE_PMU |
			SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_PGA_E("Mic Amp", SND_SOC_NOPM, 0, 0, NULL, 0,
			e740_mic_amp_event, SND_SOC_DAPM_PRE_PMU |
			SND_SOC_DAPM_POST_PMD),
};

static const struct snd_soc_dapm_route audio_map[] = {
	{"Output Amp", NULL, "LOUT"},
	{"Output Amp", NULL, "ROUT"},
	{"Output Amp", NULL, "MONOOUT"},

	{"Speaker", NULL, "Output Amp"},
	{"Headphone Jack", NULL, "Output Amp"},

	{"MIC1", NULL, "Mic Amp"},
	{"Mic Amp", NULL, "Mic (Internal)"},
};

SND_SOC_DAILINK_DEFS(ac97,
	DAILINK_COMP_ARRAY(COMP_CPU("pxa2xx-ac97")),
	DAILINK_COMP_ARRAY(COMP_CODEC("wm9705-codec", "wm9705-hifi")),
	DAILINK_COMP_ARRAY(COMP_PLATFORM("pxa-pcm-audio")));

SND_SOC_DAILINK_DEFS(ac97_aux,
	DAILINK_COMP_ARRAY(COMP_CPU("pxa2xx-ac97-aux")),
	DAILINK_COMP_ARRAY(COMP_CODEC("wm9705-codec", "wm9705-aux")),
	DAILINK_COMP_ARRAY(COMP_PLATFORM("pxa-pcm-audio")));

static struct snd_soc_dai_link e740_dai[] = {
	{
		.name = "AC97",
		.stream_name = "AC97 HiFi",
		SND_SOC_DAILINK_REG(ac97),
	},
	{
		.name = "AC97 Aux",
		.stream_name = "AC97 Aux",
		SND_SOC_DAILINK_REG(ac97_aux),
	},
};

static struct snd_soc_card e740 = {
	.name = "Toshiba e740",
	.owner = THIS_MODULE,
	.dai_link = e740_dai,
	.num_links = ARRAY_SIZE(e740_dai),

	.dapm_widgets = e740_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(e740_dapm_widgets),
	.dapm_routes = audio_map,
	.num_dapm_routes = ARRAY_SIZE(audio_map),
	.fully_routed = true,
};

static int e740_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card = &e740;
	int ret;

	gpiod_input_amp  = devm_gpiod_get(&pdev->dev, "Mic amp", GPIOD_OUT_LOW);
	ret = PTR_ERR_OR_ZERO(gpiod_input_amp);
	if (ret)
		return ret;
	gpiod_output_amp  = devm_gpiod_get(&pdev->dev, "Output amp", GPIOD_OUT_LOW);
	ret = PTR_ERR_OR_ZERO(gpiod_output_amp);
	if (ret)
		return ret;
	gpiod_audio_power = devm_gpiod_get(&pdev->dev, "Audio power", GPIOD_OUT_HIGH);
	ret = PTR_ERR_OR_ZERO(gpiod_audio_power);
	if (ret)
		return ret;

	card->dev = &pdev->dev;

	ret = devm_snd_soc_register_card(&pdev->dev, card);
	if (ret)
		dev_err(&pdev->dev, "snd_soc_register_card() failed: %d\n",
			ret);
	return ret;
}

static int e740_remove(struct platform_device *pdev)
{
	return 0;
}

static struct platform_driver e740_driver = {
	.driver		= {
		.name	= "e740-audio",
		.pm     = &snd_soc_pm_ops,
	},
	.probe		= e740_probe,
	.remove		= e740_remove,
};

module_platform_driver(e740_driver);

/* Module information */
MODULE_AUTHOR("Ian Molton <spyro@f2s.com>");
MODULE_DESCRIPTION("ALSA SoC driver for e740");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:e740-audio");
