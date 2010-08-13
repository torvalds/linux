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

#include <linux/i2c.h>
#include <linux/spi/spi.h>
#include <sound/soc.h>

static unsigned int snd_soc_4_12_read(struct snd_soc_codec *codec,
				     unsigned int reg)
{
	u16 *cache = codec->reg_cache;
	if (reg >= codec->reg_cache_size)
		return -1;
	return cache[reg];
}

static int snd_soc_4_12_write(struct snd_soc_codec *codec, unsigned int reg,
			     unsigned int value)
{
	u16 *cache = codec->reg_cache;
	u8 data[2];
	int ret;

	BUG_ON(codec->volatile_register);

	data[0] = (reg << 4) | ((value >> 8) & 0x000f);
	data[1] = value & 0x00ff;

	if (reg < codec->reg_cache_size)
		cache[reg] = value;

	if (codec->cache_only) {
		codec->cache_sync = 1;
		return 0;
	}

	dev_dbg(codec->dev, "0x%x = 0x%x\n", reg, value);

	ret = codec->hw_write(codec->control_data, data, 2);
	if (ret == 2)
		return 0;
	if (ret < 0)
		return ret;
	else
		return -EIO;
}

#if defined(CONFIG_SPI_MASTER)
static int snd_soc_4_12_spi_write(void *control_data, const char *data,
				 int len)
{
	struct spi_device *spi = control_data;
	struct spi_transfer t;
	struct spi_message m;
	u8 msg[2];

	if (len <= 0)
		return 0;

	msg[0] = data[1];
	msg[1] = data[0];

	spi_message_init(&m);
	memset(&t, 0, (sizeof t));

	t.tx_buf = &msg[0];
	t.len = len;

	spi_message_add_tail(&t, &m);
	spi_sync(spi, &m);

	return len;
}
#else
#define snd_soc_4_12_spi_write NULL
#endif

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

	if (codec->cache_only) {
		codec->cache_sync = 1;
		return 0;
	}

	dev_dbg(codec->dev, "0x%x = 0x%x\n", reg, value);

	ret = codec->hw_write(codec->control_data, data, 2);
	if (ret == 2)
		return 0;
	if (ret < 0)
		return ret;
	else
		return -EIO;
}

#if defined(CONFIG_SPI_MASTER)
static int snd_soc_7_9_spi_write(void *control_data, const char *data,
				 int len)
{
	struct spi_device *spi = control_data;
	struct spi_transfer t;
	struct spi_message m;
	u8 msg[2];

	if (len <= 0)
		return 0;

	msg[0] = data[0];
	msg[1] = data[1];

	spi_message_init(&m);
	memset(&t, 0, (sizeof t));

	t.tx_buf = &msg[0];
	t.len = len;

	spi_message_add_tail(&t, &m);
	spi_sync(spi, &m);

	return len;
}
#else
#define snd_soc_7_9_spi_write NULL
#endif

static int snd_soc_8_8_write(struct snd_soc_codec *codec, unsigned int reg,
			     unsigned int value)
{
	u8 *cache = codec->reg_cache;
	u8 data[2];

	BUG_ON(codec->volatile_register);

	reg &= 0xff;
	data[0] = reg;
	data[1] = value & 0xff;

	if (reg < codec->reg_cache_size)
		cache[reg] = value;

	if (codec->cache_only) {
		codec->cache_sync = 1;
		return 0;
	}

	dev_dbg(codec->dev, "0x%x = 0x%x\n", reg, value);

	if (codec->hw_write(codec->control_data, data, 2) == 2)
		return 0;
	else
		return -EIO;
}

static unsigned int snd_soc_8_8_read(struct snd_soc_codec *codec,
				     unsigned int reg)
{
	u8 *cache = codec->reg_cache;
	reg &= 0xff;
	if (reg >= codec->reg_cache_size)
		return -1;
	return cache[reg];
}

static int snd_soc_8_16_write(struct snd_soc_codec *codec, unsigned int reg,
			      unsigned int value)
{
	u16 *reg_cache = codec->reg_cache;
	u8 data[3];

	data[0] = reg;
	data[1] = (value >> 8) & 0xff;
	data[2] = value & 0xff;

	if (!snd_soc_codec_volatile_register(codec, reg))
		reg_cache[reg] = value;

	if (codec->cache_only) {
		codec->cache_sync = 1;
		return 0;
	}

	dev_dbg(codec->dev, "0x%x = 0x%x\n", reg, value);

	if (codec->hw_write(codec->control_data, data, 3) == 3)
		return 0;
	else
		return -EIO;
}

static unsigned int snd_soc_8_16_read(struct snd_soc_codec *codec,
				      unsigned int reg)
{
	u16 *cache = codec->reg_cache;

	if (reg >= codec->reg_cache_size ||
	    snd_soc_codec_volatile_register(codec, reg)) {
		if (codec->cache_only)
			return -EINVAL;

		return codec->hw_read(codec, reg);
	} else {
		return cache[reg];
	}
}

