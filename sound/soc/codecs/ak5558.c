// SPDX-License-Identifier: GPL-2.0
//
// Audio driver for AK5558 ADC
//
// Copyright (C) 2015 Asahi Kasei Microdevices Corporation
// Copyright 2018 NXP

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/slab.h>

#include <sound/initval.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/soc-dapm.h>
#include <sound/tlv.h>

#include "ak5558.h"

#define AK5558_NUM_SUPPLIES 2
static const char *ak5558_supply_names[AK5558_NUM_SUPPLIES] = {
	"DVDD",
	"AVDD",
};

/* AK5558 Codec Private Data */
struct ak5558_priv {
	struct regulator_bulk_data supplies[AK5558_NUM_SUPPLIES];
	struct snd_soc_component component;
	struct regmap *regmap;
	struct i2c_client *i2c;
	struct gpio_desc *reset_gpiod; /* Reset & Power down GPIO */
	int slots;
	int slot_width;
};

/* ak5558 register cache & default register settings */
static const struct reg_default ak5558_reg[] = {
	{ 0x0, 0xFF },	/*	0x00	AK5558_00_POWER_MANAGEMENT1	*/
	{ 0x1, 0x01 },	/*	0x01	AK5558_01_POWER_MANAGEMENT2	*/
	{ 0x2, 0x01 },	/*	0x02	AK5558_02_CONTROL1		*/
	{ 0x3, 0x00 },	/*	0x03	AK5558_03_CONTROL2		*/
	{ 0x4, 0x00 },	/*	0x04	AK5558_04_CONTROL3		*/
	{ 0x5, 0x00 }	/*	0x05	AK5558_05_DSD			*/
};

static const char * const mono_texts[] = {
	"8 Slot", "2 Slot", "4 Slot", "1 Slot",
};

static const struct soc_enum ak5558_mono_enum[] = {
	SOC_ENUM_SINGLE(AK5558_01_POWER_MANAGEMENT2, 1,
			ARRAY_SIZE(mono_texts), mono_texts),
};

static const char * const digfil_texts[] = {
	"Sharp Roll-Off", "Show Roll-Off",
	"Short Delay Sharp Roll-Off", "Short Delay Show Roll-Off",
};

static const struct soc_enum ak5558_adcset_enum[] = {
	SOC_ENUM_SINGLE(AK5558_04_CONTROL3, 0,
			ARRAY_SIZE(digfil_texts), digfil_texts),
};

static const struct snd_kcontrol_new ak5558_snd_controls[] = {
	SOC_ENUM("AK5558 Monaural Mode", ak5558_mono_enum[0]),
	SOC_ENUM("AK5558 Digital Filter", ak5558_adcset_enum[0]),
};

static const struct snd_soc_dapm_widget ak5558_dapm_widgets[] = {
	/* Analog Input */
	SND_SOC_DAPM_INPUT("AIN1"),
	SND_SOC_DAPM_INPUT("AIN2"),
	SND_SOC_DAPM_INPUT("AIN3"),
	SND_SOC_DAPM_INPUT("AIN4"),
	SND_SOC_DAPM_INPUT("AIN5"),
	SND_SOC_DAPM_INPUT("AIN6"),
	SND_SOC_DAPM_INPUT("AIN7"),
	SND_SOC_DAPM_INPUT("AIN8"),

	SND_SOC_DAPM_ADC("ADC Ch1", NULL, AK5558_00_POWER_MANAGEMENT1, 0, 0),
	SND_SOC_DAPM_ADC("ADC Ch2", NULL, AK5558_00_POWER_MANAGEMENT1, 1, 0),
	SND_SOC_DAPM_ADC("ADC Ch3", NULL, AK5558_00_POWER_MANAGEMENT1, 2, 0),
	SND_SOC_DAPM_ADC("ADC Ch4", NULL, AK5558_00_POWER_MANAGEMENT1, 3, 0),
	SND_SOC_DAPM_ADC("ADC Ch5", NULL, AK5558_00_POWER_MANAGEMENT1, 4, 0),
	SND_SOC_DAPM_ADC("ADC Ch6", NULL, AK5558_00_POWER_MANAGEMENT1, 5, 0),
	SND_SOC_DAPM_ADC("ADC Ch7", NULL, AK5558_00_POWER_MANAGEMENT1, 6, 0),
	SND_SOC_DAPM_ADC("ADC Ch8", NULL, AK5558_00_POWER_MANAGEMENT1, 7, 0),

	SND_SOC_DAPM_AIF_OUT("SDTO", "Capture", 0, SND_SOC_NOPM, 0, 0),
};

