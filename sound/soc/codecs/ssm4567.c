/*
 * SSM4567 amplifier audio driver
 *
 * Copyright 2014 Google Chromium project.
 *  Author: Anatol Pomozov <anatol@chromium.org>
 *
 * Based on code copyright/by:
 *   Copyright 2013 Analog Devices Inc.
 *
 * Licensed under the GPL-2.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/initval.h>
#include <sound/tlv.h>

#define SSM4567_REG_POWER_CTRL		0x00
#define SSM4567_REG_AMP_SNS_CTRL		0x01
#define SSM4567_REG_DAC_CTRL		0x02
#define SSM4567_REG_DAC_VOLUME		0x03
#define SSM4567_REG_SAI_CTRL_1		0x04
#define SSM4567_REG_SAI_CTRL_2		0x05
#define SSM4567_REG_SAI_PLACEMENT_1		0x06
#define SSM4567_REG_SAI_PLACEMENT_2		0x07
#define SSM4567_REG_SAI_PLACEMENT_3		0x08
#define SSM4567_REG_SAI_PLACEMENT_4		0x09
#define SSM4567_REG_SAI_PLACEMENT_5		0x0a
#define SSM4567_REG_SAI_PLACEMENT_6		0x0b
#define SSM4567_REG_BATTERY_V_OUT		0x0c
#define SSM4567_REG_LIMITER_CTRL_1		0x0d
#define SSM4567_REG_LIMITER_CTRL_2		0x0e
#define SSM4567_REG_LIMITER_CTRL_3		0x0f
#define SSM4567_REG_STATUS_1		0x10
#define SSM4567_REG_STATUS_2		0x11
#define SSM4567_REG_FAULT_CTRL		0x12
#define SSM4567_REG_PDM_CTRL		0x13
#define SSM4567_REG_MCLK_RATIO		0x14
#define SSM4567_REG_BOOST_CTRL_1		0x15
#define SSM4567_REG_BOOST_CTRL_2		0x16
#define SSM4567_REG_SOFT_RESET		0xff

/* POWER_CTRL */
#define SSM4567_POWER_APWDN_EN		BIT(7)
#define SSM4567_POWER_BSNS_PWDN		BIT(6)
#define SSM4567_POWER_VSNS_PWDN		BIT(5)
#define SSM4567_POWER_ISNS_PWDN		BIT(4)
#define SSM4567_POWER_BOOST_PWDN		BIT(3)
#define SSM4567_POWER_AMP_PWDN		BIT(2)
#define SSM4567_POWER_VBAT_ONLY		BIT(1)
#define SSM4567_POWER_SPWDN			BIT(0)

/* DAC_CTRL */
#define SSM4567_DAC_HV			BIT(7)
#define SSM4567_DAC_MUTE		BIT(6)
#define SSM4567_DAC_HPF			BIT(5)
#define SSM4567_DAC_LPM			BIT(4)
#define SSM4567_DAC_FS_MASK	0x7
#define SSM4567_DAC_FS_8000_12000	0x0
#define SSM4567_DAC_FS_16000_24000	0x1
#define SSM4567_DAC_FS_32000_48000	0x2
#define SSM4567_DAC_FS_64000_96000	0x3
#define SSM4567_DAC_FS_128000_192000	0x4

struct ssm4567 {
	struct regmap *regmap;
};

static const struct reg_default ssm4567_reg_defaults[] = {
	{ SSM4567_REG_POWER_CTRL,	0x81 },
	{ SSM4567_REG_AMP_SNS_CTRL, 0x09 },
	{ SSM4567_REG_DAC_CTRL, 0x32 },
	{ SSM4567_REG_DAC_VOLUME, 0x40 },
	{ SSM4567_REG_SAI_CTRL_1, 0x00 },
	{ SSM4567_REG_SAI_CTRL_2, 0x08 },
	{ SSM4567_REG_SAI_PLACEMENT_1, 0x01 },
	{ SSM4567_REG_SAI_PLACEMENT_2, 0x20 },
	{ SSM4567_REG_SAI_PLACEMENT_3, 0x32 },
	{ SSM4567_REG_SAI_PLACEMENT_4, 0x07 },
	{ SSM4567_REG_SAI_PLACEMENT_5, 0x07 },
	{ SSM4567_REG_SAI_PLACEMENT_6, 0x07 },
	{ SSM4567_REG_BATTERY_V_OUT, 0x00 },
	{ SSM4567_REG_LIMITER_CTRL_1, 0xa4 },
	{ SSM4567_REG_LIMITER_CTRL_2, 0x73 },
	{ SSM4567_REG_LIMITER_CTRL_3, 0x00 },
	{ SSM4567_REG_STATUS_1, 0x00 },
	{ SSM4567_REG_STATUS_2, 0x00 },
	{ SSM4567_REG_FAULT_CTRL, 0x30 },
	{ SSM4567_REG_PDM_CTRL, 0x40 },
	{ SSM4567_REG_MCLK_RATIO, 0x11 },
	{ SSM4567_REG_BOOST_CTRL_1, 0x03 },
	{ SSM4567_REG_BOOST_CTRL_2, 0x00 },
	{ SSM4567_REG_SOFT_RESET, 0x00 },
};


