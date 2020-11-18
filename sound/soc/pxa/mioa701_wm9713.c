// SPDX-License-Identifier: GPL-2.0-only
/*
 * Handles the Mitac mioa701 SoC system
 *
 * Copyright (C) 2008 Robert Jarzmik
 *
 * This is a little schema of the sound interconnections :
 *
 *    Sagem X200                 Wolfson WM9713
 *    +--------+             +-------------------+      Rear Speaker
 *    |        |             |                   |           /-+
 *    |        +--->----->---+MONOIN         SPKL+--->----+-+  |
 *    |  GSM   |             |                   |        | |  |
 *    |        +--->----->---+PCBEEP         SPKR+--->----+-+  |
 *    |  CHIP  |             |                   |           \-+
 *    |        +---<-----<---+MONO               |
 *    |        |             |                   |      Front Speaker
 *    +--------+             |                   |           /-+
 *                           |                HPL+--->----+-+  |
 *                           |                   |        | |  |
 *                           |               OUT3+--->----+-+  |
 *                           |                   |           \-+
 *                           |                   |
 *                           |                   |     Front Micro
 *                           |                   |         +
 *                           |               MIC1+-----<--+o+
 *                           |                   |         +
 *                           +-------------------+        ---
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/platform_device.h>

#include <asm/mach-types.h>
#include <mach/audio.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <sound/initval.h>
#include <sound/ac97_codec.h>

#include "../codecs/wm9713.h"

#define AC97_GPIO_PULL		0x58

/* Use GPIO8 for rear speaker amplifier */
static int rear_amp_power(struct snd_soc_component *component, int power)
{
	unsigned short reg;

	if (power) {
		reg = snd_soc_component_read(component, AC97_GPIO_CFG);
		snd_soc_component_write(component, AC97_GPIO_CFG, reg | 0x0100);
		reg = snd_soc_component_read(component, AC97_GPIO_PULL);
		snd_soc_component_write(component, AC97_GPIO_PULL, reg | (1<<15));
	} else {
		reg = snd_soc_component_read(component, AC97_GPIO_CFG);
		snd_soc_component_write(component, AC97_GPIO_CFG, reg & ~0x0100);
		reg = snd_soc_component_read(component, AC97_GPIO_PULL);
		snd_soc_component_write(component, AC97_GPIO_PULL, reg & ~(1<<15));
	}

	return 0;
}

static int rear_amp_event(struct snd_soc_dapm_widget *widget,
			  struct snd_kcontrol *kctl, int event)
{
	struct snd_soc_card *card = widget->dapm->card;
	struct snd_soc_pcm_runtime *rtd;
	struct snd_soc_component *component;

	rtd = snd_soc_get_pcm_runtime(card, &card->dai_link[0]);
	component = asoc_rtd_to_codec(rtd, 0)->component;
	return rear_amp_power(component, SND_SOC_DAPM_EVENT_ON(event));
}

/* mioa701 machine dapm widgets */
static const struct snd_soc_dapm_widget mioa701_dapm_widgets[] = {
	SND_SOC_DAPM_SPK("Front Speaker", NULL),
	SND_SOC_DAPM_SPK("Rear Speaker", rear_amp_event),
	SND_SOC_DAPM_MIC("Headset", NULL),
	SND_SOC_DAPM_LINE("GSM Line Out", NULL),
	SND_SOC_DAPM_LINE("GSM Line In", NULL),
	SND_SOC_DAPM_MIC("Headset Mic", NULL),
	SND_SOC_DAPM_MIC("Front Mic", NULL),
};

static const struct snd_soc_dapm_route audio_map[] = {
	/* Call Mic */
	{"Mic Bias", NULL, "Front Mic"},
	{"MIC1", NULL, "Mic Bias"},

	/* Headset Mic */
	{"LINEL", NULL, "Headset Mic"},
	{"LINER", NULL, "Headset Mic"},

	/* GSM Module */
	{"MONOIN", NULL, "GSM Line Out"},
	{"PCBEEP", NULL, "GSM Line Out"},
	{"GSM Line In", NULL, "MONO"},

	/* headphone connected to HPL, HPR */
	{"Headset", NULL, "HPL"},
	{"Headset", NULL, "HPR"},

	/* front speaker connected to HPL, OUT3 */
	{"Front Speaker", NULL, "HPL"},
	{"Front Speaker", NULL, "OUT3"},

	/* rear speaker connected to SPKL, SPKR */
	{"Rear Speaker", NULL, "SPKL"},
	{"Rear Speaker", NULL, "SPKR"},
};

static int mioa701_wm9713_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_component *component = asoc_rtd_to_codec(rtd, 0)->component;

	/* Prepare GPIO8 for rear speaker amplifier */
	snd_soc_component_update_bits(component, AC97_GPIO_CFG, 0x100, 0x100);

	/* Prepare MIC input */
	snd_soc_component_update_bits(component, AC97_3D_CONTROL, 0xc000, 0xc000);

	return 0;
}

static struct snd_soc_ops mioa701_ops;

SND_SOC_DAILINK_DEFS(ac97,
	DAILINK_COMP_ARRAY(COMP_CPU("pxa2xx-ac97")),
	DAILINK_COMP_ARRAY(COMP_CODEC("wm9713-codec", "wm9713-hifi")),
	DAILINK_COMP_ARRAY(COMP_PLATFORM("pxa-pcm-audio")));

SND_SOC_DAILINK_DEFS(ac97_aux,
	DAILINK_COMP_ARRAY(COMP_CPU("pxa2xx-ac97-aux")),
	DAILINK_COMP_ARRAY(COMP_CODEC("wm9713-codec", "wm9713-aux")),
	DAILINK_COMP_ARRAY(COMP_PLATFORM("pxa-pcm-audio")));

static struct snd_soc_dai_link mioa701_dai[] = {
	{
		.name = "AC97",
		.stream_name = "AC97 HiFi",
		.init = mioa701_wm9713_init,
		.ops = &mioa701_ops,
		SND_SOC_DAILINK_REG(ac97),
	},
	{
		.name = "AC97 Aux",
		.stream_name = "AC97 Aux",
		.ops = &mioa701_ops,
		SND_SOC_DAILINK_REG(ac97_aux),
	},
};

static struct snd_soc_card mioa701 = {
	.name = "MioA701",
	.owner = THIS_MODULE,
	.dai_link = mioa701_dai,
	.num_links = ARRAY_SIZE(mioa701_dai),

	.dapm_widgets = mioa701_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(mioa701_dapm_widgets),
	.dapm_routes = audio_map,
	.num_dapm_routes = ARRAY_SIZE(audio_map),
};

static int mioa701_wm9713_probe(struct platform_device *pdev)
{
	int rc;

	if (!machine_is_mioa701())
		return -ENODEV;

	mioa701.dev = &pdev->dev;
	rc = devm_snd_soc_register_card(&pdev->dev, &mioa701);
	if (!rc)
		dev_warn(&pdev->dev, "Be warned that incorrect mixers/muxes setup will "
			 "lead to overheating and possible destruction of your device."
			 " Do not use without a good knowledge of mio's board design!\n");
	return rc;
}

static struct platform_driver mioa701_wm9713_driver = {
	.probe		= mioa701_wm9713_probe,
	.driver		= {
		.name		= "mioa701-wm9713",
		.pm     = &snd_soc_pm_ops,
	},
};

module_platform_driver(mioa701_wm9713_driver);

/* Module information */
MODULE_AUTHOR("Robert Jarzmik (rjarzmik@free.fr)");
MODULE_DESCRIPTION("ALSA SoC WM9713 MIO A701");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:mioa701-wm9713");
