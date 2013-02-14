/*
 * max98090.c -- MAX98090 ALSA SoC Audio driver
 * based on Rev0p8 datasheet
 *
 * Copyright (C) 2012 Renesas Solutions Corp.
 * Kuninori Morimoto <kuninori.morimoto.gx@renesas.com>
 *
 * Based on
 *
 * max98095.c
 * Copyright 2011 Maxim Integrated Products
 *
 * https://github.com/hardkernel/linux/commit/\
 *	3417d7166b17113b3b33b0a337c74d1c7cc313df#sound/soc/codecs/max98090.c
 * Copyright 2011 Maxim Integrated Products
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <sound/soc.h>
#include <sound/tlv.h>

/*
 *
 * MAX98090 Registers Definition
 *
 */

/* RESET / STATUS / INTERRUPT REGISTERS */
#define MAX98090_0x00_SW_RESET		0x00
#define MAX98090_0x01_INT_STS		0x01
#define MAX98090_0x02_JACK_STS		0x02
#define MAX98090_0x03_INT_MASK		0x03

/* QUICK SETUP REGISTERS */
#define MAX98090_0x04_SYS_CLK		0x04
#define MAX98090_0x05_SAMPLE_RATE	0x05
#define MAX98090_0x06_DAI_IF		0x06
#define MAX98090_0x07_DAC_PATH		0x07
#define MAX98090_0x08_MIC_TO_ADC	0x08
#define MAX98090_0x09_LINE_TO_ADC	0x09
#define MAX98090_0x0A_ANALOG_MIC_LOOP	0x0A
#define MAX98090_0x0B_ANALOG_LINE_LOOP	0x0B

/* ANALOG INPUT CONFIGURATION REGISTERS */
#define MAX98090_0x0D_INPUT_CONFIG	0x0D
#define MAX98090_0x0E_LINE_IN_LVL	0x0E
#define MAX98090_0x0F_LINI_IN_CFG	0x0F
#define MAX98090_0x10_MIC1_IN_LVL	0x10
#define MAX98090_0x11_MIC2_IN_LVL	0x11

/* MICROPHONE CONFIGURATION REGISTERS  */
#define MAX98090_0x12_MIC_BIAS_VOL	0x12
#define MAX98090_0x13_DIGITAL_MIC_CFG	0x13
#define MAX98090_0x14_DIGITAL_MIC_MODE	0x14

/* ADC PATH AND CONFIGURATION REGISTERS */
#define MAX98090_0x15_L_ADC_MIX		0x15
#define MAX98090_0x16_R_ADC_MIX		0x16
#define MAX98090_0x17_L_ADC_LVL		0x17
#define MAX98090_0x18_R_ADC_LVL		0x18
#define MAX98090_0x19_ADC_BIQUAD_LVL	0x19
#define MAX98090_0x1A_ADC_SIDETONE	0x1A

/* CLOCK CONFIGURATION REGISTERS */
#define MAX98090_0x1B_SYS_CLK		0x1B
#define MAX98090_0x1C_CLK_MODE		0x1C
#define MAX98090_0x1D_ANY_CLK1		0x1D
#define MAX98090_0x1E_ANY_CLK2		0x1E
#define MAX98090_0x1F_ANY_CLK3		0x1F
#define MAX98090_0x20_ANY_CLK4		0x20
#define MAX98090_0x21_MASTER_MODE	0x21

/* INTERFACE CONTROL REGISTERS */
#define MAX98090_0x22_DAI_IF_FMT	0x22
#define MAX98090_0x23_DAI_TDM_FMT1	0x23
#define MAX98090_0x24_DAI_TDM_FMT2	0x24
#define MAX98090_0x25_DAI_IO_CFG	0x25
#define MAX98090_0x26_FILTER_CFG	0x26
#define MAX98090_0x27_DAI_PLAYBACK_LVL	0x27
#define MAX98090_0x28_EQ_PLAYBACK_LVL	0x28

