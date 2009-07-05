/*
 * soc-cache.c  --  ASoC register cache helpers
 *
 * Copyright 2009 Wolfson Microelectronics PLC.
 *
 * Author: Mark Brown <broonie@opensource.wolfsonmicro.com>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 */

#include <sound/soc.h>

static unsigned int snd_soc_7_9_read(struct snd_soc_codec *codec,
				     unsigned int reg)
{
	u16 *cache = codec->reg_cache;
	if (reg >= codec->reg_cache_size)
		return -1;
	return cache[reg];
}

static int snd_soc_7_9_write(struct snd_soc_codec *codec, unsigned int reg,
			     unsigned int value)
{
	u16 *cache = codec->reg_cache;
	u8 data[2];
	int ret;

	BUG_ON(codec->volatile_register);

	data[0] = (reg << 1) | ((value >> 8) & 0x0001);
	data[1] = value & 0x00ff;

	if (reg < codec->reg_cache_size)
		cache[reg] = value;
	ret = codec->hw_write(codec->control_data, data, 2);
	if (ret == 2)
		return 0;
	if (ret < 0)
		return ret;
	else
		return -EIO;
}


static struct {
	int addr_bits;
	int data_bits;
	int (*write)(struct snd_soc_codec *, unsigned int, unsigned int);
	unsigned int (*read)(struct snd_soc_codec *, unsigned int);
} io_types[] = {
	{ 7, 9, snd_soc_7_9_write, snd_soc_7_9_read },
};

/**
 * snd_soc_codec_set_cache_io: Set up standard I/O functions.
 *
 * @codec: CODEC to configure.
 * @type: Type of cache.
 * @addr_bits: Number of bits of register address data.
 * @data_bits: Number of bits of data per register.
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
			       int addr_bits, int data_bits)
{
	int i;

	/* We don't support volatile registers yet - refactoring of
	 * the hw_read operation will be required to do so. */
	if (codec->volatile_register) {
		printk(KERN_ERR "Volatile registers not yet supported\n");
		return -EINVAL;
	}

	for (i = 0; i < ARRAY_SIZE(io_types); i++)
		if (io_types[i].addr_bits == addr_bits &&
		    io_types[i].data_bits == data_bits)
			break;
	if (i == ARRAY_SIZE(io_types)) {
		printk(KERN_ERR
		       "No I/O functions for %d bit address %d bit data\n",
		       addr_bits, data_bits);
		return -EINVAL;
	}

	codec->write = io_types[i].write;
	codec->read = io_types[i].read;

	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_codec_set_cache_io);