#if defined(CONFIG_I2C) || (defined(CONFIG_I2C_MODULE) && defined(MODULE))
static unsigned int snd_soc_8_8_read_i2c(struct snd_soc_codec *codec,
					  unsigned int r)
{
	struct i2c_msg xfer[2];
	u8 reg = r;
	u8 data;
	int ret;
	struct i2c_client *client = codec->control_data;

	/* Write register */
	xfer[0].addr = client->addr;
	xfer[0].flags = 0;
	xfer[0].len = 1;
	xfer[0].buf = &reg;

	/* Read data */
	xfer[1].addr = client->addr;
	xfer[1].flags = I2C_M_RD;
	xfer[1].len = 1;
	xfer[1].buf = &data;

	ret = i2c_transfer(client->adapter, xfer, 2);
	if (ret != 2) {
		dev_err(&client->dev, "i2c_transfer() returned %d\n", ret);
		return 0;
	}

	return data;
}
#else
#define snd_soc_8_8_read_i2c NULL
#endif

#if defined(CONFIG_I2C) || (defined(CONFIG_I2C_MODULE) && defined(MODULE))
static unsigned int snd_soc_8_16_read_i2c(struct snd_soc_codec *codec,
					  unsigned int r)
{
	struct i2c_msg xfer[2];
	u8 reg = r;
	u16 data;
	int ret;
	struct i2c_client *client = codec->control_data;

	/* Write register */
	xfer[0].addr = client->addr;
	xfer[0].flags = 0;
	xfer[0].len = 1;
	xfer[0].buf = &reg;

	/* Read data */
	xfer[1].addr = client->addr;
	xfer[1].flags = I2C_M_RD;
	xfer[1].len = 2;
	xfer[1].buf = (u8 *)&data;

	ret = i2c_transfer(client->adapter, xfer, 2);
	if (ret != 2) {
		dev_err(&client->dev, "i2c_transfer() returned %d\n", ret);
		return 0;
	}

	return (data >> 8) | ((data & 0xff) << 8);
}
#else
#define snd_soc_8_16_read_i2c NULL
#endif

#if defined(CONFIG_I2C) || (defined(CONFIG_I2C_MODULE) && defined(MODULE))
static unsigned int snd_soc_16_8_read_i2c(struct snd_soc_codec *codec,
					  unsigned int r)
{
	struct i2c_msg xfer[2];
	u16 reg = r;
	u8 data;
	int ret;
	struct i2c_client *client = codec->control_data;

	/* Write register */
	xfer[0].addr = client->addr;
	xfer[0].flags = 0;
	xfer[0].len = 2;
	xfer[0].buf = (u8 *)&reg;

	/* Read data */
	xfer[1].addr = client->addr;
	xfer[1].flags = I2C_M_RD;
	xfer[1].len = 1;
	xfer[1].buf = &data;

	ret = i2c_transfer(client->adapter, xfer, 2);
	if (ret != 2) {
		dev_err(&client->dev, "i2c_transfer() returned %d\n", ret);
		return 0;
	}

	return data;
}
#else
#define snd_soc_16_8_read_i2c NULL
#endif

static unsigned int snd_soc_16_8_read(struct snd_soc_codec *codec,
				     unsigned int reg)
{
	u8 *cache = codec->reg_cache;

	reg &= 0xff;
	if (reg >= codec->reg_cache_size)
		return -1;
	return cache[reg];
}

static int snd_soc_16_8_write(struct snd_soc_codec *codec, unsigned int reg,
			     unsigned int value)
{
	u8 *cache = codec->reg_cache;
	u8 data[3];
	int ret;

	BUG_ON(codec->volatile_register);

	data[0] = (reg >> 8) & 0xff;
	data[1] = reg & 0xff;
	data[2] = value;

	reg &= 0xff;
	if (reg < codec->reg_cache_size)
		cache[reg] = value;

	if (codec->cache_only) {
		codec->cache_sync = 1;
		return 0;
	}

	dev_dbg(codec->dev, "0x%x = 0x%x\n", reg, value);

	ret = codec->hw_write(codec->control_data, data, 3);
	if (ret == 3)
		return 0;
	if (ret < 0)
		return ret;
	else
		return -EIO;
}

#if defined(CONFIG_SPI_MASTER)
static int snd_soc_16_8_spi_write(void *control_data, const char *data,
				 int len)
{
	struct spi_device *spi = control_data;
	struct spi_transfer t;
	struct spi_message m;
	u8 msg[3];

	if (len <= 0)
		return 0;

	msg[0] = data[0];
	msg[1] = data[1];
	msg[2] = data[2];

	spi_message_init(&m);
	memset(&t, 0, (sizeof t));

	t.tx_buf = &msg[0];
	t.len = len;

	spi_message_add_tail(&t, &m);
	spi_sync(spi, &m);

	return len;
}
#else
#define snd_soc_16_8_spi_write NULL
#endif