/* HEADPHONE CONTROL REGISTERS */
#define MAX98090_0x29_L_HP_MIX		0x29
#define MAX98090_0x2A_R_HP_MIX		0x2A
#define MAX98090_0x2B_HP_CTR		0x2B
#define MAX98090_0x2C_L_HP_VOL		0x2C
#define MAX98090_0x2D_R_HP_VOL		0x2D

/* SPEAKER CONFIGURATION REGISTERS */
#define MAX98090_0x2E_L_SPK_MIX		0x2E
#define MAX98090_0x2F_R_SPK_MIX		0x2F
#define MAX98090_0x30_SPK_CTR		0x30
#define MAX98090_0x31_L_SPK_VOL		0x31
#define MAX98090_0x32_R_SPK_VOL		0x32

/* ALC CONFIGURATION REGISTERS */
#define MAX98090_0x33_ALC_TIMING	0x33
#define MAX98090_0x34_ALC_COMPRESSOR	0x34
#define MAX98090_0x35_ALC_EXPANDER	0x35
#define MAX98090_0x36_ALC_GAIN		0x36

/* RECEIVER AND LINE_OUTPUT REGISTERS */
#define MAX98090_0x37_RCV_LOUT_L_MIX	0x37
#define MAX98090_0x38_RCV_LOUT_L_CNTL	0x38
#define MAX98090_0x39_RCV_LOUT_L_VOL	0x39
#define MAX98090_0x3A_LOUT_R_MIX	0x3A
#define MAX98090_0x3B_LOUT_R_CNTL	0x3B
#define MAX98090_0x3C_LOUT_R_VOL	0x3C

/* JACK DETECT AND ENABLE REGISTERS */
#define MAX98090_0x3D_JACK_DETECT	0x3D
#define MAX98090_0x3E_IN_ENABLE		0x3E
#define MAX98090_0x3F_OUT_ENABLE	0x3F
#define MAX98090_0x40_LVL_CTR		0x40
#define MAX98090_0x41_DSP_FILTER_ENABLE	0x41

/* BIAS AND POWER MODE CONFIGURATION REGISTERS */
#define MAX98090_0x42_BIAS_CTR		0x42
#define MAX98090_0x43_DAC_CTR		0x43
#define MAX98090_0x44_ADC_CTR		0x44
#define MAX98090_0x45_DEV_SHUTDOWN	0x45

/* REVISION ID REGISTER */
#define MAX98090_0xFF_REV_ID		0xFF

#define MAX98090_REG_MAX_CACHED		0x45
#define MAX98090_REG_END		0xFF

/*
 *
 * MAX98090 Registers Bit Fields
 *
 */

/* MAX98090_0x06_DAI_IF */
#define MAX98090_DAI_IF_MASK		0x3F
#define MAX98090_RJ_M			(1 << 5)
#define MAX98090_RJ_S			(1 << 4)
#define MAX98090_LJ_M			(1 << 3)
#define MAX98090_LJ_S			(1 << 2)
#define MAX98090_I2S_M			(1 << 1)
#define MAX98090_I2S_S			(1 << 0)

/* MAX98090_0x45_DEV_SHUTDOWN */
#define MAX98090_SHDNRUN		(1 << 7)

/* codec private data */
struct max98090_priv {
	struct regmap *regmap;
};

