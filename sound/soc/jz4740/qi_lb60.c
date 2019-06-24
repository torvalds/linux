// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2009, Lars-Peter Clausen <lars@metafoo.de>
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <linux/gpio/consumer.h>

struct qi_lb60 {
	struct gpio_desc *snd_gpio;
	struct gpio_desc *amp_gpio;
};

static int qi_lb60_spk_event(struct snd_soc_dapm_widget *widget,
			     struct snd_kcontrol *ctrl, int event)
{
	struct qi_lb60 *qi_lb60 = snd_soc_card_get_drvdata(widget->dapm->card);
	int on = !SND_SOC_DAPM_EVENT_OFF(event);

	gpiod_set_value_cansleep(qi_lb60->snd_gpio, on);
	gpiod_set_value_cansleep(qi_lb60->amp_gpio, on);

	return 0;
}

static const struct snd_soc_dapm_widget qi_lb60_widgets[] = {
	SND_SOC_DAPM_SPK("Speaker", qi_lb60_spk_event),
	SND_SOC_DAPM_MIC("Mic", NULL),
};

static const struct snd_soc_dapm_route qi_lb60_routes[] = {
	{"Mic", NULL, "MIC"},
	{"Speaker", NULL, "LOUT"},
	{"Speaker", NULL, "ROUT"},
};

static struct snd_soc_dai_link qi_lb60_dai = {
	.name = "jz4740",
	.stream_name = "jz4740",
	.cpu_dai_name = "jz4740-i2s",
	.platform_name = "jz4740-i2s",
	.codec_dai_name = "jz4740-hifi",
	.codec_name = "jz4740-codec",
	.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
		SND_SOC_DAIFMT_CBM_CFM,
};

static struct snd_soc_card qi_lb60_card = {
	.name = "QI LB60",
	.owner = THIS_MODULE,
	.dai_link = &qi_lb60_dai,
	.num_links = 1,

	.dapm_widgets = qi_lb60_widgets,
	.num_dapm_widgets = ARRAY_SIZE(qi_lb60_widgets),
	.dapm_routes = qi_lb60_routes,
	.num_dapm_routes = ARRAY_SIZE(qi_lb60_routes),
	.fully_routed = true,
};

static int qi_lb60_probe(struct platform_device *pdev)
{
	struct qi_lb60 *qi_lb60;
	struct snd_soc_card *card = &qi_lb60_card;

	qi_lb60 = devm_kzalloc(&pdev->dev, sizeof(*qi_lb60), GFP_KERNEL);
	if (!qi_lb60)
		return -ENOMEM;

	qi_lb60->snd_gpio = devm_gpiod_get(&pdev->dev, "snd", GPIOD_OUT_LOW);
	if (IS_ERR(qi_lb60->snd_gpio))
		return PTR_ERR(qi_lb60->snd_gpio);

	qi_lb60->amp_gpio = devm_gpiod_get(&pdev->dev, "amp", GPIOD_OUT_LOW);
	if (IS_ERR(qi_lb60->amp_gpio))
		return PTR_ERR(qi_lb60->amp_gpio);

	card->dev = &pdev->dev;

	snd_soc_card_set_drvdata(card, qi_lb60);

	return devm_snd_soc_register_card(&pdev->dev, card);
}

static struct platform_driver qi_lb60_driver = {
	.driver		= {
		.name	= "qi-lb60-audio",
	},
	.probe		= qi_lb60_probe,
};

module_platform_driver(qi_lb60_driver);

MODULE_AUTHOR("Lars-Peter Clausen <lars@metafoo.de>");
MODULE_DESCRIPTION("ALSA SoC QI LB60 Audio support");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:qi-lb60-audio");
