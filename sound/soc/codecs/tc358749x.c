/*
 * tc358749x.c TC358749XBG ALSA SoC audio codec driver
 *
 * Copyright (c) 2016 Fuzhou Rockchip Electronics Co., Ltd
 * Author: Roy <luoxiaotan@rock-chips.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.*
 *
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/of_gpio.h>
#include <linux/regmap.h>
#include <sound/soc.h>

#include "tc358749x.h"

static int snd_tc358749x_dai_hw_params(struct snd_pcm_substream *substream,
				       struct snd_pcm_hw_params *params,
				       struct snd_soc_dai *codec_dai)
{
	struct snd_soc_codec *codec = codec_dai->codec;
	unsigned int fs;

	switch (params_rate(params)) {
	case 32000:
		fs = FS_32000;
		break;
	case 44100:
		fs = FS_44100;
		break;
	case 48000:
		fs = FS_48000;
		break;
	case 88200:
		fs = FS_88200;
		break;
	case 96000:
		fs = FS_96000;
		break;
	case 176400:
		fs = FS_176400;
		break;
	case 192000:
		fs = FS_192000;
		break;
	default:
		dev_err(codec->dev, "Enter:%s, %d, Error rate=%d\n",
			__func__, __LINE__, params_rate(params));
		return -EINVAL;
	}
	snd_soc_update_bits(codec, TC358749X_FS_SET, FS_SET_MASK, fs);
	return 0;
}

static int snd_tc358749x_mute(struct snd_soc_dai *dai, int mute)
{
	struct snd_soc_codec *codec = dai->codec;

	if (mute)
		snd_soc_update_bits(codec, TC358749X_FORCE_MUTE,
				    FORCE_DMUTE_MASK, MUTE);
	else
		snd_soc_update_bits(codec, TC358749X_FORCE_MUTE,
				    FORCE_DMUTE_MASK, !MUTE);

	return 0;
}

static const struct snd_soc_dai_ops tc358749x_dai_ops = {
	.hw_params = snd_tc358749x_dai_hw_params,
	.digital_mute = snd_tc358749x_mute,
};

static struct snd_soc_dai_driver tc358749x_dai = {
	.name = "tc358749x-audio",
	.playback = {
		.stream_name = "Playback",
		.channels_min = 2,
		.channels_max = 8,
		.rates = SNDRV_PCM_RATE_32000 |
			 SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000 |
			 SNDRV_PCM_RATE_88200 | SNDRV_PCM_RATE_96000 |
			 SNDRV_PCM_RATE_176400 | SNDRV_PCM_RATE_192000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE,
	},

	.capture = {
		.stream_name = "Capture",
		.channels_min = 2,
		.channels_max = 8,
		.rates = SNDRV_PCM_RATE_32000 |
			 SNDRV_PCM_RATE_44100 | SNDRV_PCM_RATE_48000 |
			 SNDRV_PCM_RATE_88200 | SNDRV_PCM_RATE_96000 |
			 SNDRV_PCM_RATE_176400 | SNDRV_PCM_RATE_192000,
		.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE,
	},
	.ops = &tc358749x_dai_ops,
};

static int tc358749x_probe(struct snd_soc_codec *codec)
{
	snd_soc_codec_force_bias_level(codec, SND_SOC_BIAS_OFF);
	return 0;
}

static int tc358749_set_bias_level(struct snd_soc_codec *codec,
				   enum snd_soc_bias_level level)
{
	switch (level) {
	case SND_SOC_BIAS_ON:
		break;

	case SND_SOC_BIAS_PREPARE:
		snd_soc_update_bits(codec, TC358749X_FORCE_MUTE,
				    FORCE_DMUTE_MASK, !MUTE);
		break;

	case SND_SOC_BIAS_STANDBY:
		break;

	case SND_SOC_BIAS_OFF:
		snd_soc_update_bits(codec, TC358749X_FORCE_MUTE,
				    FORCE_DMUTE_MASK, MUTE);
		break;
	}

	return 0;
}

static struct snd_soc_codec_driver soc_codec_dev_tc358749x = {
	.probe = tc358749x_probe,
	.set_bias_level = tc358749_set_bias_level,
};

static bool tc358749x_readable_register(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case TC358749X_FORCE_MUTE:
	case TC358749X_FS_SET:
		return true;
	default:
		return false;
	}
}

static const struct reg_default tc358749x_reg_defaults[] = {
	{ TC358749X_FORCE_MUTE, 0xb1 },
	{ TC358749X_FS_SET, 0x00 },
};

const struct regmap_config tc358749x_regmap_config = {
	.reg_bits	= 16,
	.val_bits	= 8,
	.max_register	= TC358749X_FS_SET,
	.cache_type	= REGCACHE_RBTREE,
	.reg_defaults = tc358749x_reg_defaults,
	.num_reg_defaults = ARRAY_SIZE(tc358749x_reg_defaults),
	.readable_reg = tc358749x_readable_register,
};

static const struct i2c_device_id tc358749x_i2c_id[] = {
	{ "tc358749x", 0 },
	{ }
};

MODULE_DEVICE_TABLE(i2c, tc358749x_i2c_id);

static int tc358749x_parse_dts(struct i2c_client *i2c,
			       struct tc358749x_priv *tc358749x)
{
	int ret = 0;
	struct device *dev = &i2c->dev;

	tc358749x->gpio_power18 = devm_gpiod_get_optional(dev, "power18",
							GPIOD_OUT_LOW);

	tc358749x->gpio_power = devm_gpiod_get_optional(dev, "power",
							GPIOD_OUT_LOW);
	if (IS_ERR(tc358749x->gpio_power)) {
		ret = PTR_ERR(tc358749x->gpio_power);
		dev_err(&i2c->dev, "Unable to claim gpio \"power\".\n");
		return ret;
	}

	tc358749x->gpio_power33 = devm_gpiod_get_optional(dev, "power33",
							GPIOD_OUT_LOW);

	tc358749x->gpio_int = devm_gpiod_get_optional(dev, "int",
							GPIOD_OUT_LOW);
	if (IS_ERR(tc358749x->gpio_int)) {
		ret = PTR_ERR(tc358749x->gpio_int);
		dev_err(&i2c->dev, "Unable to claim gpio \"int\".\n");
		return ret;
	}

	tc358749x->gpio_reset = devm_gpiod_get_optional(dev, "reset",
							GPIOD_OUT_LOW);
	if (IS_ERR(tc358749x->gpio_reset)) {
		ret = PTR_ERR(tc358749x->gpio_reset);
		dev_err(&i2c->dev, "Unable to claim gpio \"reset\".\n");
		return ret;
	}

	tc358749x->gpio_csi_ctl = devm_gpiod_get_optional(dev, "csi-ctl",
							  GPIOD_OUT_LOW);
	if (IS_ERR(tc358749x->gpio_csi_ctl)) {
		ret = PTR_ERR(tc358749x->gpio_csi_ctl);
		dev_err(&i2c->dev, "Unable to claim gpio \"csi-ctl\".\n");
	}

	tc358749x->gpio_stanby = devm_gpiod_get_optional(dev, "stanby",
							 GPIOD_OUT_LOW);
	if (IS_ERR(tc358749x->gpio_stanby)) {
		ret = PTR_ERR(tc358749x->gpio_stanby);
		dev_err(&i2c->dev, "Unable to claim gpio \"stanby\".\n");
		return ret;
	}

	if (IS_ERR(tc358749x->gpio_power18)) {
		ret = PTR_ERR(tc358749x->gpio_power18);
		dev_err(&i2c->dev, "Unable to claim gpio \"power18\".\n");
	} else {
		gpiod_direction_output(tc358749x->gpio_power18, 1);
	}

	if (IS_ERR(tc358749x->gpio_power33)) {
		ret = PTR_ERR(tc358749x->gpio_power33);
		dev_err(&i2c->dev, "Unable to claim gpio \"power33\".\n");
	} else {
		gpiod_direction_output(tc358749x->gpio_power33, 1);
	}

	gpiod_direction_output(tc358749x->gpio_power, 1);
	gpiod_direction_output(tc358749x->gpio_stanby, 1);
	gpiod_direction_output(tc358749x->gpio_reset, 1);

	/* Wait 10ms tc358749x lock I2C Slave address */
	usleep_range(10000, 11000);
	/* after I2C address has been lock and set it input */
	gpiod_direction_input(tc358749x->gpio_int);
	return 0;
}