static const struct reg_default max98090_reg_defaults[] = {
	/* RESET / STATUS / INTERRUPT REGISTERS */
	{MAX98090_0x00_SW_RESET,		0x00},
	{MAX98090_0x01_INT_STS,			0x00},
	{MAX98090_0x02_JACK_STS,		0x00},
	{MAX98090_0x03_INT_MASK,		0x04},

	/* QUICK SETUP REGISTERS */
	{MAX98090_0x04_SYS_CLK,			0x00},
	{MAX98090_0x05_SAMPLE_RATE,		0x00},
	{MAX98090_0x06_DAI_IF,			0x00},
	{MAX98090_0x07_DAC_PATH,		0x00},
	{MAX98090_0x08_MIC_TO_ADC,		0x00},
	{MAX98090_0x09_LINE_TO_ADC,		0x00},
	{MAX98090_0x0A_ANALOG_MIC_LOOP,		0x00},
	{MAX98090_0x0B_ANALOG_LINE_LOOP,	0x00},

	/* ANALOG INPUT CONFIGURATION REGISTERS */
	{MAX98090_0x0D_INPUT_CONFIG,		0x00},
	{MAX98090_0x0E_LINE_IN_LVL,		0x1B},
	{MAX98090_0x0F_LINI_IN_CFG,		0x00},
	{MAX98090_0x10_MIC1_IN_LVL,		0x11},
	{MAX98090_0x11_MIC2_IN_LVL,		0x11},

	/* MICROPHONE CONFIGURATION REGISTERS  */
	{MAX98090_0x12_MIC_BIAS_VOL,		0x00},
	{MAX98090_0x13_DIGITAL_MIC_CFG,		0x00},
	{MAX98090_0x14_DIGITAL_MIC_MODE,	0x00},

	/* ADC PATH AND CONFIGURATION REGISTERS */
	{MAX98090_0x15_L_ADC_MIX,		0x00},
	{MAX98090_0x16_R_ADC_MIX,		0x00},
	{MAX98090_0x17_L_ADC_LVL,		0x03},
	{MAX98090_0x18_R_ADC_LVL,		0x03},
	{MAX98090_0x19_ADC_BIQUAD_LVL,		0x00},
	{MAX98090_0x1A_ADC_SIDETONE,		0x00},

	/* CLOCK CONFIGURATION REGISTERS */
	{MAX98090_0x1B_SYS_CLK,			0x00},
	{MAX98090_0x1C_CLK_MODE,		0x00},
	{MAX98090_0x1D_ANY_CLK1,		0x00},
	{MAX98090_0x1E_ANY_CLK2,		0x00},
	{MAX98090_0x1F_ANY_CLK3,		0x00},
	{MAX98090_0x20_ANY_CLK4,		0x00},
	{MAX98090_0x21_MASTER_MODE,		0x00},

	/* INTERFACE CONTROL REGISTERS */
	{MAX98090_0x22_DAI_IF_FMT,		0x00},
	{MAX98090_0x23_DAI_TDM_FMT1,		0x00},
	{MAX98090_0x24_DAI_TDM_FMT2,		0x00},
	{MAX98090_0x25_DAI_IO_CFG,		0x00},
	{MAX98090_0x26_FILTER_CFG,		0x80},
	{MAX98090_0x27_DAI_PLAYBACK_LVL,	0x00},
	{MAX98090_0x28_EQ_PLAYBACK_LVL,		0x00},

	/* HEADPHONE CONTROL REGISTERS */
	{MAX98090_0x29_L_HP_MIX,		0x00},
	{MAX98090_0x2A_R_HP_MIX,		0x00},
	{MAX98090_0x2B_HP_CTR,			0x00},
	{MAX98090_0x2C_L_HP_VOL,		0x1A},
	{MAX98090_0x2D_R_HP_VOL,		0x1A},

	/* SPEAKER CONFIGURATION REGISTERS */
	{MAX98090_0x2E_L_SPK_MIX,		0x00},
	{MAX98090_0x2F_R_SPK_MIX,		0x00},
	{MAX98090_0x30_SPK_CTR,			0x00},
	{MAX98090_0x31_L_SPK_VOL,		0x2C},
	{MAX98090_0x32_R_SPK_VOL,		0x2C},

	/* ALC CONFIGURATION REGISTERS */
	{MAX98090_0x33_ALC_TIMING,		0x00},
	{MAX98090_0x34_ALC_COMPRESSOR,		0x00},
	{MAX98090_0x35_ALC_EXPANDER,		0x00},
	{MAX98090_0x36_ALC_GAIN,		0x00},

	/* RECEIVER AND LINE_OUTPUT REGISTERS */
	{MAX98090_0x37_RCV_LOUT_L_MIX,		0x00},
	{MAX98090_0x38_RCV_LOUT_L_CNTL,		0x00},
	{MAX98090_0x39_RCV_LOUT_L_VOL,		0x15},
	{MAX98090_0x3A_LOUT_R_MIX,		0x00},
	{MAX98090_0x3B_LOUT_R_CNTL,		0x00},
	{MAX98090_0x3C_LOUT_R_VOL,		0x15},

	/* JACK DETECT AND ENABLE REGISTERS */
	{MAX98090_0x3D_JACK_DETECT,		0x00},
	{MAX98090_0x3E_IN_ENABLE,		0x00},
	{MAX98090_0x3F_OUT_ENABLE,		0x00},
	{MAX98090_0x40_LVL_CTR,			0x00},
	{MAX98090_0x41_DSP_FILTER_ENABLE,	0x00},

	/* BIAS AND POWER MODE CONFIGURATION REGISTERS */
	{MAX98090_0x42_BIAS_CTR,		0x00},
	{MAX98090_0x43_DAC_CTR,			0x00},
	{MAX98090_0x44_ADC_CTR,			0x06},
	{MAX98090_0x45_DEV_SHUTDOWN,		0x00},
};

