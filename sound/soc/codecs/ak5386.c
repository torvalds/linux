// SPDX-License-Identifier: GPL-2.0-only
/*
 * ALSA SoC driver for
 *    Asahi Kasei AK5386 Single-ended 24-Bit 192kHz delta-sigma ADC
 *
 * (c) 2013 Daniel Mack <zonque@gmail.com>
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/of_device.h>
#include <linux/regulator/consumer.h>
#include <sound/soc.h>
#include <sound/pcm.h>
#include <sound/initval.h>

static const char * const supply_names[] = {
	"va", "vd"
};

struct ak5386_priv {
	int reset_gpio;
	struct regulator_bulk_data supplies[ARRAY_SIZE(supply_names)];
};

static const struct snd_soc_dapm_widget ak5386_dapm_widgets[] = {
SND_SOC_DAPM_INPUT("AINL"),
SND_SOC_DAPM_INPUT("AINR"),
};

static const struct snd_soc_dapm_route ak5386_dapm_routes[] = {
	{ "Capture", NULL, "AINL" },
	{ "Capture", NULL, "AINR" },
};

static int ak5386_soc_probe(struct snd_soc_component *component)
{
	struct ak5386_priv *priv = snd_soc_component_get_drvdata(component);
	return regulator_bulk_enable(ARRAY_SIZE(priv->supplies), priv->supplies);
}

static void ak5386_soc_remove(struct snd_soc_component *component)
{
	struct ak5386_priv *priv = snd_soc_component_get_drvdata(component);
	regulator_bulk_disable(ARRAY_SIZE(priv->supplies), priv->supplies);
}

#ifdef CONFIG_PM
static int ak5386_soc_suspend(struct snd_soc_component *component)
{
	struct ak5386_priv *priv = snd_soc_component_get_drvdata(component);
	regulator_bulk_disable(ARRAY_SIZE(priv->supplies), priv->supplies);
	return 0;
}

static int ak5386_soc_resume(struct snd_soc_component *component)
{
	struct ak5386_priv *priv = snd_soc_component_get_drvdata(component);
	return regulator_bulk_enable(ARRAY_SIZE(priv->supplies), priv->supplies);
}
#else
#define ak5386_soc_suspend	NULL
#define ak5386_soc_resume	NULL
#endif /* CONFIG_PM */

static const struct snd_soc_component_driver soc_component_ak5386 = {
	.probe			= ak5386_soc_probe,
	.remove			= ak5386_soc_remove,
	.suspend		= ak5386_soc_suspend,
	.resume			= ak5386_soc_resume,
	.dapm_widgets		= ak5386_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(ak5386_dapm_widgets),
	.dapm_routes		= ak5386_dapm_routes,
	.num_dapm_routes	= ARRAY_SIZE(ak5386_dapm_routes),
	.idle_bias_on		= 1,
	.use_pmdown_time	= 1,
	.endianness		= 1,
};

static int ak5386_set_dai_fmt(struct snd_soc_dai *codec_dai,
			      unsigned int format)
{
	struct snd_soc_component *component = codec_dai->component;

	format &= SND_SOC_DAIFMT_FORMAT_MASK;
	if (format != SND_SOC_DAIFMT_LEFT_J &&
	    format != SND_SOC_DAIFMT_I2S) {
		dev_err(component->dev, "Invalid DAI format\n");
		return -EINVAL;
	}

	return 0;
}

static int ak5386_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params,
			    struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct ak5386_priv *priv = snd_soc_component_get_drvdata(component);

	/*
	 * From the datasheet:
	 *
	 * All external clocks (MCLK, SCLK and LRCK) must be present unless
	 * PDN pin = “L”. If these clocks are not provided, the AK5386 may
	 * draw excess current due to its use of internal dynamically
	 * refreshed logic. If the external clocks are not present, place
	 * the AK5386 in power-down mode (PDN pin = “L”).
	 */

	if (gpio_is_valid(priv->reset_gpio))
		gpio_set_value(priv->reset_gpio, 1);

	return 0;
}

static int ak5386_hw_free(struct snd_pcm_substream *substream,
			  struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct ak5386_priv *priv = snd_soc_component_get_drvdata(component);

	if (gpio_is_valid(priv->reset_gpio))
		gpio_set_value(priv->reset_gpio, 0);

	return 0;
}

static const struct snd_soc_dai_ops ak5386_dai_ops = {
	.set_fmt	= ak5386_set_dai_fmt,
	.hw_params	= ak5386_hw_params,
	.hw_free	= ak5386_hw_free,
};

static struct snd_soc_dai_driver ak5386_dai = {
	.name		= "ak5386-hifi",
	.capture	= {
		.stream_name	= "Capture",
		.channels_min	= 1,
		.channels_max	= 2,
		.rates		= SNDRV_PCM_RATE_8000_192000,
		.formats	= SNDRV_PCM_FMTBIT_S8     |
				  SNDRV_PCM_FMTBIT_S16_LE |
				  SNDRV_PCM_FMTBIT_S24_LE |
				  SNDRV_PCM_FMTBIT_S24_3LE,
	},
	.ops	= &ak5386_dai_ops,
};

#ifdef CONFIG_OF
static const struct of_device_id ak5386_dt_ids[] = {
	{ .compatible = "asahi-kasei,ak5386", },
	{ }
};
MODULE_DEVICE_TABLE(of, ak5386_dt_ids);
#endif

static int ak5386_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct ak5386_priv *priv;
	int ret, i;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->reset_gpio = -EINVAL;
	dev_set_drvdata(dev, priv);

	for (i = 0; i < ARRAY_SIZE(supply_names); i++)
		priv->supplies[i].supply = supply_names[i];

	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(priv->supplies),
				      priv->supplies);
	if (ret < 0)
		return ret;

	if (of_match_device(of_match_ptr(ak5386_dt_ids), dev))
		priv->reset_gpio = of_get_named_gpio(dev->of_node,
						      "reset-gpio", 0);

	if (gpio_is_valid(priv->reset_gpio))
		if (devm_gpio_request_one(dev, priv->reset_gpio,
					  GPIOF_OUT_INIT_LOW,
					  "AK5386 Reset"))
			priv->reset_gpio = -EINVAL;

	return devm_snd_soc_register_component(dev, &soc_component_ak5386,
				      &ak5386_dai, 1);
}

static struct platform_driver ak5386_driver = {
	.probe		= ak5386_probe,
	.driver		= {
		.name	= "ak5386",
		.of_match_table = of_match_ptr(ak5386_dt_ids),
	},
};

module_platform_driver(ak5386_driver);

MODULE_DESCRIPTION("ASoC driver for AK5386 ADC");
MODULE_AUTHOR("Daniel Mack <zonque@gmail.com>");
MODULE_LICENSE("GPL");
