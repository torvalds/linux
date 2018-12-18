// SPDX-License-Identifier: GPL-2.0+
//
// tfa9879.c  --  driver for NXP Semiconductors TFA9879
//
// Copyright (C) 2014 Axentia Technologies AB
// Author: Peter Rosin <peda@axentia.se>

#include <linux/module.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <sound/soc.h>
#include <sound/tlv.h>
#include <sound/pcm_params.h>

#include "tfa9879.h"

struct tfa9879_priv {
	struct regmap *regmap;
	int lsb_justified;
};

static int tfa9879_hw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *params,
			     struct snd_soc_dai *dai)
{
	struct snd_soc_component *component = dai->component;
	struct tfa9879_priv *tfa9879 = snd_soc_component_get_drvdata(component);
	int fs;
	int i2s_set = 0;

	switch (params_rate(params)) {
	case 8000:
		fs = TFA9879_I2S_FS_8000;
		break;
	case 11025:
		fs = TFA9879_I2S_FS_11025;
		break;
	case 12000:
		fs = TFA9879_I2S_FS_12000;
		break;
	case 16000:
		fs = TFA9879_I2S_FS_16000;
		break;
	case 22050:
		fs = TFA9879_I2S_FS_22050;
		break;
	case 24000:
		fs = TFA9879_I2S_FS_24000;
		break;
	case 32000:
		fs = TFA9879_I2S_FS_32000;
		break;
	case 44100:
		fs = TFA9879_I2S_FS_44100;
		break;
	case 48000:
		fs = TFA9879_I2S_FS_48000;
		break;
	case 64000:
		fs = TFA9879_I2S_FS_64000;
		break;
	case 88200:
		fs = TFA9879_I2S_FS_88200;
		break;
	case 96000:
		fs = TFA9879_I2S_FS_96000;
		break;
	default:
		return -EINVAL;
	}

	switch (params_width(params)) {
	case 16:
		i2s_set = TFA9879_I2S_SET_LSB_J_16;
		break;
	case 24:
		i2s_set = TFA9879_I2S_SET_LSB_J_24;
		break;
	default:
		return -EINVAL;
	}

	if (tfa9879->lsb_justified)
		snd_soc_component_update_bits(component,
					      TFA9879_SERIAL_INTERFACE_1,
					      TFA9879_I2S_SET_MASK,
					      i2s_set << TFA9879_I2S_SET_SHIFT);

	snd_soc_component_update_bits(component, TFA9879_SERIAL_INTERFACE_1,
				      TFA9879_I2S_FS_MASK,
				      fs << TFA9879_I2S_FS_SHIFT);
	return 0;
}

static int tfa9879_digital_mute(struct snd_soc_dai *dai, int mute)
{
	struct snd_soc_component *component = dai->component;

	snd_soc_component_update_bits(component, TFA9879_MISC_CONTROL,
				      TFA9879_S_MUTE_MASK,
				      !!mute << TFA9879_S_MUTE_SHIFT);

	return 0;
}

static int tfa9879_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	struct snd_soc_component *component = dai->component;
	struct tfa9879_priv *tfa9879 = snd_soc_component_get_drvdata(component);
	int i2s_set;
	int sck_pol;

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBS_CFS:
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		sck_pol = TFA9879_SCK_POL_NORMAL;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		sck_pol = TFA9879_SCK_POL_INVERSE;
		break;
	default:
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		tfa9879->lsb_justified = 0;
		i2s_set = TFA9879_I2S_SET_I2S_24;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		tfa9879->lsb_justified = 0;
		i2s_set = TFA9879_I2S_SET_MSB_J_24;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		tfa9879->lsb_justified = 1;
		i2s_set = TFA9879_I2S_SET_LSB_J_24;
		break;
	default:
		return -EINVAL;
	}

	snd_soc_component_update_bits(component, TFA9879_SERIAL_INTERFACE_1,
				      TFA9879_SCK_POL_MASK,
				      sck_pol << TFA9879_SCK_POL_SHIFT);
	snd_soc_component_update_bits(component, TFA9879_SERIAL_INTERFACE_1,
				      TFA9879_I2S_SET_MASK,
				      i2s_set << TFA9879_I2S_SET_SHIFT);
	return 0;
}