static const unsigned int max98090_hp_tlv[] = {
	TLV_DB_RANGE_HEAD(5),
	0x0,	0x6,	TLV_DB_SCALE_ITEM(-6700, 400, 0),
	0x7,	0xE,	TLV_DB_SCALE_ITEM(-4000, 300, 0),
	0xF,	0x15,	TLV_DB_SCALE_ITEM(-1700, 200, 0),
	0x16,	0x1B,	TLV_DB_SCALE_ITEM(-400, 100, 0),
	0x1C,	0x1F,	TLV_DB_SCALE_ITEM(150, 50, 0),
};

static struct snd_kcontrol_new max98090_snd_controls[] = {
	SOC_DOUBLE_R_TLV("Headphone Volume", MAX98090_0x2C_L_HP_VOL,
			 MAX98090_0x2D_R_HP_VOL, 0, 31, 0, max98090_hp_tlv),
};

/* Left HeadPhone Mixer Switch */
static struct snd_kcontrol_new max98090_left_hp_mixer_controls[] = {
	SOC_DAPM_SINGLE("DACR Switch", MAX98090_0x29_L_HP_MIX, 1, 1, 0),
	SOC_DAPM_SINGLE("DACL Switch", MAX98090_0x29_L_HP_MIX, 0, 1, 0),
};

/* Right HeadPhone Mixer Switch */
static struct snd_kcontrol_new max98090_right_hp_mixer_controls[] = {
	SOC_DAPM_SINGLE("DACR Switch", MAX98090_0x2A_R_HP_MIX, 1, 1, 0),
	SOC_DAPM_SINGLE("DACL Switch", MAX98090_0x2A_R_HP_MIX, 0, 1, 0),
};

static struct snd_soc_dapm_widget max98090_dapm_widgets[] = {
	/* Output */
	SND_SOC_DAPM_OUTPUT("HPL"),
	SND_SOC_DAPM_OUTPUT("HPR"),

	/* PGA */
	SND_SOC_DAPM_PGA("HPL Out", MAX98090_0x3F_OUT_ENABLE, 7, 0, NULL, 0),
	SND_SOC_DAPM_PGA("HPR Out", MAX98090_0x3F_OUT_ENABLE, 6, 0, NULL, 0),

	/* Mixer */
	SND_SOC_DAPM_MIXER("HPL Mixer", SND_SOC_NOPM, 0, 0,
			   max98090_left_hp_mixer_controls,
			   ARRAY_SIZE(max98090_left_hp_mixer_controls)),

	SND_SOC_DAPM_MIXER("HPR Mixer", SND_SOC_NOPM, 0, 0,
			   max98090_right_hp_mixer_controls,
			   ARRAY_SIZE(max98090_right_hp_mixer_controls)),

	/* DAC */
	SND_SOC_DAPM_DAC("DACL", "Hifi Playback", MAX98090_0x3F_OUT_ENABLE, 0, 0),
	SND_SOC_DAPM_DAC("DACR", "Hifi Playback", MAX98090_0x3F_OUT_ENABLE, 1, 0),
};

