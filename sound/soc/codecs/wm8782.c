// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * sound/soc/codecs/wm8782.c
 * simple, strap-pin configured 24bit 2ch ADC
 *
 * Copyright: 2011 Raumfeld GmbH
 * Author: Johannes Stezenbach <js@sig21.net>
 *
 * based on ad73311.c
 * Copyright:	Analog Devices Inc.
 * Author:	Cliff Cai <cliff.cai@analog.com>
 */

#include <linux/init.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/regulator/consumer.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/ac97_codec.h>
#include <sound/initval.h>
#include <sound/soc.h>

static const struct snd_soc_dapm_widget wm8782_dapm_widgets[] = {
SND_SOC_DAPM_INPUT("AINL"),
SND_SOC_DAPM_INPUT("AINR"),
};

static const struct snd_soc_dapm_route wm8782_dapm_routes[] = {
	{ "Capture", NULL, "AINL" },
	{ "Capture", NULL, "AINR" },
};

static struct snd_soc_dai_driver wm8782_dai = {
	.name = "wm8782",
	.capture = {
		.stream_name = "Capture",
		.channels_min = 2,
		.channels_max = 2,
		/* For configurations with FSAMPEN=0 */
		.rates = SNDRV_PCM_RATE_8000_48000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE |
			   SNDRV_PCM_FMTBIT_S20_3LE |
			   SNDRV_PCM_FMTBIT_S24_LE,
	},
};

/* regulator power supply names */
static const char *supply_names[] = {
	"Vdda", /* analog supply, 2.7V - 3.6V */
	"Vdd",  /* digital supply, 2.7V - 5.5V */
};

struct wm8782_priv {
	struct regulator_bulk_data supplies[ARRAY_SIZE(supply_names)];
};

static int wm8782_soc_probe(struct snd_soc_component *component)
{
	struct wm8782_priv *priv = snd_soc_component_get_drvdata(component);
	return regulator_bulk_enable(ARRAY_SIZE(priv->supplies), priv->supplies);
}

static void wm8782_soc_remove(struct snd_soc_component *component)
{
	struct wm8782_priv *priv = snd_soc_component_get_drvdata(component);
	regulator_bulk_disable(ARRAY_SIZE(priv->supplies), priv->supplies);
}

#ifdef CONFIG_PM
static int wm8782_soc_suspend(struct snd_soc_component *component)
{
	struct wm8782_priv *priv = snd_soc_component_get_drvdata(component);
	regulator_bulk_disable(ARRAY_SIZE(priv->supplies), priv->supplies);
	return 0;
}

static int wm8782_soc_resume(struct snd_soc_component *component)
{
	struct wm8782_priv *priv = snd_soc_component_get_drvdata(component);
	return regulator_bulk_enable(ARRAY_SIZE(priv->supplies), priv->supplies);
}
#else
#define wm8782_soc_suspend      NULL
#define wm8782_soc_resume       NULL
#endif /* CONFIG_PM */

static const struct snd_soc_component_driver soc_component_dev_wm8782 = {
	.probe			= wm8782_soc_probe,
	.remove			= wm8782_soc_remove,
	.suspend		= wm8782_soc_suspend,
	.resume			= wm8782_soc_resume,
	.dapm_widgets		= wm8782_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(wm8782_dapm_widgets),
	.dapm_routes		= wm8782_dapm_routes,
	.num_dapm_routes	= ARRAY_SIZE(wm8782_dapm_routes),
	.idle_bias_on		= 1,
	.use_pmdown_time	= 1,
	.endianness		= 1,
};

static int wm8782_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct wm8782_priv *priv;
	int ret, i;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	dev_set_drvdata(dev, priv);

	for (i = 0; i < ARRAY_SIZE(supply_names); i++)
		priv->supplies[i].supply = supply_names[i];

	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(priv->supplies),
				      priv->supplies);
	if (ret < 0)
		return ret;

	return devm_snd_soc_register_component(&pdev->dev,
			&soc_component_dev_wm8782, &wm8782_dai, 1);
}

#ifdef CONFIG_OF
static const struct of_device_id wm8782_of_match[] = {
	{ .compatible = "wlf,wm8782", },
	{ }
};
MODULE_DEVICE_TABLE(of, wm8782_of_match);
#endif

static struct platform_driver wm8782_codec_driver = {
	.driver = {
		.name = "wm8782",
		.of_match_table = of_match_ptr(wm8782_of_match),
	},
	.probe = wm8782_probe,
};

module_platform_driver(wm8782_codec_driver);

MODULE_DESCRIPTION("ASoC WM8782 driver");
MODULE_AUTHOR("Johannes Stezenbach <js@sig21.net>");
MODULE_LICENSE("GPL");