static int tc358749x_i2c_probe(struct i2c_client *i2c,
			       const struct i2c_device_id *id)
{
	struct tc358749x_priv *tc358749x;
	int ret;

	tc358749x = devm_kzalloc(&i2c->dev, sizeof(*tc358749x),
				 GFP_KERNEL);
	if (!tc358749x)
		return -ENOMEM;

	i2c_set_clientdata(i2c, tc358749x);
	tc358749x_parse_dts(i2c, tc358749x);

	tc358749x->regmap = devm_regmap_init_i2c(i2c, &tc358749x_regmap_config);
	if (IS_ERR(tc358749x->regmap)) {
		ret = PTR_ERR(tc358749x->regmap);
		dev_err(&i2c->dev, "Failed to init regmap: %d\n", ret);
		return ret;
	}

	ret = snd_soc_register_codec(&i2c->dev, &soc_codec_dev_tc358749x,
				     &tc358749x_dai, 1);

	dev_info(&i2c->dev, "%s success\n", __func__);
	return ret;
}

static int tc358749x_i2c_remove(struct i2c_client *i2c)
{
	snd_soc_unregister_codec(&i2c->dev);

	return 0;
}

static struct i2c_driver tc358749x_i2c_driver = {
	.driver = {
		.name = "tc358749x",
	},
	.probe = tc358749x_i2c_probe,
	.remove   = tc358749x_i2c_remove,
	.id_table = tc358749x_i2c_id,
};
module_i2c_driver(tc358749x_i2c_driver);

MODULE_AUTHOR("Roy <luoxiaotan@rock-chips.com>");
MODULE_DESCRIPTION("TC358749X HDMI Audio RX ASoC Interface");
MODULE_LICENSE("GPL");
