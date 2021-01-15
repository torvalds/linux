// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020 Bootlin SA
 * Author: Alexandre Belloni <alexandre.belloni@bootlin.com>
 */

#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/regulator/consumer.h>
#include <sound/soc.h>

struct simple_mux {
	struct gpio_desc *gpiod_mux;
	unsigned int mux;
};

static const char * const simple_mux_texts[] = {
	"Input 1", "Input 2"
};

static SOC_ENUM_SINGLE_EXT_DECL(simple_mux_enum, simple_mux_texts);

static int simple_mux_control_get(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dapm_context *dapm = snd_soc_dapm_kcontrol_dapm(kcontrol);
	struct snd_soc_component *c = snd_soc_dapm_to_component(dapm);
	struct simple_mux *priv = snd_soc_component_get_drvdata(c);

	ucontrol->value.enumerated.item[0] = priv->mux;

	return 0;
}

static int simple_mux_control_put(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dapm_context *dapm = snd_soc_dapm_kcontrol_dapm(kcontrol);
	struct soc_enum *e = (struct soc_enum *)kcontrol->private_value;
	struct snd_soc_component *c = snd_soc_dapm_to_component(dapm);
	struct simple_mux *priv = snd_soc_component_get_drvdata(c);

	if (ucontrol->value.enumerated.item[0] > e->items)
		return -EINVAL;

	if (priv->mux == ucontrol->value.enumerated.item[0])
		return 0;

	priv->mux = ucontrol->value.enumerated.item[0];

	gpiod_set_value_cansleep(priv->gpiod_mux, priv->mux);

	return snd_soc_dapm_mux_update_power(dapm, kcontrol,
					     ucontrol->value.enumerated.item[0],
					     e, NULL);
}

static const struct snd_kcontrol_new simple_mux_mux =
	SOC_DAPM_ENUM_EXT("Muxer", simple_mux_enum, simple_mux_control_get, simple_mux_control_put);

static const struct snd_soc_dapm_widget simple_mux_dapm_widgets[] = {
	SND_SOC_DAPM_INPUT("IN1"),
	SND_SOC_DAPM_INPUT("IN2"),
	SND_SOC_DAPM_MUX("MUX", SND_SOC_NOPM, 0, 0, &simple_mux_mux),
	SND_SOC_DAPM_OUTPUT("OUT"),
};

static const struct snd_soc_dapm_route simple_mux_dapm_routes[] = {
	{ "OUT", NULL, "MUX" },
	{ "MUX", "Input 1", "IN1" },
	{ "MUX", "Input 2", "IN2" },
};

static const struct snd_soc_component_driver simple_mux_component_driver = {
	.dapm_widgets		= simple_mux_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(simple_mux_dapm_widgets),
	.dapm_routes		= simple_mux_dapm_routes,
	.num_dapm_routes	= ARRAY_SIZE(simple_mux_dapm_routes),
};

static int simple_mux_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct simple_mux *priv;
	int err;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	dev_set_drvdata(dev, priv);

	priv->gpiod_mux = devm_gpiod_get(dev, "mux", GPIOD_OUT_LOW);
	if (IS_ERR(priv->gpiod_mux)) {
		err = PTR_ERR(priv->gpiod_mux);
		if (err != -EPROBE_DEFER)
			dev_err(dev, "Failed to get 'mux' gpio: %d", err);
		return err;
	}

	return devm_snd_soc_register_component(dev, &simple_mux_component_driver, NULL, 0);
}

#ifdef CONFIG_OF
static const struct of_device_id simple_mux_ids[] = {
	{ .compatible = "simple-audio-mux", },
	{ }
};
MODULE_DEVICE_TABLE(of, simple_mux_ids);
#endif

static struct platform_driver simple_mux_driver = {
	.driver = {
		.name = "simple-mux",
		.of_match_table = of_match_ptr(simple_mux_ids),
	},
	.probe = simple_mux_probe,
};

module_platform_driver(simple_mux_driver);

MODULE_DESCRIPTION("ASoC Simple Audio Mux driver");
MODULE_AUTHOR("Alexandre Belloni <alexandre.belloni@bootlin.com>");
MODULE_LICENSE("GPL");