static const struct snd_soc_dapm_route ak5558_intercon[] = {
	{"ADC Ch1", NULL, "AIN1"},
	{"SDTO", NULL, "ADC Ch1"},

	{"ADC Ch2", NULL, "AIN2"},
	{"SDTO", NULL, "ADC Ch2"},

	{"ADC Ch3", NULL, "AIN3"},
	{"SDTO", NULL, "ADC Ch3"},

	{"ADC Ch4", NULL, "AIN4"},
	{"SDTO", NULL, "ADC Ch4"},

	{"ADC Ch5", NULL, "AIN5"},
	{"SDTO", NULL, "ADC Ch5"},

	{"ADC Ch6", NULL, "AIN6"},
	{"SDTO", NULL, "ADC Ch6"},

	{"ADC Ch7", NULL, "AIN7"},
	{"SDTO", NULL, "ADC Ch7"},

	{"ADC Ch8", NULL, "AIN8"},
	{"SDTO", NULL, "ADC Ch8"},
};

static int ak5558_set_mcki(struct snd_soc_component *component)
{
	return snd_soc_component_update_bits(component, AK5558_02_CONTROL1, AK5558_CKS,
				   AK5558_CKS_AUTO);
}

static int ak5558_hw_params(struct snd_pcm_substream *substream,
			    struct snd_pcm_hw_params *params,
			    struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct ak5558_priv *ak5558 = snd_soc_component_get_drvdata(component);
	u8 bits;
	int pcm_width = max(params_physical_width(params), ak5558->slot_width);

	switch (pcm_width) {
	case 16:
		bits = AK5558_DIF_24BIT_MODE;
		break;
	case 32:
		bits = AK5558_DIF_32BIT_MODE;
		break;
	default:
		return -EINVAL;
	}

	snd_soc_component_update_bits(component, AK5558_02_CONTROL1, AK5558_BITS, bits);

	return 0;
}

static int ak5558_set_dai_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_component *component = dai->component;
	u8 format;

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		break;
	case SND_SOC_DAIFMT_CBM_CFM:
		break;
	case SND_SOC_DAIFMT_CBS_CFM:
	case SND_SOC_DAIFMT_CBM_CFS:
	default:
		dev_err(dai->dev, "Clock mode unsupported");
		return -EINVAL;
	}

	/* set master/slave audio interface */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		format = AK5558_DIF_I2S_MODE;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		format = AK5558_DIF_MSB_MODE;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		format = AK5558_DIF_MSB_MODE;
		break;
	default:
		return -EINVAL;
	}

	snd_soc_component_update_bits(component, AK5558_02_CONTROL1, AK5558_DIF, format);

	return 0;
}

static int ak5558_set_tdm_slot(struct snd_soc_dai *dai, unsigned int tx_mask,
			       unsigned int rx_mask, int slots,
			       int slot_width)
{
	struct snd_soc_component *component = dai->component;
	struct ak5558_priv *ak5558 = snd_soc_component_get_drvdata(component);
	int tdm_mode;

	ak5558->slots = slots;
	ak5558->slot_width = slot_width;

	switch (slots * slot_width) {
	case 128:
		tdm_mode = AK5558_MODE_TDM128;
		break;
	case 256:
		tdm_mode = AK5558_MODE_TDM256;
		break;
	case 512:
		tdm_mode = AK5558_MODE_TDM512;
		break;
	default:
		tdm_mode = AK5558_MODE_NORMAL;
		break;
	}

	snd_soc_component_update_bits(component, AK5558_03_CONTROL2, AK5558_MODE_BITS,
			    tdm_mode);
	return 0;
}

