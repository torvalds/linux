/*
 * Copyright (C) 2009, Lars-Peter Clausen <lars@metafoo.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>
#include <linux/gpio.h>

#define QI_LB60_SND_GPIO JZ_GPIO_PORTB(29)
#define QI_LB60_AMP_GPIO JZ_GPIO_PORTD(4)

static int qi_lb60_spk_event(struct snd_soc_dapm_widget *widget,
			     struct snd_kcontrol *ctrl, int event)
{
	int on = !SND_SOC_DAPM_EVENT_OFF(event);

	gpio_set_value(QI_LB60_SND_GPIO, on);
	gpio_set_value(QI_LB60_AMP_GPIO, on);

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

#define QI_LB60_DAIFMT (SND_SOC_DAIFMT_I2S | \
			SND_SOC_DAIFMT_NB_NF | \
			SND_SOC_DAIFMT_CBM_CFM)

static int qi_lb60_codec_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_codec *codec = rtd->codec;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct snd_soc_dapm_context *dapm = &codec->dapm;
	int ret;

	snd_soc_dapm_nc_pin(dapm, "LIN");
	snd_soc_dapm_nc_pin(dapm, "RIN");

	ret = snd_soc_dai_set_fmt(cpu_dai, QI_LB60_DAIFMT);
	if (ret < 0) {
		dev_err(codec->dev, "Failed to set cpu dai format: %d\n", ret);
		return ret;
	}

	return 0;
}

static struct snd_soc_dai_link qi_lb60_dai = {
	.name = "jz4740",
	.stream_name = "jz4740",
	.cpu_dai_name = "jz4740-i2s",
	.platform_name = "jz4740-pcm-audio",
	.codec_dai_name = "jz4740-hifi",
	.codec_name = "jz4740-codec",
	.init = qi_lb60_codec_init,
};

static struct snd_soc_card qi_lb60 = {
	.name = "QI LB60",
	.owner = THIS_MODULE,
	.dai_link = &qi_lb60_dai,
	.num_links = 1,

	.dapm_widgets = qi_lb60_widgets,
	.num_dapm_widgets = ARRAY_SIZE(qi_lb60_widgets),
	.dapm_routes = qi_lb60_routes,
	.num_dapm_routes = ARRAY_SIZE(qi_lb60_routes),
};

static const struct gpio qi_lb60_gpios[] = {
	{ QI_LB60_SND_GPIO, GPIOF_OUT_INIT_LOW, "SND" },
	{ QI_LB60_AMP_GPIO, GPIOF_OUT_INIT_LOW, "AMP" },
};

static int qi_lb60_probe(struct platform_device *pdev)
{
	struct snd_soc_card *card = &qi_lb60;
	int ret;

	ret = gpio_request_array(qi_lb60_gpios, ARRAY_SIZE(qi_lb60_gpios));
	if (ret)
		return ret;

	card->dev = &pdev->dev;

	ret = snd_soc_register_card(card);
	if (ret) {
		dev_err(&pdev->dev, "snd_soc_register_card() failed: %d\n",
			ret);
		gpio_free_array(qi_lb60_gpios, ARRAY_SIZE(qi_lb60_gpios));
	}
	return ret;
}

static int qi_lb60_remove(struct platform_device *pdev)
{
	struct snd_soc_card *card = platform_get_drvdata(pdev);

	snd_soc_unregister_card(card);
	gpio_free_array(qi_lb60_gpios, ARRAY_SIZE(qi_lb60_gpios));
	return 0;
}

static struct platform_driver qi_lb60_driver = {
	.driver		= {
		.name	= "qi-lb60-audio",
		.owner	= THIS_MODULE,
	},
	.probe		= qi_lb60_probe,
	.remove		= qi_lb60_remove,
};

module_platform_driver(qi_lb60_driver);

MODULE_AUTHOR("Lars-Peter Clausen <lars@metafoo.de>");
MODULE_DESCRIPTION("ALSA SoC QI LB60 Audio support");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:qi-lb60-audio");