#if defined(CONFIG_I2C) || (defined(CONFIG_I2C_MODULE) && defined(MODULE))
static unsigned int snd_soc_16_16_read_i2c(struct snd_soc_codec *codec,
					   unsigned int r)
{
	struct i2c_msg xfer[2];
	u16 reg = cpu_to_be16(r);
	u16 data;
	int ret;
	struct i2c_client *client = codec->control_data;

	/* Write register */
	xfer[0].addr = client->addr;
	xfer[0].flags = 0;
	xfer[0].len = 2;
	xfer[0].buf = (u8 *)&reg;

	/* Read data */
	xfer[1].addr = client->addr;
	xfer[1].flags = I2C_M_RD;
	xfer[1].len = 2;
	xfer[1].buf = (u8 *)&data;

	ret = i2c_transfer(client->adapter, xfer, 2);
	if (ret != 2) {
		dev_err(&client->dev, "i2c_transfer() returned %d\n", ret);
		return 0;
	}

	return be16_to_cpu(data);
}
#else
#define snd_soc_16_16_read_i2c NULL
#endif

static unsigned int snd_soc_16_16_read(struct snd_soc_codec *codec,
				       unsigned int reg)
{
	u16 *cache = codec->reg_cache;

	if (reg >= codec->reg_cache_size ||
	    snd_soc_codec_volatile_register(codec, reg)) {
		if (codec->cache_only)
			return -EINVAL;

		return codec->hw_read(codec, reg);
	}

	return cache[reg];
}

static int snd_soc_16_16_write(struct snd_soc_codec *codec, unsigned int reg,
			       unsigned int value)
{
	u16 *cache = codec->reg_cache;
	u8 data[4];
	int ret;

	data[0] = (reg >> 8) & 0xff;
	data[1] = reg & 0xff;
	data[2] = (value >> 8) & 0xff;
	data[3] = value & 0xff;

	if (reg < codec->reg_cache_size)
		cache[reg] = value;

	if (codec->cache_only) {
		codec->cache_sync = 1;
		return 0;
	}

	dev_dbg(codec->dev, "0x%x = 0x%x\n", reg, value);

	ret = codec->hw_write(codec->control_data, data, 4);
	if (ret == 4)
		return 0;
	if (ret < 0)
		return ret;
	else
		return -EIO;
}

static struct {
	int addr_bits;
	int data_bits;
	int (*write)(struct snd_soc_codec *codec, unsigned int, unsigned int);
	int (*spi_write)(void *, const char *, int);
	unsigned int (*read)(struct snd_soc_codec *, unsigned int);
	unsigned int (*i2c_read)(struct snd_soc_codec *, unsigned int);
} io_types[] = {
	{
		.addr_bits = 4, .data_bits = 12,
		.write = snd_soc_4_12_write, .read = snd_soc_4_12_read,
		.spi_write = snd_soc_4_12_spi_write,
	},
	{
		.addr_bits = 7, .data_bits = 9,
		.write = snd_soc_7_9_write, .read = snd_soc_7_9_read,
		.spi_write = snd_soc_7_9_spi_write,
	},
	{
		.addr_bits = 8, .data_bits = 8,
		.write = snd_soc_8_8_write, .read = snd_soc_8_8_read,
		.i2c_read = snd_soc_8_8_read_i2c,
	},
	{
		.addr_bits = 8, .data_bits = 16,
		.write = snd_soc_8_16_write, .read = snd_soc_8_16_read,
		.i2c_read = snd_soc_8_16_read_i2c,
	},
	{
		.addr_bits = 16, .data_bits = 8,
		.write = snd_soc_16_8_write, .read = snd_soc_16_8_read,
		.i2c_read = snd_soc_16_8_read_i2c,
		.spi_write = snd_soc_16_8_spi_write,
	},
	{
		.addr_bits = 16, .data_bits = 16,
		.write = snd_soc_16_16_write, .read = snd_soc_16_16_read,
		.i2c_read = snd_soc_16_16_read_i2c,
	},
};

/**
 * snd_soc_codec_set_cache_io: Set up standard I/O functions.
 *
 * @codec: CODEC to configure.
 * @type: Type of cache.
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
	int i;

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

	switch (control) {
	case SND_SOC_CUSTOM:
		break;

	case SND_SOC_I2C:
#if defined(CONFIG_I2C) || (defined(CONFIG_I2C_MODULE) && defined(MODULE))
		codec->hw_write = (hw_write_t)i2c_master_send;
#endif
		if (io_types[i].i2c_read)
			codec->hw_read = io_types[i].i2c_read;
		break;

	case SND_SOC_SPI:
		if (io_types[i].spi_write)
			codec->hw_write = io_types[i].spi_write;
		break;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_codec_set_cache_io);
