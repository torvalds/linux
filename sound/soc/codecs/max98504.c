// SPDX-License-Identifier: GPL-2.0-only
/*
 * MAX98504 ALSA SoC Audio driver
 *
 * Copyright 2013 - 2014 Maxim Integrated Products
 * Copyright 2016 Samsung Electronics Co., Ltd.
 */

#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <sound/soc.h>

#include "max98504.h"

static const char * const max98504_supply_names[] = {
	"DVDD",
	"DIOVDD",
	"PVDD",
};
#define MAX98504_NUM_SUPPLIES ARRAY_SIZE(max98504_supply_names)

struct max98504_priv {
	struct regmap *regmap;
	struct regulator_bulk_data supplies[MAX98504_NUM_SUPPLIES];
	unsigned int pcm_rx_channels;
	bool brownout_enable;
	unsigned int brownout_threshold;
	unsigned int brownout_attenuation;
	unsigned int brownout_attack_hold;
	unsigned int brownout_timed_hold;
	unsigned int brownout_release_rate;
};

static struct reg_default max98504_reg_defaults[] = {
	{ 0x01,	0},
	{ 0x02,	0},
	{ 0x03,	0},
	{ 0x04,	0},
	{ 0x10, 0},
	{ 0x11, 0},
	{ 0x12, 0},
	{ 0x13, 0},
	{ 0x14, 0},
	{ 0x15, 0},
	{ 0x16, 0},
	{ 0x17, 0},
	{ 0x18, 0},
	{ 0x19, 0},
	{ 0x1A, 0},
	{ 0x20, 0},
	{ 0x21, 0},
	{ 0x22, 0},
	{ 0x23, 0},
	{ 0x24, 0},
	{ 0x25, 0},
	{ 0x26, 0},
	{ 0x27, 0},
	{ 0x28, 0},
	{ 0x30, 0},
	{ 0x31, 0},
	{ 0x32, 0},
	{ 0x33, 0},
	{ 0x34, 0},
	{ 0x35, 0},
	{ 0x36, 0},
	{ 0x37, 0},
	{ 0x38, 0},
	{ 0x39, 0},
	{ 0x40, 0},
	{ 0x41, 0},
};

static bool max98504_volatile_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case MAX98504_INTERRUPT_STATUS:
	case MAX98504_INTERRUPT_FLAGS:
	case MAX98504_INTERRUPT_FLAG_CLEARS:
	case MAX98504_WATCHDOG_CLEAR:
	case MAX98504_GLOBAL_ENABLE:
	case MAX98504_SOFTWARE_RESET:
		return true;
	default:
		return false;
	}
}

static bool max98504_readable_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case MAX98504_SOFTWARE_RESET:
	case MAX98504_WATCHDOG_CLEAR:
	case MAX98504_INTERRUPT_FLAG_CLEARS:
		return false;
	default:
		return true;
	}
}

static int max98504_pcm_rx_ev(struct snd_soc_dapm_widget *w,
			      struct snd_kcontrol *kcontrol, int event)
{
	struct snd_soc_component *c = snd_soc_dapm_to_component(w->dapm);
	struct max98504_priv *max98504 = snd_soc_component_get_drvdata(c);

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		regmap_write(max98504->regmap, MAX98504_PCM_RX_ENABLE,
			     max98504->pcm_rx_channels);
		break;
	case SND_SOC_DAPM_POST_PMD:
		regmap_write(max98504->regmap, MAX98504_PCM_RX_ENABLE, 0);
		break;
	}

	return 0;
}

static int max98504_component_probe(struct snd_soc_component *c)
{
	struct max98504_priv *max98504 = snd_soc_component_get_drvdata(c);
	struct regmap *map = max98504->regmap;
	int ret;

	ret = regulator_bulk_enable(MAX98504_NUM_SUPPLIES, max98504->supplies);
	if (ret < 0)
		return ret;

	regmap_write(map, MAX98504_SOFTWARE_RESET, 0x1);
	msleep(20);

	if (!max98504->brownout_enable)
		return 0;

	regmap_write(map, MAX98504_PVDD_BROWNOUT_ENABLE, 0x1);

	regmap_write(map, MAX98504_PVDD_BROWNOUT_CONFIG_1,
		     (max98504->brownout_threshold & 0x1f) << 3 |
		     (max98504->brownout_attenuation & 0x3));

	regmap_write(map, MAX98504_PVDD_BROWNOUT_CONFIG_2,
		     max98504->brownout_attack_hold & 0xff);

	regmap_write(map, MAX98504_PVDD_BROWNOUT_CONFIG_3,
		     max98504->brownout_timed_hold & 0xff);

	regmap_write(map, MAX98504_PVDD_BROWNOUT_CONFIG_4,
		     max98504->brownout_release_rate & 0xff);

	return 0;
}