static bool ssm4567_readable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case SSM4567_REG_POWER_CTRL ... SSM4567_REG_BOOST_CTRL_2:
		return true;
	default:
		return false;
	}

}

static bool ssm4567_writeable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case SSM4567_REG_POWER_CTRL ... SSM4567_REG_SAI_PLACEMENT_6:
	case SSM4567_REG_LIMITER_CTRL_1 ... SSM4567_REG_LIMITER_CTRL_3:
	case SSM4567_REG_FAULT_CTRL ... SSM4567_REG_BOOST_CTRL_2:
	/* The datasheet states that soft reset register is read-only,
	 * but logically it is write-only. */
	case SSM4567_REG_SOFT_RESET:
		return true;
	default:
		return false;
	}
}

static bool ssm4567_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case SSM4567_REG_BATTERY_V_OUT:
	case SSM4567_REG_STATUS_1 ... SSM4567_REG_STATUS_2:
	case SSM4567_REG_SOFT_RESET:
		return true;
	default:
		return false;
	}
}

static const DECLARE_TLV_DB_MINMAX_MUTE(ssm4567_vol_tlv, -7125, 2400);

static const struct snd_kcontrol_new ssm4567_snd_controls[] = {
	SOC_SINGLE_TLV("Master Playback Volume", SSM4567_REG_DAC_VOLUME, 0,
		0xff, 1, ssm4567_vol_tlv),
	SOC_SINGLE("DAC Low Power Mode Switch", SSM4567_REG_DAC_CTRL, 4, 1, 0),
};

static const struct snd_soc_dapm_widget ssm4567_dapm_widgets[] = {
	SND_SOC_DAPM_DAC("DAC", "HiFi Playback", SSM4567_REG_POWER_CTRL, 2, 1),

	SND_SOC_DAPM_OUTPUT("OUT"),
};

static const struct snd_soc_dapm_route ssm4567_routes[] = {
	{ "OUT", NULL, "DAC" },
};

static int ssm4567_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct ssm4567 *ssm4567 = snd_soc_codec_get_drvdata(codec);
	unsigned int rate = params_rate(params);
	unsigned int dacfs;

	if (rate >= 8000 && rate <= 12000)
		dacfs = SSM4567_DAC_FS_8000_12000;
	else if (rate >= 16000 && rate <= 24000)
		dacfs = SSM4567_DAC_FS_16000_24000;
	else if (rate >= 32000 && rate <= 48000)
		dacfs = SSM4567_DAC_FS_32000_48000;
	else if (rate >= 64000 && rate <= 96000)
		dacfs = SSM4567_DAC_FS_64000_96000;
	else if (rate >= 64000 && rate <= 96000)
		dacfs = SSM4567_DAC_FS_64000_96000;
	else if (rate >= 128000 && rate <= 192000)
		dacfs = SSM4567_DAC_FS_128000_192000;
	else
		return -EINVAL;

	return regmap_update_bits(ssm4567->regmap, SSM4567_REG_DAC_CTRL,
				SSM4567_DAC_FS_MASK, dacfs);
}

static int ssm4567_mute(struct snd_soc_dai *dai, int mute)
{
	struct ssm4567 *ssm4567 = snd_soc_codec_get_drvdata(dai->codec);
	unsigned int val;

	val = mute ? SSM4567_DAC_MUTE : 0;
	return regmap_update_bits(ssm4567->regmap, SSM4567_REG_DAC_CTRL,
			SSM4567_DAC_MUTE, val);
}

