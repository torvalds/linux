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
	int ret;

	if (!snd_soc_codec_volatile_register(codec, reg) &&
	    reg < codec->driver->reg_cache_size &&
	    !codec->cache_bypass) {
		ret = snd_soc_cache_write(codec, reg, value);
		if (ret < 0)
			return -1;
	}

	if (codec->cache_only) {
		codec->cache_sync = 1;
		return 0;
	}

	return regmap_write(codec->control_data, reg, value);
}

static unsigned int hw_read(struct snd_soc_codec *codec, unsigned int reg)
{
	int ret;
	unsigned int val;

	if (reg >= codec->driver->reg_cache_size ||
	    snd_soc_codec_volatile_register(codec, reg) ||
	    codec->cache_bypass) {
		if (codec->cache_only)
			return -1;

		ret = regmap_read(codec->control_data, reg, &val);
		if (ret == 0)
			return val;
		else
			return -1;
	}

	ret = snd_soc_cache_read(codec, reg, &val);
	if (ret < 0)
		return -1;
	return val;
}

/**
 * snd_soc_codec_set_cache_io: Set up standard I/O functions.
 *
 * @codec: CODEC to configure.
 * @addr_bits: Number of bits of register address data.
 * @data_bits: Number of bits of data per register.
 * @control: Control bus used.
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
			       int addr_bits, int data_bits,
			       enum snd_soc_control_type control)
{
	struct regmap_config config;
	int ret;

	memset(&config, 0, sizeof(config));
	codec->write = hw_write;
	codec->read = hw_read;

	config.reg_bits = addr_bits;
	config.val_bits = data_bits;

	switch (control) {
#if IS_ENABLED(CONFIG_REGMAP_I2C)
	case SND_SOC_I2C:
		codec->control_data = regmap_init_i2c(to_i2c_client(codec->dev),
						      &config);
		break;
#endif

#if IS_ENABLED(CONFIG_REGMAP_SPI)
	case SND_SOC_SPI:
		codec->control_data = regmap_init_spi(to_spi_device(codec->dev),
						      &config);
		break;
#endif

	case SND_SOC_REGMAP:
		/* Device has made its own regmap arrangements */
		codec->using_regmap = true;
		if (!codec->control_data)
			codec->control_data = dev_get_regmap(codec->dev, NULL);

		if (codec->control_data) {
			ret = regmap_get_val_bytes(codec->control_data);
			/* Errors are legitimate for non-integer byte
			 * multiples */
			if (ret > 0)
				codec->val_bytes = ret;
		}
		break;

	default:
		return -EINVAL;
	}

	return PTR_ERR_OR_ZERO(codec->control_data);
}
EXPORT_SYMBOL_GPL(snd_soc_codec_set_cache_io);
#else
int snd_soc_codec_set_cache_io(struct snd_soc_codec *codec,
			       int addr_bits, int data_bits,
			       enum snd_soc_control_type control)
{
	return -ENOTSUPP;
}
EXPORT_SYMBOL_GPL(snd_soc_codec_set_cache_io);
#endif