static void max98504_component_remove(struct snd_soc_component *c)
{
	struct max98504_priv *max98504 = snd_soc_component_get_drvdata(c);

	regulator_bulk_disable(MAX98504_NUM_SUPPLIES, max98504->supplies);
}

static const char *spk_source_mux_text[] = {
	"PCM Monomix", "Analog In", "PDM Left", "PDM Right"
};

static const struct soc_enum spk_source_mux_enum =
	SOC_ENUM_SINGLE(MAX98504_SPEAKER_SOURCE_SELECT,
			0, ARRAY_SIZE(spk_source_mux_text),
			spk_source_mux_text);

static const struct snd_kcontrol_new spk_source_mux =
	SOC_DAPM_ENUM("SPK Source", spk_source_mux_enum);

static const struct snd_soc_dapm_route max98504_dapm_routes[] = {
	{ "SPKOUT", NULL, "Global Enable" },
	{ "SPK Source", "PCM Monomix", "DAC PCM" },
	{ "SPK Source", "Analog In", "AIN" },
	{ "SPK Source", "PDM Left", "DAC PDM" },
	{ "SPK Source", "PDM Right", "DAC PDM" },
};

static const struct snd_soc_dapm_widget max98504_dapm_widgets[] = {
	SND_SOC_DAPM_SUPPLY("Global Enable", MAX98504_GLOBAL_ENABLE,
		0, 0, NULL, 0),
	SND_SOC_DAPM_INPUT("AIN"),
	SND_SOC_DAPM_AIF_OUT("AIF2OUTL", "AIF2 Capture", 0, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_AIF_OUT("AIF2OUTR", "AIF2 Capture", 1, SND_SOC_NOPM, 0, 0),
	SND_SOC_DAPM_DAC_E("DAC PCM", NULL, SND_SOC_NOPM, 0, 0,
		max98504_pcm_rx_ev,
		SND_SOC_DAPM_POST_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_DAC("DAC PDM", NULL, MAX98504_PDM_RX_ENABLE, 0, 0),
	SND_SOC_DAPM_MUX("SPK Source", SND_SOC_NOPM, 0, 0, &spk_source_mux),
	SND_SOC_DAPM_REG(snd_soc_dapm_spk, "SPKOUT",
		MAX98504_SPEAKER_ENABLE, 0, 1, 1, 0),
};

static int max98504_set_tdm_slot(struct snd_soc_dai *dai,
		unsigned int tx_mask, unsigned int rx_mask,
		int slots, int slot_width)
{
	struct max98504_priv *max98504 = snd_soc_dai_get_drvdata(dai);
	struct regmap *map = max98504->regmap;


	switch (dai->id) {
	case MAX98504_DAI_ID_PCM:
		regmap_write(map, MAX98504_PCM_TX_ENABLE, tx_mask);
		max98504->pcm_rx_channels = rx_mask;
		break;

	case MAX98504_DAI_ID_PDM:
		regmap_write(map, MAX98504_PDM_TX_ENABLE, tx_mask);
		break;
	default:
		WARN_ON(1);
	}

	return 0;
}
static int max98504_set_channel_map(struct snd_soc_dai *dai,
		unsigned int tx_num, unsigned int *tx_slot,
		unsigned int rx_num, unsigned int *rx_slot)
{
	struct max98504_priv *max98504 = snd_soc_dai_get_drvdata(dai);
	struct regmap *map = max98504->regmap;
	unsigned int i, sources = 0;

	for (i = 0; i < tx_num; i++)
		if (tx_slot[i])
			sources |= (1 << i);

	switch (dai->id) {
	case MAX98504_DAI_ID_PCM:
		regmap_write(map, MAX98504_PCM_TX_CHANNEL_SOURCES,
			     sources);
		break;

	case MAX98504_DAI_ID_PDM:
		regmap_write(map, MAX98504_PDM_TX_CONTROL, sources);
		break;
	default:
		WARN_ON(1);
	}

	regmap_write(map, MAX98504_MEASUREMENT_ENABLE, sources ? 0x3 : 0x01);

	return 0;
}

static const struct snd_soc_dai_ops max98504_dai_ops = {
	.set_tdm_slot		= max98504_set_tdm_slot,
	.set_channel_map	= max98504_set_channel_map,
};

#define MAX98504_FORMATS	(SNDRV_PCM_FMTBIT_S8|SNDRV_PCM_FMTBIT_S16_LE|\
				SNDRV_PCM_FMTBIT_S24_LE|SNDRV_PCM_FMTBIT_S32_LE)
#define MAX98504_PDM_RATES	(SNDRV_PCM_RATE_8000|SNDRV_PCM_RATE_16000|\
				SNDRV_PCM_RATE_32000|SNDRV_PCM_RATE_44100|\
				SNDRV_PCM_RATE_48000|SNDRV_PCM_RATE_88200|\
				SNDRV_PCM_RATE_96000)