static int ssm4567_set_power(struct ssm4567 *ssm4567, bool enable)
{
	int ret = 0;

	if (!enable) {
		ret = regmap_update_bits(ssm4567->regmap,
			SSM4567_REG_POWER_CTRL,
			SSM4567_POWER_SPWDN, SSM4567_POWER_SPWDN);
		regcache_mark_dirty(ssm4567->regmap);
	}

	regcache_cache_only(ssm4567->regmap, !enable);

	if (enable) {
		ret = regmap_update_bits(ssm4567->regmap,
			SSM4567_REG_POWER_CTRL,
			SSM4567_POWER_SPWDN, 0x00);
		regcache_sync(ssm4567->regmap);
	}

	return ret;
}

static int ssm4567_set_bias_level(struct snd_soc_codec *codec,
	enum snd_soc_bias_level level)
{
	struct ssm4567 *ssm4567 = snd_soc_codec_get_drvdata(codec);
	int ret = 0;

	switch (level) {
	case SND_SOC_BIAS_ON:
		break;
	case SND_SOC_BIAS_PREPARE:
		break;
	case SND_SOC_BIAS_STANDBY:
		if (codec->dapm.bias_level == SND_SOC_BIAS_OFF)
			ret = ssm4567_set_power(ssm4567, true);
		break;
	case SND_SOC_BIAS_OFF:
		ret = ssm4567_set_power(ssm4567, false);
		break;
	}

	if (ret)
		return ret;

	codec->dapm.bias_level = level;

	return 0;
}

static const struct snd_soc_dai_ops ssm4567_dai_ops = {
	.hw_params	= ssm4567_hw_params,
	.digital_mute	= ssm4567_mute,
};

static struct snd_soc_dai_driver ssm4567_dai = {
	.name = "ssm4567-hifi",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 1,
		.channels_max = 1,
		.rates = SNDRV_PCM_RATE_8000_192000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE |
			SNDRV_PCM_FMTBIT_S32,
	},
	.ops = &ssm4567_dai_ops,
};

static struct snd_soc_codec_driver ssm4567_codec_driver = {
	.set_bias_level = ssm4567_set_bias_level,
	.idle_bias_off = true,

	.controls = ssm4567_snd_controls,
	.num_controls = ARRAY_SIZE(ssm4567_snd_controls),
	.dapm_widgets = ssm4567_dapm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(ssm4567_dapm_widgets),
	.dapm_routes = ssm4567_routes,
	.num_dapm_routes = ARRAY_SIZE(ssm4567_routes),
};

static const struct regmap_config ssm4567_regmap_config = {
	.val_bits = 8,
	.reg_bits = 8,

	.max_register = SSM4567_REG_SOFT_RESET,
	.readable_reg = ssm4567_readable_reg,
	.writeable_reg = ssm4567_writeable_reg,
	.volatile_reg = ssm4567_volatile_reg,

	.cache_type = REGCACHE_RBTREE,
	.reg_defaults = ssm4567_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(ssm4567_reg_defaults),
};

static int ssm4567_i2c_probe(struct i2c_client *i2c,
	const struct i2c_device_id *id)
{
	struct ssm4567 *ssm4567;
	int ret;

	ssm4567 = devm_kzalloc(&i2c->dev, sizeof(*ssm4567), GFP_KERNEL);
	if (ssm4567 == NULL)
		return -ENOMEM;

	i2c_set_clientdata(i2c, ssm4567);

	ssm4567->regmap = devm_regmap_init_i2c(i2c, &ssm4567_regmap_config);
	if (IS_ERR(ssm4567->regmap))
		return PTR_ERR(ssm4567->regmap);

	ret = regmap_write(ssm4567->regmap, SSM4567_REG_SOFT_RESET, 0x00);
	if (ret)
		return ret;

	ret = ssm4567_set_power(ssm4567, false);
	if (ret)
		return ret;

	return snd_soc_register_codec(&i2c->dev, &ssm4567_codec_driver,
			&ssm4567_dai, 1);
}

static int ssm4567_i2c_remove(struct i2c_client *client)
{
	snd_soc_unregister_codec(&client->dev);
	return 0;
}

static const struct i2c_device_id ssm4567_i2c_ids[] = {
	{ "ssm4567", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ssm4567_i2c_ids);

static struct i2c_driver ssm4567_driver = {
	.driver = {
		.name = "ssm4567",
		.owner = THIS_MODULE,
	},
	.probe = ssm4567_i2c_probe,
	.remove = ssm4567_i2c_remove,
	.id_table = ssm4567_i2c_ids,
};
module_i2c_driver(ssm4567_driver);

MODULE_DESCRIPTION("ASoC SSM4567 driver");
MODULE_AUTHOR("Anatol Pomozov <anatol@chromium.org>");
MODULE_LICENSE("GPL");
