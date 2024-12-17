// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020 Bootlin SA
 * Author: Alexandre Belloni <alexandre.belloni@bootlin.com>
 */

#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/mux/driver.h>
#include <linux/regulator/consumer.h>
#include <sound/soc.h>

#define MUX_TEXT_SIZE	2
#define MUX_WIDGET_SIZE	4
#define MUX_ROUTE_SIZE	3
struct simple_mux {
	struct gpio_desc *gpiod_mux;
	unsigned int mux;
	const char *mux_texts[MUX_TEXT_SIZE];
	unsigned int idle_state;
	struct soc_enum mux_enum;
	struct snd_kcontrol_new mux_mux;
	struct snd_soc_dapm_widget mux_widgets[MUX_WIDGET_SIZE];
	struct snd_soc_dapm_route mux_routes[MUX_ROUTE_SIZE];
	struct snd_soc_component_driver mux_driver;
};

static const char * const simple_mux_texts[MUX_TEXT_SIZE] = {
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

	if (priv->idle_state != MUX_IDLE_AS_IS && dapm->bias_level < SND_SOC_BIAS_PREPARE)
		return 0;

	gpiod_set_value_cansleep(priv->gpiod_mux, priv->mux);

	return snd_soc_dapm_mux_update_power(dapm, kcontrol,
					     ucontrol->value.enumerated.item[0],
					     e, NULL);
}

static unsigned int simple_mux_read(struct snd_soc_component *component,
				    unsigned int reg)
{
	struct simple_mux *priv = snd_soc_component_get_drvdata(component);

	return priv->mux;
}

static const struct snd_kcontrol_new simple_mux_mux =
	SOC_DAPM_ENUM_EXT("Muxer", simple_mux_enum, simple_mux_control_get, simple_mux_control_put);

static int simple_mux_event(struct snd_soc_dapm_widget *w,
			    struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *c = snd_soc_dapm_to_component(w->dapm);
	struct simple_mux *priv = snd_soc_component_get_drvdata(c);

	if (priv->idle_state != MUX_IDLE_AS_IS) {
		switch (event) {
		case SND_SOC_DAPM_PRE_PMU:
			gpiod_set_value_cansleep(priv->gpiod_mux, priv->mux);
			break;
		case SND_SOC_DAPM_POST_PMD:
			gpiod_set_value_cansleep(priv->gpiod_mux, priv->idle_state);
			break;
		default:
			break;
		}
	}

	return 0;
}

static const struct snd_soc_dapm_widget simple_mux_dapm_widgets[MUX_WIDGET_SIZE] = {
	SND_SOC_DAPM_INPUT("IN1"),
	SND_SOC_DAPM_INPUT("IN2"),
	SND_SOC_DAPM_MUX_E("MUX", SND_SOC_NOPM, 0, 0, &simple_mux_mux, // see simple_mux_probe()
			   simple_mux_event, SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
	SND_SOC_DAPM_OUTPUT("OUT"),
};

static const struct snd_soc_dapm_route simple_mux_dapm_routes[MUX_ROUTE_SIZE] = {
	{ "OUT", NULL, "MUX" },
	{ "MUX", "Input 1", "IN1" }, // see simple_mux_probe()
	{ "MUX", "Input 2", "IN2" }, // see simple_mux_probe()
};

static int simple_mux_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct simple_mux *priv;
	int ret;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	dev_set_drvdata(dev, priv);

	priv->gpiod_mux = devm_gpiod_get(dev, "mux", GPIOD_OUT_LOW);
	if (IS_ERR(priv->gpiod_mux))
		return dev_err_probe(dev, PTR_ERR(priv->gpiod_mux),
				     "Failed to get 'mux' gpio");

	/* Copy default settings */
	memcpy(&priv->mux_texts,	&simple_mux_texts,		sizeof(priv->mux_texts));
	memcpy(&priv->mux_enum,		&simple_mux_enum,		sizeof(priv->mux_enum));
	memcpy(&priv->mux_mux,		&simple_mux_mux,		sizeof(priv->mux_mux));
	memcpy(&priv->mux_widgets,	&simple_mux_dapm_widgets,	sizeof(priv->mux_widgets));
	memcpy(&priv->mux_routes,	&simple_mux_dapm_routes,	sizeof(priv->mux_routes));

	priv->mux_driver.dapm_widgets		= priv->mux_widgets;
	priv->mux_driver.num_dapm_widgets	= MUX_WIDGET_SIZE;
	priv->mux_driver.dapm_routes		= priv->mux_routes;
	priv->mux_driver.num_dapm_routes	= MUX_ROUTE_SIZE;
	priv->mux_driver.read			= simple_mux_read;

	/* Overwrite text ("Input 1", "Input 2") if property exists */
	of_property_read_string_array(np, "state-labels", priv->mux_texts, MUX_TEXT_SIZE);

	ret = of_property_read_u32(np, "idle-state", &priv->idle_state);
	if (ret < 0) {
		priv->idle_state = MUX_IDLE_AS_IS;
	} else if (priv->idle_state != MUX_IDLE_AS_IS && priv->idle_state >= 2) {
		dev_err(dev, "invalid idle-state %u\n", priv->idle_state);
		return -EINVAL;
	}

	/* switch to use priv data instead of default */
	priv->mux_enum.texts			= priv->mux_texts;
	priv->mux_mux.private_value		= (unsigned long)&priv->mux_enum;
	priv->mux_widgets[2].kcontrol_news	= &priv->mux_mux;
	priv->mux_routes[1].control		= priv->mux_texts[0]; // "Input 1"
	priv->mux_routes[2].control		= priv->mux_texts[1]; // "Input 2"

	return devm_snd_soc_register_component(dev, &priv->mux_driver, NULL, 0);
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