static struct snd_soc_dai_driver max98504_dai[] = {
	/* TODO: Add the PCM interface definitions */
	{
		.name = "max98504-aif2",
		.id = MAX98504_DAI_ID_PDM,
		.playback = {
			.stream_name	= "AIF2 Playback",
			.channels_min	= 1,
			.channels_max	= 2,
			.rates		= MAX98504_PDM_RATES,
			.formats	= MAX98504_FORMATS,
		},
		.capture = {
			.stream_name	= "AIF2 Capture",
			.channels_min	= 1,
			.channels_max	= 2,
			.rates		= MAX98504_PDM_RATES,
			.formats	= MAX98504_FORMATS,
		},
		.ops = &max98504_dai_ops,
	},
};

static const struct snd_soc_component_driver max98504_component_driver = {
	.probe			= max98504_component_probe,
	.remove			= max98504_component_remove,
	.dapm_widgets		= max98504_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(max98504_dapm_widgets),
	.dapm_routes		= max98504_dapm_routes,
	.num_dapm_routes	= ARRAY_SIZE(max98504_dapm_routes),
	.endianness		= 1,
};

static const struct regmap_config max98504_regmap = {
	.reg_bits		= 16,
	.val_bits		= 8,
	.max_register		= MAX98504_MAX_REGISTER,
	.reg_defaults		= max98504_reg_defaults,
	.num_reg_defaults	= ARRAY_SIZE(max98504_reg_defaults),
	.volatile_reg		= max98504_volatile_register,
	.readable_reg		= max98504_readable_register,
	.cache_type		= REGCACHE_RBTREE,
};

static int max98504_i2c_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct device_node *node = dev->of_node;
	struct max98504_priv *max98504;
	int i, ret;

	max98504 = devm_kzalloc(dev, sizeof(*max98504), GFP_KERNEL);
	if (!max98504)
		return -ENOMEM;

	if (node) {
		if (!of_property_read_u32(node, "maxim,brownout-threshold",
					&max98504->brownout_threshold))
			max98504->brownout_enable = true;

		of_property_read_u32(node, "maxim,brownout-attenuation",
					&max98504->brownout_attenuation);
		of_property_read_u32(node, "maxim,brownout-attack-hold-ms",
					&max98504->brownout_attack_hold);
		of_property_read_u32(node, "maxim,brownout-timed-hold-ms",
					&max98504->brownout_timed_hold);
		of_property_read_u32(node, "maxim,brownout-release-rate-ms",
					&max98504->brownout_release_rate);
	}

	max98504->regmap = devm_regmap_init_i2c(client, &max98504_regmap);
	if (IS_ERR(max98504->regmap)) {
		ret = PTR_ERR(max98504->regmap);
		dev_err(&client->dev, "regmap initialization failed: %d\n", ret);
		return ret;
	}

	for (i = 0; i < MAX98504_NUM_SUPPLIES; i++)
		max98504->supplies[i].supply = max98504_supply_names[i];

	ret = devm_regulator_bulk_get(dev, MAX98504_NUM_SUPPLIES,
				      max98504->supplies);
	if (ret < 0)
		return ret;

	i2c_set_clientdata(client, max98504);

	return devm_snd_soc_register_component(dev, &max98504_component_driver,
				max98504_dai, ARRAY_SIZE(max98504_dai));
}

#ifdef CONFIG_OF
static const struct of_device_id max98504_of_match[] = {
	{ .compatible = "maxim,max98504" },
	{ },
};
MODULE_DEVICE_TABLE(of, max98504_of_match);
#endif

static const struct i2c_device_id max98504_i2c_id[] = {
	{ "max98504" },
	{ }
};
MODULE_DEVICE_TABLE(i2c, max98504_i2c_id);

static struct i2c_driver max98504_i2c_driver = {
	.driver = {
		.name = "max98504",
		.of_match_table = of_match_ptr(max98504_of_match),
	},
	.probe_new = max98504_i2c_probe,
	.id_table = max98504_i2c_id,
};
module_i2c_driver(max98504_i2c_driver);

MODULE_DESCRIPTION("ASoC MAX98504 driver");
MODULE_LICENSE("GPL");