#define AK5558_FORMATS	(SNDRV_PCM_FMTBIT_S16_LE |\
			 SNDRV_PCM_FMTBIT_S24_LE |\
			 SNDRV_PCM_FMTBIT_S32_LE)

static const unsigned int ak5558_rates[] = {
	8000, 11025,  16000, 22050,
	32000, 44100, 48000, 88200,
	96000, 176400, 192000, 352800,
	384000, 705600, 768000, 1411200,
	2822400,
};

static const struct snd_pcm_hw_constraint_list ak5558_rate_constraints = {
	.count = ARRAY_SIZE(ak5558_rates),
	.list = ak5558_rates,
};

static int ak5558_startup(struct snd_pcm_substream *substream,
			  struct snd_soc_dai *dai)
{
	return snd_pcm_hw_constraint_list(substream->runtime, 0,
					  SNDRV_PCM_HW_PARAM_RATE,
					  &ak5558_rate_constraints);
}

static const struct snd_soc_dai_ops ak5558_dai_ops = {
	.startup        = ak5558_startup,
	.hw_params	= ak5558_hw_params,

	.set_fmt	= ak5558_set_dai_fmt,
	.set_tdm_slot   = ak5558_set_tdm_slot,
};

static struct snd_soc_dai_driver ak5558_dai = {
	.name = "ak5558-aif",
	.capture = {
		.stream_name = "Capture",
		.channels_min = 1,
		.channels_max = 8,
		.rates = SNDRV_PCM_RATE_KNOT,
		.formats = AK5558_FORMATS,
	},
	.ops = &ak5558_dai_ops,
};

static void ak5558_power_off(struct ak5558_priv *ak5558)
{
	if (!ak5558->reset_gpiod)
		return;

	gpiod_set_value_cansleep(ak5558->reset_gpiod, 0);
	usleep_range(1000, 2000);
}

static void ak5558_power_on(struct ak5558_priv *ak5558)
{
	if (!ak5558->reset_gpiod)
		return;

	gpiod_set_value_cansleep(ak5558->reset_gpiod, 1);
	usleep_range(1000, 2000);
}

static int ak5558_probe(struct snd_soc_component *component)
{
	struct ak5558_priv *ak5558 = snd_soc_component_get_drvdata(component);

	ak5558_power_on(ak5558);
	return ak5558_set_mcki(component);
}

static void ak5558_remove(struct snd_soc_component *component)
{
	struct ak5558_priv *ak5558 = snd_soc_component_get_drvdata(component);

	ak5558_power_off(ak5558);
}

static int __maybe_unused ak5558_runtime_suspend(struct device *dev)
{
	struct ak5558_priv *ak5558 = dev_get_drvdata(dev);

	regcache_cache_only(ak5558->regmap, true);
	ak5558_power_off(ak5558);

	regulator_bulk_disable(ARRAY_SIZE(ak5558->supplies),
			       ak5558->supplies);
	return 0;
}

static int __maybe_unused ak5558_runtime_resume(struct device *dev)
{
	struct ak5558_priv *ak5558 = dev_get_drvdata(dev);
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(ak5558->supplies),
				    ak5558->supplies);
	if (ret != 0) {
		dev_err(dev, "Failed to enable supplies: %d\n", ret);
		return ret;
	}

	ak5558_power_off(ak5558);
	ak5558_power_on(ak5558);

	regcache_cache_only(ak5558->regmap, false);
	regcache_mark_dirty(ak5558->regmap);

	return regcache_sync(ak5558->regmap);
}