static const struct reg_default tfa9879_regs[] = {
	{ TFA9879_DEVICE_CONTROL,	0x0000 }, /* 0x00 */
	{ TFA9879_SERIAL_INTERFACE_1,	0x0a18 }, /* 0x01 */
	{ TFA9879_PCM_IOM2_FORMAT_1,	0x0007 }, /* 0x02 */
	{ TFA9879_SERIAL_INTERFACE_2,	0x0a18 }, /* 0x03 */
	{ TFA9879_PCM_IOM2_FORMAT_2,	0x0007 }, /* 0x04 */
	{ TFA9879_EQUALIZER_A1,		0x59dd }, /* 0x05 */
	{ TFA9879_EQUALIZER_A2,		0xc63e }, /* 0x06 */
	{ TFA9879_EQUALIZER_B1,		0x651a }, /* 0x07 */
	{ TFA9879_EQUALIZER_B2,		0xe53e }, /* 0x08 */
	{ TFA9879_EQUALIZER_C1,		0x4616 }, /* 0x09 */
	{ TFA9879_EQUALIZER_C2,		0xd33e }, /* 0x0a */
	{ TFA9879_EQUALIZER_D1,		0x4df3 }, /* 0x0b */
	{ TFA9879_EQUALIZER_D2,		0xea3e }, /* 0x0c */
	{ TFA9879_EQUALIZER_E1,		0x5ee0 }, /* 0x0d */
	{ TFA9879_EQUALIZER_E2,		0xf93e }, /* 0x0e */
	{ TFA9879_BYPASS_CONTROL,	0x0093 }, /* 0x0f */
	{ TFA9879_DYNAMIC_RANGE_COMPR,	0x92ba }, /* 0x10 */
	{ TFA9879_BASS_TREBLE,		0x12a5 }, /* 0x11 */
	{ TFA9879_HIGH_PASS_FILTER,	0x0004 }, /* 0x12 */
	{ TFA9879_VOLUME_CONTROL,	0x10bd }, /* 0x13 */
	{ TFA9879_MISC_CONTROL,		0x0000 }, /* 0x14 */
};

static bool tfa9879_volatile_reg(struct device *dev, unsigned int reg)
{
	return reg == TFA9879_MISC_STATUS;
}

static const DECLARE_TLV_DB_SCALE(volume_tlv, -7050, 50, 1);
static const DECLARE_TLV_DB_SCALE(tb_gain_tlv, -1800, 200, 0);
static const char * const tb_freq_text[] = {
	"Low", "Mid", "High"
};
static const struct soc_enum treble_freq_enum =
	SOC_ENUM_SINGLE(TFA9879_BASS_TREBLE, TFA9879_F_TRBLE_SHIFT,
			ARRAY_SIZE(tb_freq_text), tb_freq_text);
static const struct soc_enum bass_freq_enum =
	SOC_ENUM_SINGLE(TFA9879_BASS_TREBLE, TFA9879_F_BASS_SHIFT,
			ARRAY_SIZE(tb_freq_text), tb_freq_text);

static const struct snd_kcontrol_new tfa9879_controls[] = {
	SOC_SINGLE_TLV("PCM Playback Volume", TFA9879_VOLUME_CONTROL,
		       TFA9879_VOL_SHIFT, 0xbd, 1, volume_tlv),
	SOC_SINGLE_TLV("Treble Volume", TFA9879_BASS_TREBLE,
		       TFA9879_G_TRBLE_SHIFT, 18, 0, tb_gain_tlv),
	SOC_SINGLE_TLV("Bass Volume", TFA9879_BASS_TREBLE,
		       TFA9879_G_BASS_SHIFT, 18, 0, tb_gain_tlv),
	SOC_ENUM("Treble Corner Freq", treble_freq_enum),
	SOC_ENUM("Bass Corner Freq", bass_freq_enum),
};

