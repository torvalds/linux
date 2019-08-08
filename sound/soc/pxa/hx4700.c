// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * SoC audio for HP iPAQ hx4700
 *
 * Copyright (c) 2009 Philipp Zabel
 */

#include <linux/module.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/delay.h>
#include <linux/gpio.h>

#include <sound/core.h>
#include <sound/jack.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>

#include <mach/hx4700.h>
#include <asm/mach-types.h>
#include "pxa2xx-i2s.h"

static struct snd_soc_jack hs_jack;

/* Headphones jack detection DAPM pin */
static struct snd_soc_jack_pin hs_jack_pin[] = {
	{
		.pin	= "Headphone Jack",
		.mask	= SND_JACK_HEADPHONE,
	},
	{
		.pin	= "Speaker",
		/* disable speaker when hp jack is inserted */
		.mask   = SND_JACK_HEADPHONE,
		.invert	= 1,
	},
};

/* Headphones jack detection GPIO */
static struct snd_soc_jack_gpio hs_jack_gpio = {
	.gpio		= GPIO75_HX4700_EARPHONE_nDET,
	.invert		= true,
	.name		= "hp-gpio",
	.report		= SND_JACK_HEADPHONE,
	.debounce_time	= 200,
};

/*
 * iPAQ hx4700 uses I2S for capture and playback.
 */
static int hx4700_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *codec_dai = rtd->codec_dai;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	int ret = 0;

	/* set the I2S system clock as output */
	ret = snd_soc_dai_set_sysclk(cpu_dai, PXA2XX_I2S_SYSCLK, 0,
			SND_SOC_CLOCK_OUT);
	if (ret < 0)
		return ret;

	/* inform codec driver about clock freq *
	 * (PXA I2S always uses divider 256)    */
	ret = snd_soc_dai_set_sysclk(codec_dai, 0, 256 * params_rate(params),
			SND_SOC_CLOCK_IN);
	if (ret < 0)
		return ret;

	return 0;
}

static const struct snd_soc_ops hx4700_ops = {
	.hw_params = hx4700_hw_params,
};

static int hx4700_spk_power(struct snd_soc_dapm_widget *w,
			    struct snd_kcontrol *k, int event)
{
	gpio_set_value(GPIO107_HX4700_SPK_nSD, !!SND_SOC_DAPM_EVENT_ON(event));
	return 0;
}

static int hx4700_hp_power(struct snd_soc_dapm_widget *w,
			   struct snd_kcontrol *k, int event)
{
	gpio_set_value(GPIO92_HX4700_HP_DRIVER, !!SND_SOC_DAPM_EVENT_ON(event));
	return 0;
}

/* hx4700 machine dapm widgets */
static const struct snd_soc_dapm_widget hx4700_dapm_widgets[] = {
	SND_SOC_DAPM_HP("Headphone Jack", hx4700_hp_power),
	SND_SOC_DAPM_SPK("Speaker", hx4700_spk_power),
	SND_SOC_DAPM_MIC("Built-in Microphone", NULL),
};

/* hx4700 machine audio_map */
static const struct snd_soc_dapm_route hx4700_audio_map[] = {

	/* Headphone connected to LOUT, ROUT */
	{"Headphone Jack", NULL, "LOUT"},
	{"Headphone Jack", NULL, "ROUT"},

	/* Speaker connected to MOUT2 */
	{"Speaker", NULL, "MOUT2"},

	/* Microphone connected to MICIN */
	{"MICIN", NULL, "Built-in Microphone"},
	{"AIN", NULL, "MICOUT"},
};

/*
 * Logic for a ak4641 as connected on a HP iPAQ hx4700
 */
static int hx4700_ak4641_init(struct snd_soc_pcm_runtime *rtd)
{
	int err;

	/* Jack detection API stuff */
	err = snd_soc_card_jack_new(rtd->card, "Headphone Jack",
				    SND_JACK_HEADPHONE, &hs_jack, hs_jack_pin,
				    ARRAY_SIZE(hs_jack_pin));
	if (err)
		return err;

	err = snd_soc_jack_add_gpios(&hs_jack, 1, &hs_jack_gpio);

	return err;
}

/* hx4700 digital audio interface glue - connects codec <--> CPU */
static struct snd_soc_dai_link hx4700_dai = {
	.name = "ak4641",
	.stream_name = "AK4641",
	.cpu_dai_name = "pxa2xx-i2s",
	.codec_dai_name = "ak4641-hifi",
	.platform_name = "pxa-pcm-audio",
	.codec_name = "ak4641.0-0012",
	.init = hx4700_ak4641_init,
	.dai_fmt = SND_SOC_DAIFMT_MSB | SND_SOC_DAIFMT_NB_NF |
		   SND_SOC_DAIFMT_CBS_CFS,
	.ops = &hx4700_ops,
};

/* hx4700 audio machine driver */
static struct snd_soc_card snd_soc_card_hx4700 = {
	.name			= "iPAQ hx4700",
	.owner			= THIS_MODULE,
	.dai_link		= &hx4700_dai,
	.num_links		= 1,
	.dapm_widgets		= hx4700_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(hx4700_dapm_widgets),
	.dapm_routes		= hx4700_audio_map,
	.num_dapm_routes	= ARRAY_SIZE(hx4700_audio_map),
	.fully_routed		= true,
};

static struct gpio hx4700_audio_gpios[] = {
	{ GPIO107_HX4700_SPK_nSD, GPIOF_OUT_INIT_HIGH, "SPK_POWER" },
	{ GPIO92_HX4700_HP_DRIVER, GPIOF_OUT_INIT_LOW, "EP_POWER" },
};

static int hx4700_audio_probe(struct platform_device *pdev)
{
	int ret;

	if (!machine_is_h4700())
		return -ENODEV;

	ret = gpio_request_array(hx4700_audio_gpios,
				ARRAY_SIZE(hx4700_audio_gpios));
	if (ret)
		return ret;

	snd_soc_card_hx4700.dev = &pdev->dev;
	ret = devm_snd_soc_register_card(&pdev->dev, &snd_soc_card_hx4700);
	if (ret)
		gpio_free_array(hx4700_audio_gpios,
				ARRAY_SIZE(hx4700_audio_gpios));

	return ret;
}

static int hx4700_audio_remove(struct platform_device *pdev)
{
	gpio_set_value(GPIO92_HX4700_HP_DRIVER, 0);
	gpio_set_value(GPIO107_HX4700_SPK_nSD, 0);

	gpio_free_array(hx4700_audio_gpios, ARRAY_SIZE(hx4700_audio_gpios));
	return 0;
}

static struct platform_driver hx4700_audio_driver = {
	.driver	= {
		.name = "hx4700-audio",
		.pm = &snd_soc_pm_ops,
	},
	.probe	= hx4700_audio_probe,
	.remove	= hx4700_audio_remove,
};

module_platform_driver(hx4700_audio_driver);

MODULE_AUTHOR("Philipp Zabel");
MODULE_DESCRIPTION("ALSA SoC iPAQ hx4700");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:hx4700-audio");