static struct snd_soc_dapm_route max98090_audio_map[] = {
	/* Output */
	{"HPL", NULL, "HPL Out"},
	{"HPR", NULL, "HPR Out"},

	/* PGA */
	{"HPL Out", NULL, "HPL Mixer"},
	{"HPR Out", NULL, "HPR Mixer"},

	/* Mixer*/
	{"HPL Mixer", "DACR Switch", "DACR"},
	{"HPL Mixer", "DACL Switch", "DACL"},

	{"HPR Mixer", "DACR Switch", "DACR"},
	{"HPR Mixer", "DACL Switch", "DACL"},
};

static bool max98090_volatile(struct device *dev, unsigned int reg)
{
	if ((reg == MAX98090_0x01_INT_STS)	||
	    (reg == MAX98090_0x02_JACK_STS)	||
	    (reg >  MAX98090_REG_MAX_CACHED))
		return true;

	return false;
}

static int max98090_dai_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params,
		struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	unsigned int val;

	switch (params_rate(params)) {
	case 96000:
		val = 1 << 5;
		break;
	case 32000:
		val = 1 << 4;
		break;
	case 48000:
		val = 1 << 3;
		break;
	case 44100:
		val = 1 << 2;
		break;
	case 16000:
		val = 1 << 1;
		break;
	case 8000:
		val = 1 << 0;
		break;
	default:
		dev_err(codec->dev, "unsupported rate\n");
		return -EINVAL;
	}
	snd_soc_update_bits(codec, MAX98090_0x05_SAMPLE_RATE, 0x03F, val);

	return 0;
}

static int max98090_dai_set_sysclk(struct snd_soc_dai *dai,
		int clk_id, unsigned int freq, int dir)
{
	struct snd_soc_codec *codec = dai->codec;
	unsigned int val;

	snd_soc_update_bits(codec, MAX98090_0x45_DEV_SHUTDOWN,
			    MAX98090_SHDNRUN, 0);

	switch (freq) {
	case 26000000:
		val = 1 << 7;
		break;
	case 19200000:
		val = 1 << 6;
		break;
	case 13000000:
		val = 1 << 5;
		break;
	case 12288000:
		val = 1 << 4;
		break;
	case 12000000:
		val = 1 << 3;
		break;
	case 11289600:
		val = 1 << 2;
		break;
	default:
		dev_err(codec->dev, "Invalid master clock frequency\n");
		return -EINVAL;
	}
	snd_soc_update_bits(codec, MAX98090_0x04_SYS_CLK, 0xFD, val);

	snd_soc_update_bits(codec, MAX98090_0x45_DEV_SHUTDOWN,
			    MAX98090_SHDNRUN, MAX98090_SHDNRUN);

	dev_dbg(dai->dev, "sysclk is %uHz\n", freq);

	return 0;
}

static int max98090_dai_set_fmt(struct snd_soc_dai *dai,
				unsigned int fmt)
{
	struct snd_soc_codec *codec = dai->codec;
	int is_master;
	u8 val;

	/* master/slave mode */
	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		is_master = 1;
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		is_master = 0;
		break;
	default:
		dev_err(codec->dev, "unsupported clock\n");
		return -EINVAL;
	}

	/* format */
	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_I2S:
		val = (is_master) ? MAX98090_I2S_M : MAX98090_I2S_S;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		val = (is_master) ? MAX98090_RJ_M : MAX98090_RJ_S;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		val = (is_master) ? MAX98090_LJ_M : MAX98090_LJ_S;
		break;
	default:
		dev_err(codec->dev, "unsupported format\n");
		return -EINVAL;
	}
	snd_soc_update_bits(codec, MAX98090_0x06_DAI_IF,
			    MAX98090_DAI_IF_MASK, val);

	return 0;
}

#define MAX98090_RATES SNDRV_PCM_RATE_8000_96000
#define MAX98090_FORMATS (SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE)

static struct snd_soc_dai_ops max98090_dai_ops = {
	.set_sysclk	= max98090_dai_set_sysclk,
	.set_fmt	= max98090_dai_set_fmt,
	.hw_params	= max98090_dai_hw_params,
};