static const struct snd_soc_dapm_widget tfa9879_dapm_widgets[] = {
SND_SOC_DAPM_AIF_IN("AIFINL", "Playback", 0, SND_SOC_NOPM, 0, 0),
SND_SOC_DAPM_AIF_IN("AIFINR", "Playback", 1, SND_SOC_NOPM, 0, 0),
SND_SOC_DAPM_DAC("DAC", NULL, TFA9879_DEVICE_CONTROL, TFA9879_OPMODE_SHIFT, 0),
SND_SOC_DAPM_OUTPUT("LINEOUT"),
SND_SOC_DAPM_SUPPLY("POWER", TFA9879_DEVICE_CONTROL, TFA9879_POWERUP_SHIFT, 0,
		    NULL, 0),
};

static const struct snd_soc_dapm_route tfa9879_dapm_routes[] = {
	{ "DAC", NULL, "AIFINL" },
	{ "DAC", NULL, "AIFINR" },

	{ "LINEOUT", NULL, "DAC" },

	{ "DAC", NULL, "POWER" },
};

static const struct snd_soc_component_driver tfa9879_component = {
	.controls		= tfa9879_controls,
	.num_controls		= ARRAY_SIZE(tfa9879_controls),
	.dapm_widgets		= tfa9879_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(tfa9879_dapm_widgets),
	.dapm_routes		= tfa9879_dapm_routes,
	.num_dapm_routes	= ARRAY_SIZE(tfa9879_dapm_routes),
	.idle_bias_on		= 1,
	.use_pmdown_time	= 1,
	.endianness		= 1,
	.non_legacy_dai_naming	= 1,
};

static const struct regmap_config tfa9879_regmap = {
	.reg_bits = 8,
	.val_bits = 16,

	.volatile_reg = tfa9879_volatile_reg,
	.max_register = TFA9879_MISC_STATUS,
	.reg_defaults = tfa9879_regs,
	.num_reg_defaults = ARRAY_SIZE(tfa9879_regs),
	.cache_type = REGCACHE_RBTREE,
};

static const struct snd_soc_dai_ops tfa9879_dai_ops = {
	.hw_params = tfa9879_hw_params,
	.digital_mute = tfa9879_digital_mute,
	.set_fmt = tfa9879_set_fmt,
};

#define TFA9879_RATES SNDRV_PCM_RATE_8000_96000

#define TFA9879_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | \
			 SNDRV_PCM_FMTBIT_S24_LE)

static struct snd_soc_dai_driver tfa9879_dai = {
	.name = "tfa9879-hifi",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 2,
		.channels_max = 2,
		.rates = TFA9879_RATES,
		.formats = TFA9879_FORMATS, },
	.ops = &tfa9879_dai_ops,
};

static int tfa9879_i2c_probe(struct i2c_client *i2c)
{
	struct tfa9879_priv *tfa9879;
	int i;

	tfa9879 = devm_kzalloc(&i2c->dev, sizeof(*tfa9879), GFP_KERNEL);
	if (!tfa9879)
		return -ENOMEM;

	i2c_set_clientdata(i2c, tfa9879);

	tfa9879->regmap = devm_regmap_init_i2c(i2c, &tfa9879_regmap);
	if (IS_ERR(tfa9879->regmap))
		return PTR_ERR(tfa9879->regmap);

	/* Ensure the device is in reset state */
	for (i = 0; i < ARRAY_SIZE(tfa9879_regs); i++)
		regmap_write(tfa9879->regmap,
			     tfa9879_regs[i].reg, tfa9879_regs[i].def);

	return devm_snd_soc_register_component(&i2c->dev, &tfa9879_component,
					       &tfa9879_dai, 1);
}

static const struct i2c_device_id tfa9879_i2c_id[] = {
	{ "tfa9879", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, tfa9879_i2c_id);

static const struct of_device_id tfa9879_of_match[] = {
	{ .compatible = "nxp,tfa9879", },
	{ }
};
MODULE_DEVICE_TABLE(of, tfa9879_of_match);

static struct i2c_driver tfa9879_i2c_driver = {
	.driver = {
		.name = "tfa9879",
		.of_match_table = tfa9879_of_match,
	},
	.probe_new = tfa9879_i2c_probe,
	.id_table = tfa9879_i2c_id,
};

module_i2c_driver(tfa9879_i2c_driver);

MODULE_DESCRIPTION("ASoC NXP Semiconductors TFA9879 driver");
MODULE_AUTHOR("Peter Rosin <peda@axentia.se>");
MODULE_LICENSE("GPL");
