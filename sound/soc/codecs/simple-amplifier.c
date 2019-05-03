/*
 * Copyright (c) 2017 BayLibre, SAS.
 * Author: Jerome Brunet <jbrunet@baylibre.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 * The full GNU General Public License is included in this distribution
 * in the file called COPYING.
 */

#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/regulator/consumer.h>
#include <sound/soc.h>

#define DRV_NAME "simple-amplifier"

struct simple_amp {
	struct gpio_desc *gpiod_enable;
};

static int drv_event(struct snd_soc_dapm_widget *w,
		     struct snd_kcontrol *control, int event)
{
	struct snd_soc_component *c = snd_soc_dapm_to_component(w->dapm);
	struct simple_amp *priv = snd_soc_component_get_drvdata(c);
	int val;

	switch (event) {
	case SND_SOC_DAPM_POST_PMU:
		val = 1;
		break;
	case SND_SOC_DAPM_PRE_PMD:
		val = 0;
		break;
	default:
		WARN(1, "Unexpected event");
		return -EINVAL;
	}

	gpiod_set_value_cansleep(priv->gpiod_enable, val);

	return 0;
}

static const struct snd_soc_dapm_widget simple_amp_dapm_widgets[] = {
	SND_SOC_DAPM_INPUT("INL"),
	SND_SOC_DAPM_INPUT("INR"),
	SND_SOC_DAPM_OUT_DRV_E("DRV", SND_SOC_NOPM, 0, 0, NULL, 0, drv_event,
			       (SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD)),
	SND_SOC_DAPM_OUTPUT("OUTL"),
	SND_SOC_DAPM_OUTPUT("OUTR"),
	SND_SOC_DAPM_REGULATOR_SUPPLY("VCC", 20, 0),
};

static const struct snd_soc_dapm_route simple_amp_dapm_routes[] = {
	{ "DRV", NULL, "INL" },
	{ "DRV", NULL, "INR" },
	{ "OUTL", NULL, "VCC" },
	{ "OUTR", NULL, "VCC" },
	{ "OUTL", NULL, "DRV" },
	{ "OUTR", NULL, "DRV" },
};

static const struct snd_soc_component_driver simple_amp_component_driver = {
	.dapm_widgets		= simple_amp_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(simple_amp_dapm_widgets),
	.dapm_routes		= simple_amp_dapm_routes,
	.num_dapm_routes	= ARRAY_SIZE(simple_amp_dapm_routes),
};

static int simple_amp_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct simple_amp *priv;
	int err;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (priv == NULL)
		return -ENOMEM;
	platform_set_drvdata(pdev, priv);

	priv->gpiod_enable = devm_gpiod_get(dev, "enable", GPIOD_OUT_LOW);
	if (IS_ERR(priv->gpiod_enable)) {
		err = PTR_ERR(priv->gpiod_enable);
		if (err != -EPROBE_DEFER)
			dev_err(dev, "Failed to get 'enable' gpio: %d", err);
		return err;
	}

	return devm_snd_soc_register_component(dev,
					       &simple_amp_component_driver,
					       NULL, 0);
}

#ifdef CONFIG_OF
static const struct of_device_id simple_amp_ids[] = {
	{ .compatible = "dioo,dio2125", },
	{ .compatible = "simple-audio-amplifier", },
	{ }
};
MODULE_DEVICE_TABLE(of, simple_amp_ids);
#endif

static struct platform_driver simple_amp_driver = {
	.driver = {
		.name = DRV_NAME,
		.of_match_table = of_match_ptr(simple_amp_ids),
	},
	.probe = simple_amp_probe,
};

module_platform_driver(simple_amp_driver);

MODULE_DESCRIPTION("ASoC Simple Audio Amplifier driver");
MODULE_AUTHOR("Jerome Brunet <jbrunet@baylibre.com>");
MODULE_LICENSE("GPL");