static struct snd_soc_dai_driver max98090_dai = {
	.name = "max98090-Hifi",
	.playback = {
		.stream_name	= "Playback",
		.channels_min	= 1,
		.channels_max	= 2,
		.rates		= MAX98090_RATES,
		.formats	= MAX98090_FORMATS,
	},
	.ops = &max98090_dai_ops,
};

static int max98090_probe(struct snd_soc_codec *codec)
{
	struct max98090_priv *priv = snd_soc_codec_get_drvdata(codec);
	struct device *dev = codec->dev;
	int ret;

	codec->control_data = priv->regmap;
	ret = snd_soc_codec_set_cache_io(codec, 8, 8, SND_SOC_REGMAP);
	if (ret < 0) {
		dev_err(dev, "Failed to set cache I/O: %d\n", ret);
		return ret;
	}

	/* Device active */
	snd_soc_update_bits(codec, MAX98090_0x45_DEV_SHUTDOWN,
			    MAX98090_SHDNRUN, MAX98090_SHDNRUN);

	return 0;
}

static int max98090_remove(struct snd_soc_codec *codec)
{
	return 0;
}

static struct snd_soc_codec_driver soc_codec_dev_max98090 = {
	.probe			= max98090_probe,
	.remove			= max98090_remove,
	.controls		= max98090_snd_controls,
	.num_controls		= ARRAY_SIZE(max98090_snd_controls),
	.dapm_widgets		= max98090_dapm_widgets,
	.num_dapm_widgets	= ARRAY_SIZE(max98090_dapm_widgets),
	.dapm_routes		= max98090_audio_map,
	.num_dapm_routes	= ARRAY_SIZE(max98090_audio_map),
};

static const struct regmap_config max98090_regmap = {
	.reg_bits		= 8,
	.val_bits		= 8,
	.max_register		= MAX98090_REG_END,
	.volatile_reg		= max98090_volatile,
	.cache_type		= REGCACHE_RBTREE,
	.reg_defaults		= max98090_reg_defaults,
	.num_reg_defaults	= ARRAY_SIZE(max98090_reg_defaults),
};

static int max98090_i2c_probe(struct i2c_client *i2c,
			      const struct i2c_device_id *id)
{
	struct max98090_priv *priv;
	struct device *dev = &i2c->dev;
	unsigned int val;
	int ret;

	priv = devm_kzalloc(dev, sizeof(struct max98090_priv),
			    GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->regmap = devm_regmap_init_i2c(i2c, &max98090_regmap);
	if (IS_ERR(priv->regmap)) {
		ret = PTR_ERR(priv->regmap);
		dev_err(dev, "Failed to init regmap: %d\n", ret);
		return ret;
	}

	i2c_set_clientdata(i2c, priv);

	ret = regmap_read(priv->regmap, MAX98090_0xFF_REV_ID, &val);
	if (ret < 0) {
		dev_err(dev, "Failed to read device revision: %d\n", ret);
		return ret;
	}
	dev_info(dev, "revision 0x%02x\n", val);

	ret = snd_soc_register_codec(dev,
				     &soc_codec_dev_max98090,
				     &max98090_dai, 1);

	return ret;
}

static int max98090_i2c_remove(struct i2c_client *client)
{
	snd_soc_unregister_codec(&client->dev);
	return 0;
}

static const struct i2c_device_id max98090_i2c_id[] = {
	{ "max98090", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, max98090_i2c_id);

static struct i2c_driver max98090_i2c_driver = {
	.driver = {
		.name = "max98090",
		.owner = THIS_MODULE,
	},
	.probe		= max98090_i2c_probe,
	.remove		= max98090_i2c_remove,
	.id_table	= max98090_i2c_id,
};
module_i2c_driver(max98090_i2c_driver);

MODULE_DESCRIPTION("ALSA SoC MAX98090 driver");
MODULE_AUTHOR("Peter Hsiang, Kuninori Morimoto");
MODULE_LICENSE("GPL");
