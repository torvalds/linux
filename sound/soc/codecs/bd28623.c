// SPDX-License-Identifier: GPL-2.0
//
// ROHM BD28623MUV class D speaker amplifier codec driver.
//
// Copyright (c) 2018 Socionext Inc.

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>
#include <sound/pcm.h>
#include <sound/soc.h>

#define BD28623_NUM_SUPPLIES    3

static const char *const bd28623_supply_names[BD28623_NUM_SUPPLIES] = {
	"VCCA",
	"VCCP1",
	"VCCP2",
};

struct bd28623_priv {
	struct device *dev;
	struct regulator_bulk_data supplies[BD28623_NUM_SUPPLIES];
	struct gpio_desc *reset_gpio;
	struct gpio_desc *mute_gpio;

	int switch_spk;
};

static const struct snd_soc_dapm_widget bd28623_widgets[] = {
	SND_SOC_DAPM_DAC("DAC", "Playback", SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_OUTPUT("OUT1P"),
	SND_SOC_DAPM_OUTPUT("OUT1N"),
	SND_SOC_DAPM_OUTPUT("OUT2P"),
	SND_SOC_DAPM_OUTPUT("OUT2N"),
};

static const struct snd_soc_dapm_route bd28623_routes[] = {
	{ "OUT1P", NULL, "DAC" },
	{ "OUT1N", NULL, "DAC" },
	{ "OUT2P", NULL, "DAC" },
	{ "OUT2N", NULL, "DAC" },
};

static int bd28623_power_on(struct bd28623_priv *bd)
{
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(bd->supplies), bd->supplies);
	if (ret) {
		dev_err(bd->dev, "Failed to enable supplies: %d\n", ret);
		return ret;
	}

	gpiod_set_value_cansleep(bd->reset_gpio, 0);
	usleep_range(300000, 400000);

	return 0;
}

static void bd28623_power_off(struct bd28623_priv *bd)
{
	gpiod_set_value_cansleep(bd->reset_gpio, 1);

	regulator_bulk_disable(ARRAY_SIZE(bd->supplies), bd->supplies);
}

static int bd28623_get_switch_spk(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct bd28623_priv *bd = snd_soc_component_get_drvdata(component);

	ucontrol->value.integer.value[0] = bd->switch_spk;

	return 0;
}

static int bd28623_set_switch_spk(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct bd28623_priv *bd = snd_soc_component_get_drvdata(component);

	if (bd->switch_spk == ucontrol->value.integer.value[0])
		return 0;

	bd->switch_spk = ucontrol->value.integer.value[0];

	gpiod_set_value_cansleep(bd->mute_gpio, bd->switch_spk ? 0 : 1);

	return 0;
}

static const struct snd_kcontrol_new bd28623_controls[] = {
	SOC_SINGLE_BOOL_EXT("Speaker Switch", 0,
			    bd28623_get_switch_spk, bd28623_set_switch_spk),
};

static int bd28623_codec_probe(struct snd_soc_component *component)
{
	struct bd28623_priv *bd = snd_soc_component_get_drvdata(component);
	int ret;

	bd->switch_spk = 1;

	ret = bd28623_power_on(bd);
	if (ret)
		return ret;

	gpiod_set_value_cansleep(bd->mute_gpio, bd->switch_spk ? 0 : 1);

	return 0;
}

static void bd28623_codec_remove(struct snd_soc_component *component)
{
	struct bd28623_priv *bd = snd_soc_component_get_drvdata(component);

	bd28623_power_off(bd);
}

static int bd28623_codec_suspend(struct snd_soc_component *component)
{
	struct bd28623_priv *bd = snd_soc_component_get_drvdata(component);

	bd28623_power_off(bd);

	return 0;
}

static int bd28623_codec_resume(struct snd_soc_component *component)
{
	struct bd28623_priv *bd = snd_soc_component_get_drvdata(component);
	int ret;

	ret = bd28623_power_on(bd);
	if (ret)
		return ret;

	gpiod_set_value_cansleep(bd->mute_gpio, bd->switch_spk ? 0 : 1);

	return 0;
}

static const struct snd_soc_component_driver soc_codec_bd = {
	.probe			= bd28623_codec_probe,
	.remove			= bd28623_codec_remove,
	.suspend		= bd28623_codec_suspend,
	.resume			= bd28623_codec_resume,
	.dapm_widgets		= bd28623_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(bd28623_widgets),
	.dapm_routes		= bd28623_routes,
	.num_dapm_routes	= ARRAY_SIZE(bd28623_routes),
	.controls		= bd28623_controls,
	.num_controls		= ARRAY_SIZE(bd28623_controls),
	.idle_bias_on		= 1,
	.use_pmdown_time	= 1,
	.endianness		= 1,
};

static struct snd_soc_dai_driver soc_dai_bd = {
	.name     = "bd28623-speaker",
	.playback = {
		.stream_name  = "Playback",
		.formats      = SNDRV_PCM_FMTBIT_S32_LE |
				SNDRV_PCM_FMTBIT_S24_LE |
				SNDRV_PCM_FMTBIT_S16_LE,
		.rates        = SNDRV_PCM_RATE_48000 |
				SNDRV_PCM_RATE_44100 |
				SNDRV_PCM_RATE_32000,
		.channels_min = 2,
		.channels_max = 2,
	},
};

static int bd28623_probe(struct platform_device *pdev)
{
	struct bd28623_priv *bd;
	struct device *dev = &pdev->dev;
	int i, ret;

	bd = devm_kzalloc(&pdev->dev, sizeof(struct bd28623_priv), GFP_KERNEL);
	if (!bd)
		return -ENOMEM;

	for (i = 0; i < ARRAY_SIZE(bd->supplies); i++)
		bd->supplies[i].supply = bd28623_supply_names[i];

	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(bd->supplies),
				      bd->supplies);
	if (ret) {
		dev_err(dev, "Failed to get supplies: %d\n", ret);
		return ret;
	}

	bd->reset_gpio = devm_gpiod_get_optional(dev, "reset",
						 GPIOD_OUT_HIGH);
	if (IS_ERR(bd->reset_gpio)) {
		dev_err(dev, "Failed to request reset_gpio: %ld\n",
			PTR_ERR(bd->reset_gpio));
		return PTR_ERR(bd->reset_gpio);
	}

	bd->mute_gpio = devm_gpiod_get_optional(dev, "mute",
						GPIOD_OUT_HIGH);
	if (IS_ERR(bd->mute_gpio)) {
		dev_err(dev, "Failed to request mute_gpio: %ld\n",
			PTR_ERR(bd->mute_gpio));
		return PTR_ERR(bd->mute_gpio);
	}

	platform_set_drvdata(pdev, bd);
	bd->dev = dev;

	return devm_snd_soc_register_component(dev, &soc_codec_bd,
					       &soc_dai_bd, 1);
}

static const struct of_device_id bd28623_of_match[] __maybe_unused = {
	{ .compatible = "rohm,bd28623", },
	{}
};
MODULE_DEVICE_TABLE(of, bd28623_of_match);

static struct platform_driver bd28623_codec_driver = {
	.driver = {
		.name = "bd28623",
		.of_match_table = of_match_ptr(bd28623_of_match),
	},
	.probe  = bd28623_probe,
};
module_platform_driver(bd28623_codec_driver);

MODULE_AUTHOR("Katsuhiro Suzuki <suzuki.katsuhiro@socionext.com>");
MODULE_DESCRIPTION("ROHM BD28623 speaker amplifier driver");
MODULE_LICENSE("GPL v2");
