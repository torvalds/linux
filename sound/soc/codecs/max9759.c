// SPDX-License-Identifier: GPL-2.0
/*
 * MAX9759 Amplifier Driver
 *
 * Copyright (c) 2017 BayLibre, SAS.
 * Author: Neil Armstrong <narmstrong@baylibre.com>
 */

#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/tlv.h>

#define DRV_NAME "max9759"

struct max9759 {
	struct gpio_desc *gpiod_shutdown;
	struct gpio_desc *gpiod_mute;
	struct gpio_descs *gpiod_gain;
	bool is_mute;
	unsigned int gain;
};

static int pga_event(struct snd_soc_dapm_widget *w,
		     struct snd_kcontrol *control, int event)
{
	struct snd_soc_component *c = snd_soc_dapm_to_component(w->dapm);
	struct max9759 *priv = snd_soc_component_get_drvdata(c);

	if (SND_SOC_DAPM_EVENT_ON(event))
		gpiod_set_value_cansleep(priv->gpiod_shutdown, 0);
	else
		gpiod_set_value_cansleep(priv->gpiod_shutdown, 1);

	return 0;
}

/* From 6dB to 24dB in steps of 6dB */
static const DECLARE_TLV_DB_SCALE(speaker_gain_tlv, 600, 600, 0);

static int speaker_gain_control_get(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *c = snd_soc_kcontrol_component(kcontrol);
	struct max9759 *priv = snd_soc_component_get_drvdata(c);

	ucontrol->value.integer.value[0] = priv->gain;

	return 0;
}

static const bool speaker_gain_table[4][2] = {
	/* G1, G2 */
	{true, true},	/* +6dB */
	{false, true},	/* +12dB */
	{true, false},	/* +18dB */
	{false, false},	/* +24dB */
};

static int speaker_gain_control_put(struct snd_kcontrol *kcontrol,
				    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *c = snd_soc_kcontrol_component(kcontrol);
	struct max9759 *priv = snd_soc_component_get_drvdata(c);

	if (ucontrol->value.integer.value[0] > 3)
		return -EINVAL;

	priv->gain = ucontrol->value.integer.value[0];

	/* G1 */
	gpiod_set_value_cansleep(priv->gpiod_gain->desc[0],
				 speaker_gain_table[priv->gain][0]);
	/* G2 */
	gpiod_set_value_cansleep(priv->gpiod_gain->desc[1],
				 speaker_gain_table[priv->gain][1]);

	return 1;
}

static int speaker_mute_get(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *c = snd_soc_kcontrol_component(kcontrol);
	struct max9759 *priv = snd_soc_component_get_drvdata(c);

	ucontrol->value.integer.value[0] = !priv->is_mute;

	return 0;
}

static int speaker_mute_put(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *c = snd_soc_kcontrol_component(kcontrol);
	struct max9759 *priv = snd_soc_component_get_drvdata(c);

	priv->is_mute = !ucontrol->value.integer.value[0];

	gpiod_set_value_cansleep(priv->gpiod_mute, priv->is_mute);

	return 1;
}

static const struct snd_kcontrol_new max9759_dapm_controls[] = {
	SOC_SINGLE_EXT_TLV("Speaker Gain Volume", 0, 0, 3, 0,
			   speaker_gain_control_get, speaker_gain_control_put,
			   speaker_gain_tlv),
	SOC_SINGLE_BOOL_EXT("Playback Switch", 0,
			    speaker_mute_get, speaker_mute_put),
};

static const struct snd_soc_dapm_widget max9759_dapm_widgets[] = {
	SND_SOC_DAPM_INPUT("INL"),
	SND_SOC_DAPM_INPUT("INR"),
	SND_SOC_DAPM_PGA_E("PGA", SND_SOC_NOPM, 0, 0, NULL, 0, pga_event,
			   (SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_POST_PMD)),
	SND_SOC_DAPM_OUTPUT("OUTL"),
	SND_SOC_DAPM_OUTPUT("OUTR"),
};

static const struct snd_soc_dapm_route max9759_dapm_routes[] = {
	{ "PGA", NULL, "INL" },
	{ "PGA", NULL, "INR" },
	{ "OUTL", NULL, "PGA" },
	{ "OUTR", NULL, "PGA" },
};

static const struct snd_soc_component_driver max9759_component_driver = {
	.controls		= max9759_dapm_controls,
	.num_controls		= ARRAY_SIZE(max9759_dapm_controls),
	.dapm_widgets		= max9759_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(max9759_dapm_widgets),
	.dapm_routes		= max9759_dapm_routes,
	.num_dapm_routes	= ARRAY_SIZE(max9759_dapm_routes),
};

static int max9759_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct max9759 *priv;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	platform_set_drvdata(pdev, priv);

	priv->gpiod_shutdown = devm_gpiod_get(dev, "shutdown", GPIOD_OUT_HIGH);
	if (IS_ERR(priv->gpiod_shutdown))
		return dev_err_probe(dev, PTR_ERR(priv->gpiod_shutdown),
				     "Failed to get 'shutdown' gpio");

	priv->gpiod_mute = devm_gpiod_get(dev, "mute", GPIOD_OUT_HIGH);
	if (IS_ERR(priv->gpiod_mute))
		return dev_err_probe(dev, PTR_ERR(priv->gpiod_mute),
				     "Failed to get 'mute' gpio");
	priv->is_mute = true;

	priv->gpiod_gain = devm_gpiod_get_array(dev, "gain", GPIOD_OUT_HIGH);
	if (IS_ERR(priv->gpiod_gain))
		return dev_err_probe(dev, PTR_ERR(priv->gpiod_gain),
				     "Failed to get 'gain' gpios");
	priv->gain = 0;

	if (priv->gpiod_gain->ndescs != 2) {
		dev_err(dev, "Invalid 'gain' gpios count: %d",
			priv->gpiod_gain->ndescs);
		return -EINVAL;
	}

	return devm_snd_soc_register_component(dev, &max9759_component_driver,
					       NULL, 0);
}

#ifdef CONFIG_OF
static const struct of_device_id max9759_ids[] = {
	{ .compatible = "maxim,max9759", },
	{ }
};
MODULE_DEVICE_TABLE(of, max9759_ids);
#endif

static struct platform_driver max9759_driver = {
	.driver = {
		.name = DRV_NAME,
		.of_match_table = of_match_ptr(max9759_ids),
	},
	.probe = max9759_probe,
};

module_platform_driver(max9759_driver);

MODULE_DESCRIPTION("ASoC MAX9759 amplifier driver");
MODULE_AUTHOR("Neil Armstrong <narmstrong@baylibre.com>");
MODULE_LICENSE("GPL");
