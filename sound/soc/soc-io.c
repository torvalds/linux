/*
 * soc-io.c  --  ASoC register I/O helpers
 *
 * Copyright 2009-2011 Wolfson Microelectronics PLC.
 *
 * Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 */

#include <linux/i2c.h>
#include <linux/spi/spi.h>
#include <linux/regmap.h>
#include <linux/export.h>
#include <sound/soc.h>

#include <trace/events/asoc.h>

#ifdef CONFIG_REGMAP
static int hw_write(struct snd_soc_codec *codec, unsigned int reg,
		    unsigned int value)
{
	return regmap_write(codec->control_data, reg, value);
}

static unsigned int hw_read(struct snd_soc_codec *codec, unsigned int reg)
{
	int ret;
	unsigned int val;

	ret = regmap_read(codec->control_data, reg, &val);
	if (ret == 0)
		return val;
	else
		return -1;
}

/**
 * snd_soc_codec_set_cache_io: Set up standard I/O functions.
 *
 * @codec: CODEC to configure.
 * @map: Register map to write to
 *
 * Register formats are frequently shared between many I2C and SPI
 * devices.  In order to promote code reuse the ASoC core provides
 * some standard implementations of CODEC read and write operations
 * which can be set up using this function.
 *
 * The caller is responsible for allocating and initialising the
 * actual cache.
 *
 * Note that at present this code cannot be used by CODECs with
 * volatile registers.
 */
int snd_soc_codec_set_cache_io(struct snd_soc_codec *codec,
			       struct regmap *regmap)
{
	int ret;

	/* Device has made its own regmap arrangements */
	if (!regmap)
		codec->control_data = dev_get_regmap(codec->dev, NULL);
	else
		codec->control_data = regmap;

	if (IS_ERR(codec->control_data))
		return PTR_ERR(codec->control_data);

	codec->write = hw_write;
	codec->read = hw_read;

	ret = regmap_get_val_bytes(codec->control_data);
	/* Errors are legitimate for non-integer byte
	 * multiples */
	if (ret > 0)
		codec->val_bytes = ret;

	codec->using_regmap = true;

	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_codec_set_cache_io);
#else
int snd_soc_codec_set_cache_io(struct snd_soc_codec *codec,
			       struct regmap *regmap)
{
	return -ENOTSUPP;
}
EXPORT_SYMBOL_GPL(snd_soc_codec_set_cache_io);
#endif
