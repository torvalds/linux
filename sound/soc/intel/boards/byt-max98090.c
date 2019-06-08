// SPDX-License-Identifier: GPL-2.0-only
/*
 * Intel Baytrail SST MAX98090 machine driver
 * Copyright (c) 2014, Intel Corporation.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/acpi.h>
#include <linux/device.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/slab.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/jack.h>
#include "../../codecs/max98090.h"

struct byt_max98090_private {
	struct snd_soc_jack jack;
};

static const struct snd_soc_dapm_widget byt_max98090_widgets[] = {
	SND_SOC_DAPM_HP("Headphone", NULL),
	SND_SOC_DAPM_MIC("Headset Mic", NULL),
	SND_SOC_DAPM_MIC("Int Mic", NULL),
	SND_SOC_DAPM_SPK("Ext Spk", NULL),
};

static const struct snd_soc_dapm_route byt_max98090_audio_map[] = {
	{"IN34", NULL, "Headset Mic"},
	{"Headset Mic", NULL, "MICBIAS"},
	{"DMICL", NULL, "Int Mic"},
	{"Headphone", NULL, "HPL"},
	{"Headphone", NULL, "HPR"},
	{"Ext Spk", NULL, "SPKL"},
	{"Ext Spk", NULL, "SPKR"},
};

static const struct snd_kcontrol_new byt_max98090_controls[] = {
	SOC_DAPM_PIN_SWITCH("Headphone"),
	SOC_DAPM_PIN_SWITCH("Headset Mic"),
	SOC_DAPM_PIN_SWITCH("Int Mic"),
	SOC_DAPM_PIN_SWITCH("Ext Spk"),
};

static struct snd_soc_jack_pin hs_jack_pins[] = {
	{
		.pin	= "Headphone",
		.mask	= SND_JACK_HEADPHONE,
	},
	{
		.pin	= "Headset Mic",
		.mask	= SND_JACK_MICROPHONE,
	},
};

static struct snd_soc_jack_gpio hs_jack_gpios[] = {
	{
		.name		= "hp",
		.report		= SND_JACK_HEADPHONE | SND_JACK_LINEOUT,
		.debounce_time	= 200,
	},
	{
		.name		= "mic",
		.invert		= 1,
		.report		= SND_JACK_MICROPHONE,
		.debounce_time	= 200,
	},
};

static const struct acpi_gpio_params hp_gpios = { 0, 0, false };
static const struct acpi_gpio_params mic_gpios = { 1, 0, false };

static const struct acpi_gpio_mapping acpi_byt_max98090_gpios[] = {
	{ "hp-gpios", &hp_gpios, 1 },
	{ "mic-gpios", &mic_gpios, 1 },
	{},
};

static int byt_max98090_init(struct snd_soc_pcm_runtime *runtime)
{
	int ret;
	struct snd_soc_card *card = runtime->card;
	struct byt_max98090_private *drv = snd_soc_card_get_drvdata(card);
	struct snd_soc_jack *jack = &drv->jack;

	card->dapm.idle_bias_off = true;

	ret = snd_soc_dai_set_sysclk(runtime->codec_dai,
				     M98090_REG_SYSTEM_CLOCK,
				     25000000, SND_SOC_CLOCK_IN);
	if (ret < 0) {
		dev_err(card->dev, "Can't set codec clock %d\n", ret);
		return ret;
	}

	/* Enable jack detection */
	ret = snd_soc_card_jack_new(runtime->card, "Headset",
				    SND_JACK_LINEOUT | SND_JACK_HEADSET, jack,
				    hs_jack_pins, ARRAY_SIZE(hs_jack_pins));
	if (ret)
		return ret;

	return snd_soc_jack_add_gpiods(card->dev->parent, jack,
				       ARRAY_SIZE(hs_jack_gpios),
				       hs_jack_gpios);
}

static struct snd_soc_dai_link byt_max98090_dais[] = {
	{
		.name = "Baytrail Audio",
		.stream_name = "Audio",
		.cpu_dai_name = "baytrail-pcm-audio",
		.codec_dai_name = "HiFi",
		.codec_name = "i2c-193C9890:00",
		.platform_name = "baytrail-pcm-audio",
		.init = byt_max98090_init,
		.dai_fmt = SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_NB_NF |
			   SND_SOC_DAIFMT_CBS_CFS,
	},
};

static struct snd_soc_card byt_max98090_card = {
	.name = "byt-max98090",
	.owner = THIS_MODULE,
	.dai_link = byt_max98090_dais,
	.num_links = ARRAY_SIZE(byt_max98090_dais),
	.dapm_widgets = byt_max98090_widgets,
	.num_dapm_widgets = ARRAY_SIZE(byt_max98090_widgets),
	.dapm_routes = byt_max98090_audio_map,
	.num_dapm_routes = ARRAY_SIZE(byt_max98090_audio_map),
	.controls = byt_max98090_controls,
	.num_controls = ARRAY_SIZE(byt_max98090_controls),
	.fully_routed = true,
};

static int byt_max98090_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct byt_max98090_private *priv;
	int ret_val;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		dev_err(&pdev->dev, "allocation failed\n");
		return -ENOMEM;
	}

	ret_val = devm_acpi_dev_add_driver_gpios(dev->parent, acpi_byt_max98090_gpios);
	if (ret_val)
		dev_dbg(dev, "Unable to add GPIO mapping table\n");

	byt_max98090_card.dev = &pdev->dev;
	snd_soc_card_set_drvdata(&byt_max98090_card, priv);
	ret_val = devm_snd_soc_register_card(&pdev->dev, &byt_max98090_card);
	if (ret_val) {
		dev_err(&pdev->dev,
			"snd_soc_register_card failed %d\n", ret_val);
		return ret_val;
	}

	return 0;
}

static struct platform_driver byt_max98090_driver = {
	.probe = byt_max98090_probe,
	.driver = {
		.name = "byt-max98090",
		.pm = &snd_soc_pm_ops,
	},
};
module_platform_driver(byt_max98090_driver)

MODULE_DESCRIPTION("ASoC Intel(R) Baytrail Machine driver");
MODULE_AUTHOR("Omair Md Abdullah, Jarkko Nikula");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:byt-max98090");
