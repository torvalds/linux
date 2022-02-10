// SPDX-License-Identifier: GPL-2.0
//
// Analog Devices SSM2305 Amplifier Driver
//
// Copyright (C) 2018 Pengutronix, Marco Felsch <kernel@pengutronix.de>
//

#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <sound/soc.h>

#define DRV_NAME "ssm2305"

struct ssm2305 {
	/* shutdown gpio  */
	struct gpio_desc *gpiod_shutdown;
};

static int ssm2305_power_event(struct snd_soc_dapm_widget *w,
			       struct snd_kcontrol *kctrl, int event)
{
	struct snd_soc_component *c = snd_soc_dapm_to_component(w->dapm);
	struct ssm2305 *data = snd_soc_component_get_drvdata(c);

	gpiod_set_value_cansleep(data->gpiod_shutdown,
				 SND_SOC_DAPM_EVENT_ON(event));

	return 0;
}

static const struct snd_soc_dapm_widget ssm2305_dapm_widgets[] = {
	/* Stereo input/output */
	SND_SOC_DAPM_INPUT("L_IN"),
	SND_SOC_DAPM_INPUT("R_IN"),
	SND_SOC_DAPM_OUTPUT("L_OUT"),
	SND_SOC_DAPM_OUTPUT("R_OUT"),

	SND_SOC_DAPM_SUPPLY("Power", SND_SOC_NOPM, 0, 0, ssm2305_power_event,
			    SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD),
};

static const struct snd_soc_dapm_route ssm2305_dapm_routes[] = {
	{ "L_OUT", NULL, "L_IN" },
	{ "R_OUT", NULL, "R_IN" },
	{ "L_IN", NULL, "Power" },
	{ "R_IN", NULL, "Power" },
};

static const struct snd_soc_component_driver ssm2305_component_driver = {
	.dapm_widgets		= ssm2305_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(ssm2305_dapm_widgets),
	.dapm_routes		= ssm2305_dapm_routes,
	.num_dapm_routes	= ARRAY_SIZE(ssm2305_dapm_routes),
};

static int ssm2305_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct ssm2305 *priv;

	/* Allocate the private data */
	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	platform_set_drvdata(pdev, priv);

	/* Get shutdown gpio */
	priv->gpiod_shutdown = devm_gpiod_get(dev, "shutdown",
					      GPIOD_OUT_LOW);
	if (IS_ERR(priv->gpiod_shutdown))
		return dev_err_probe(dev, PTR_ERR(priv->gpiod_shutdown),
				     "Failed to get 'shutdown' gpio\n");

	return devm_snd_soc_register_component(dev, &ssm2305_component_driver,
					       NULL, 0);
}

#ifdef CONFIG_OF
static const struct of_device_id ssm2305_of_match[] = {
	{ .compatible = "adi,ssm2305", },
	{ }
};
MODULE_DEVICE_TABLE(of, ssm2305_of_match);
#endif

static struct platform_driver ssm2305_driver = {
	.driver = {
		.name = DRV_NAME,
		.of_match_table = of_match_ptr(ssm2305_of_match),
	},
	.probe = ssm2305_probe,
};

module_platform_driver(ssm2305_driver);

MODULE_DESCRIPTION("ASoC SSM2305 amplifier driver");
MODULE_AUTHOR("Marco Felsch <m.felsch@pengutronix.de>");
MODULE_LICENSE("GPL v2");
