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

unsigned int snd_soc_read(struct snd_soc_codec *codec, unsigned int reg)
{
	unsigned int ret;

	ret = codec->read(codec, reg);
	dev_dbg(codec->dev, "read %x => %x\n", reg, ret);
	trace_snd_soc_reg_read(codec, reg, ret);

	return ret;
}
EXPORT_SYMBOL_GPL(snd_soc_read);

int snd_soc_write(struct snd_soc_codec *codec, unsigned int reg,
	unsigned int val)
{
	dev_dbg(codec->dev, "write %x = %x\n", reg, val);
	trace_snd_soc_reg_write(codec, reg, val);
	return codec->write(codec, reg, val);
}
EXPORT_SYMBOL_GPL(snd_soc_write);

/**
 * snd_soc_update_bits - update codec register bits
 * @codec: audio codec
 * @reg: codec register
 * @mask: register mask
 * @value: new value
 *
 * Writes new register value.
 *
 * Returns 1 for change, 0 for no change, or negative error code.
 */
int snd_soc_update_bits(struct snd_soc_codec *codec, unsigned int reg,
				unsigned int mask, unsigned int value)
{
	bool change;
	unsigned int old, new;
	int ret;

	if (codec->using_regmap) {
		ret = regmap_update_bits_check(codec->control_data, reg,
					       mask, value, &change);
	} else {
		ret = snd_soc_read(codec, reg);
		if (ret < 0)
			return ret;

		old = ret;
		new = (old & ~mask) | (value & mask);
		change = old != new;
		if (change)
			ret = snd_soc_write(codec, reg, new);
	}

	if (ret < 0)
		return ret;

	return change;
}
EXPORT_SYMBOL_GPL(snd_soc_update_bits);

/**
 * snd_soc_update_bits_locked - update codec register bits
 * @codec: audio codec
 * @reg: codec register
 * @mask: register mask
 * @value: new value
 *
 * Writes new register value, and takes the codec mutex.
 *
 * Returns 1 for change else 0.
 */
int snd_soc_update_bits_locked(struct snd_soc_codec *codec,
			       unsigned int reg, unsigned int mask,
			       unsigned int value)
{
	int change;

	mutex_lock(&codec->mutex);
	change = snd_soc_update_bits(codec, reg, mask, value);
	mutex_unlock(&codec->mutex);

	return change;
}
EXPORT_SYMBOL_GPL(snd_soc_update_bits_locked);

/**
 * snd_soc_test_bits - test register for change
 * @codec: audio codec
 * @reg: codec register
 * @mask: register mask
 * @value: new value
 *
 * Tests a register with a new value and checks if the new value is
 * different from the old value.
 *
 * Returns 1 for change else 0.
 */
int snd_soc_test_bits(struct snd_soc_codec *codec, unsigned int reg,
				unsigned int mask, unsigned int value)
{
	int change;
	unsigned int old, new;

	old = snd_soc_read(codec, reg);
	new = (old & ~mask) | value;
	change = old != new;

	return change;
}
EXPORT_SYMBOL_GPL(snd_soc_test_bits);

int snd_soc_platform_read(struct snd_soc_platform *platform,
					unsigned int reg)
{
	unsigned int ret;

	if (!platform->driver->read) {
		dev_err(platform->dev, "ASoC: platform has no read back\n");
		return -1;
	}

	ret = platform->driver->read(platform, reg);
	dev_dbg(platform->dev, "read %x => %x\n", reg, ret);
	trace_snd_soc_preg_read(platform, reg, ret);

	return ret;
}
EXPORT_SYMBOL_GPL(snd_soc_platform_read);

int snd_soc_platform_write(struct snd_soc_platform *platform,
					 unsigned int reg, unsigned int val)
{
	if (!platform->driver->write) {
		dev_err(platform->dev, "ASoC: platform has no write back\n");
		return -1;
	}

	dev_dbg(platform->dev, "write %x = %x\n", reg, val);
	trace_snd_soc_preg_write(platform, reg, val);
	return platform->driver->write(platform, reg, val);
}
EXPORT_SYMBOL_GPL(snd_soc_platform_write);

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

	if (!regmap)
		return -EINVAL;

	/* Device has made its own regmap arrangements */
	codec->control_data = regmap;

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