static const struct dev_pm_ops ak5558_pm = {
	SET_RUNTIME_PM_OPS(ak5558_runtime_suspend, ak5558_runtime_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
};

static const struct snd_soc_component_driver soc_codec_dev_ak5558 = {
	.probe			= ak5558_probe,
	.remove			= ak5558_remove,
	.controls		= ak5558_snd_controls,
	.num_controls		= ARRAY_SIZE(ak5558_snd_controls),
	.dapm_widgets		= ak5558_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(ak5558_dapm_widgets),
	.dapm_routes		= ak5558_intercon,
	.num_dapm_routes	= ARRAY_SIZE(ak5558_intercon),
	.idle_bias_on		= 1,
	.use_pmdown_time	= 1,
	.endianness		= 1,
	.non_legacy_dai_naming	= 1,
};

static const struct regmap_config ak5558_regmap = {
	.reg_bits = 8,
	.val_bits = 8,

	.max_register = AK5558_05_DSD,
	.reg_defaults = ak5558_reg,
	.num_reg_defaults = ARRAY_SIZE(ak5558_reg),
	.cache_type = REGCACHE_RBTREE,
};

static int ak5558_i2c_probe(struct i2c_client *i2c)
{
	struct ak5558_priv *ak5558;
	int ret = 0;
	int i;

	ak5558 = devm_kzalloc(&i2c->dev, sizeof(*ak5558), GFP_KERNEL);
	if (!ak5558)
		return -ENOMEM;

	ak5558->regmap = devm_regmap_init_i2c(i2c, &ak5558_regmap);
	if (IS_ERR(ak5558->regmap))
		return PTR_ERR(ak5558->regmap);

	i2c_set_clientdata(i2c, ak5558);
	ak5558->i2c = i2c;

	ak5558->reset_gpiod = devm_gpiod_get_optional(&i2c->dev, "reset",
						      GPIOD_OUT_LOW);
	if (IS_ERR(ak5558->reset_gpiod))
		return PTR_ERR(ak5558->reset_gpiod);

	for (i = 0; i < ARRAY_SIZE(ak5558->supplies); i++)
		ak5558->supplies[i].supply = ak5558_supply_names[i];

	ret = devm_regulator_bulk_get(&i2c->dev, ARRAY_SIZE(ak5558->supplies),
				      ak5558->supplies);
	if (ret != 0) {
		dev_err(&i2c->dev, "Failed to request supplies: %d\n", ret);
		return ret;
	}

	ret = devm_snd_soc_register_component(&i2c->dev,
				     &soc_codec_dev_ak5558,
				     &ak5558_dai, 1);
	if (ret)
		return ret;

	pm_runtime_enable(&i2c->dev);
	regcache_cache_only(ak5558->regmap, true);

	return 0;
}

static int ak5558_i2c_remove(struct i2c_client *i2c)
{
	pm_runtime_disable(&i2c->dev);

	return 0;
}

static const struct of_device_id ak5558_i2c_dt_ids[] __maybe_unused = {
	{ .compatible = "asahi-kasei,ak5558"},
	{ }
};
MODULE_DEVICE_TABLE(of, ak5558_i2c_dt_ids);

static struct i2c_driver ak5558_i2c_driver = {
	.driver = {
		.name = "ak5558",
		.of_match_table = of_match_ptr(ak5558_i2c_dt_ids),
		.pm = &ak5558_pm,
	},
	.probe_new = ak5558_i2c_probe,
	.remove = ak5558_i2c_remove,
};

module_i2c_driver(ak5558_i2c_driver);

MODULE_AUTHOR("Junichi Wakasugi <wakasugi.jb@om.asahi-kasei.co.jp>");
MODULE_AUTHOR("Mihai Serban <mihai.serban@nxp.com>");
MODULE_DESCRIPTION("ASoC AK5558 ADC driver");
MODULE_LICENSE("GPL v2");
