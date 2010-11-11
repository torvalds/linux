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
	int ret;
	unsigned int val;

	if (reg >= codec->driver->reg_cache_size ||
		snd_soc_codec_volatile_register(codec, reg)) {
			if (codec->cache_only)
				return -1;

			BUG_ON(!codec->hw_read);
			return codec->hw_read(codec, reg);
	}

	ret = snd_soc_cache_read(codec, reg, &val);
	if (ret < 0)
		return -1;
	return val;
}

static int snd_soc_4_12_write(struct snd_soc_codec *codec, unsigned int reg,
			     unsigned int value)
{
	u8 data[2];
	int ret;

	data[0] = (reg << 4) | ((value >> 8) & 0x000f);
	data[1] = value & 0x00ff;

	if (!snd_soc_codec_volatile_register(codec, reg) &&
		reg < codec->driver->reg_cache_size) {
		ret = snd_soc_cache_write(codec, reg, value);
		if (ret < 0)
			return -1;
	}

	if (codec->cache_only) {
		codec->cache_sync = 1;
		return 0;
	}

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
	int ret;
	unsigned int val;

	if (reg >= codec->driver->reg_cache_size ||
		snd_soc_codec_volatile_register(codec, reg)) {
			if (codec->cache_only)
				return -1;

			BUG_ON(!codec->hw_read);
			return codec->hw_read(codec, reg);
	}

	ret = snd_soc_cache_read(codec, reg, &val);
	if (ret < 0)
		return -1;
	return val;
}

static int snd_soc_7_9_write(struct snd_soc_codec *codec, unsigned int reg,
			     unsigned int value)
{
	u8 data[2];
	int ret;

	data[0] = (reg << 1) | ((value >> 8) & 0x0001);
	data[1] = value & 0x00ff;

	if (!snd_soc_codec_volatile_register(codec, reg) &&
		reg < codec->driver->reg_cache_size) {
		ret = snd_soc_cache_write(codec, reg, value);
		if (ret < 0)
			return -1;
	}

	if (codec->cache_only) {
		codec->cache_sync = 1;
		return 0;
	}

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
	u8 data[2];
	int ret;

	reg &= 0xff;
	data[0] = reg;
	data[1] = value & 0xff;

	if (!snd_soc_codec_volatile_register(codec, reg) &&
		reg < codec->driver->reg_cache_size) {
		ret = snd_soc_cache_write(codec, reg, value);
		if (ret < 0)
			return -1;
	}

	if (codec->cache_only) {
		codec->cache_sync = 1;
		return 0;
	}

	if (codec->hw_write(codec->control_data, data, 2) == 2)
		return 0;
	else
		return -EIO;
}

static unsigned int snd_soc_8_8_read(struct snd_soc_codec *codec,
				     unsigned int reg)
{
	int ret;
	unsigned int val;

	reg &= 0xff;
	if (reg >= codec->driver->reg_cache_size ||
		snd_soc_codec_volatile_register(codec, reg)) {
			if (codec->cache_only)
				return -1;

			BUG_ON(!codec->hw_read);
			return codec->hw_read(codec, reg);
	}

	ret = snd_soc_cache_read(codec, reg, &val);
	if (ret < 0)
		return -1;
	return val;
}

#if defined(CONFIG_SPI_MASTER)
static int snd_soc_8_8_spi_write(void *control_data, const char *data,
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
#define snd_soc_8_8_spi_write NULL
#endif

static int snd_soc_8_16_write(struct snd_soc_codec *codec, unsigned int reg,
			      unsigned int value)
{
	u8 data[3];
	int ret;

	data[0] = reg;
	data[1] = (value >> 8) & 0xff;
	data[2] = value & 0xff;

	if (!snd_soc_codec_volatile_register(codec, reg) &&
		reg < codec->driver->reg_cache_size) {
		ret = snd_soc_cache_write(codec, reg, value);
		if (ret < 0)
			return -1;
	}

	if (codec->cache_only) {
		codec->cache_sync = 1;
		return 0;
	}

	if (codec->hw_write(codec->control_data, data, 3) == 3)
		return 0;
	else
		return -EIO;
}

static unsigned int snd_soc_8_16_read(struct snd_soc_codec *codec,
				      unsigned int reg)
{
	int ret;
	unsigned int val;

	if (reg >= codec->driver->reg_cache_size ||
	    snd_soc_codec_volatile_register(codec, reg)) {
		if (codec->cache_only)
			return -1;

		BUG_ON(!codec->hw_read);
		return codec->hw_read(codec, reg);
	}

	ret = snd_soc_cache_read(codec, reg, &val);
	if (ret < 0)
		return -1;
	return val;
}

#if defined(CONFIG_SPI_MASTER)
static int snd_soc_8_16_spi_write(void *control_data, const char *data,
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
#define snd_soc_8_16_spi_write NULL
#endif

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
	int ret;
	unsigned int val;

	reg &= 0xff;
	if (reg >= codec->driver->reg_cache_size ||
		snd_soc_codec_volatile_register(codec, reg)) {
			if (codec->cache_only)
				return -1;

			BUG_ON(!codec->hw_read);
			return codec->hw_read(codec, reg);
	}

	ret = snd_soc_cache_read(codec, reg, &val);
	if (ret < 0)
		return -1;
	return val;
}

static int snd_soc_16_8_write(struct snd_soc_codec *codec, unsigned int reg,
			     unsigned int value)
{
	u8 data[3];
	int ret;

	data[0] = (reg >> 8) & 0xff;
	data[1] = reg & 0xff;
	data[2] = value;

	reg &= 0xff;
	if (!snd_soc_codec_volatile_register(codec, reg) &&
		reg < codec->driver->reg_cache_size) {
		ret = snd_soc_cache_write(codec, reg, value);
		if (ret < 0)
			return -1;
	}

	if (codec->cache_only) {
		codec->cache_sync = 1;
		return 0;
	}

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
	int ret;
	unsigned int val;

	if (reg >= codec->driver->reg_cache_size ||
	    snd_soc_codec_volatile_register(codec, reg)) {
		if (codec->cache_only)
			return -1;

		BUG_ON(!codec->hw_read);
		return codec->hw_read(codec, reg);
	}

	ret = snd_soc_cache_read(codec, reg, &val);
	if (ret < 0)
		return -1;

	return val;
}

static int snd_soc_16_16_write(struct snd_soc_codec *codec, unsigned int reg,
			       unsigned int value)
{
	u8 data[4];
	int ret;

	data[0] = (reg >> 8) & 0xff;
	data[1] = reg & 0xff;
	data[2] = (value >> 8) & 0xff;
	data[3] = value & 0xff;

	if (!snd_soc_codec_volatile_register(codec, reg) &&
		reg < codec->driver->reg_cache_size) {
		ret = snd_soc_cache_write(codec, reg, value);
		if (ret < 0)
			return -1;
	}

	if (codec->cache_only) {
		codec->cache_sync = 1;
		return 0;
	}

	ret = codec->hw_write(codec->control_data, data, 4);
	if (ret == 4)
		return 0;
	if (ret < 0)
		return ret;
	else
		return -EIO;
}

#if defined(CONFIG_SPI_MASTER)
static int snd_soc_16_16_spi_write(void *control_data, const char *data,
				 int len)
{
	struct spi_device *spi = control_data;
	struct spi_transfer t;
	struct spi_message m;
	u8 msg[4];

	if (len <= 0)
		return 0;

	msg[0] = data[0];
	msg[1] = data[1];
	msg[2] = data[2];
	msg[3] = data[3];

	spi_message_init(&m);
	memset(&t, 0, (sizeof t));

	t.tx_buf = &msg[0];
	t.len = len;

	spi_message_add_tail(&t, &m);
	spi_sync(spi, &m);

	return len;
}
#else
#define snd_soc_16_16_spi_write NULL
#endif

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
		.spi_write = snd_soc_8_8_spi_write,
	},
	{
		.addr_bits = 8, .data_bits = 16,
		.write = snd_soc_8_16_write, .read = snd_soc_8_16_read,
		.i2c_read = snd_soc_8_16_read_i2c,
		.spi_write = snd_soc_8_16_spi_write,
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
		.spi_write = snd_soc_16_16_spi_write,
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

	codec->driver->write = io_types[i].write;
	codec->driver->read = io_types[i].read;

	switch (control) {
	case SND_SOC_CUSTOM:
		break;

	case SND_SOC_I2C:
#if defined(CONFIG_I2C) || (defined(CONFIG_I2C_MODULE) && defined(MODULE))
		codec->hw_write = (hw_write_t)i2c_master_send;
#endif
		if (io_types[i].i2c_read)
			codec->hw_read = io_types[i].i2c_read;

		codec->control_data = container_of(codec->dev,
						   struct i2c_client,
						   dev);
		break;

	case SND_SOC_SPI:
		if (io_types[i].spi_write)
			codec->hw_write = io_types[i].spi_write;

		codec->control_data = container_of(codec->dev,
						   struct spi_device,
						   dev);
		break;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_codec_set_cache_io);

static int snd_soc_flat_cache_sync(struct snd_soc_codec *codec)
{
	int i;
	struct snd_soc_codec_driver *codec_drv;
	unsigned int val;

	codec_drv = codec->driver;
	for (i = 0; i < codec_drv->reg_cache_size; ++i) {
		snd_soc_cache_read(codec, i, &val);
		if (codec_drv->reg_cache_default) {
			switch (codec_drv->reg_word_size) {
			case 1: {
				const u8 *cache;

				cache = codec_drv->reg_cache_default;
				if (cache[i] == val)
					continue;
			}
			break;
			case 2: {
				const u16 *cache;

				cache = codec_drv->reg_cache_default;
				if (cache[i] == val)
					continue;
			}
			break;
			default:
				BUG();
			}
		}
		snd_soc_write(codec, i, val);
		dev_dbg(codec->dev, "Synced register %#x, value = %#x\n",
			i, val);
	}
	return 0;
}

static int snd_soc_flat_cache_write(struct snd_soc_codec *codec,
				    unsigned int reg, unsigned int value)
{
	switch (codec->driver->reg_word_size) {
	case 1: {
		u8 *cache;

		cache = codec->reg_cache;
		cache[reg] = value;
	}
	break;
	case 2: {
		u16 *cache;

		cache = codec->reg_cache;
		cache[reg] = value;
	}
	break;
	default:
		BUG();
	}

	return 0;
}

static int snd_soc_flat_cache_read(struct snd_soc_codec *codec,
				   unsigned int reg, unsigned int *value)
{
	switch (codec->driver->reg_word_size) {
	case 1: {
		u8 *cache;

		cache = codec->reg_cache;
		*value = cache[reg];
	}
	break;
	case 2: {
		u16 *cache;

		cache = codec->reg_cache;
		*value = cache[reg];
	}
	break;
	default:
		BUG();
	}

	return 0;
}

static int snd_soc_flat_cache_exit(struct snd_soc_codec *codec)
{
	if (!codec->reg_cache)
		return 0;
	kfree(codec->reg_cache);
	codec->reg_cache = NULL;
	return 0;
}

static int snd_soc_flat_cache_init(struct snd_soc_codec *codec)
{
	struct snd_soc_codec_driver *codec_drv;
	size_t reg_size;

	codec_drv = codec->driver;
	reg_size = codec_drv->reg_cache_size * codec_drv->reg_word_size;

	if (codec_drv->reg_cache_default)
		codec->reg_cache = kmemdup(codec_drv->reg_cache_default,
					   reg_size, GFP_KERNEL);
	else
		codec->reg_cache = kzalloc(reg_size, GFP_KERNEL);
	if (!codec->reg_cache)
		return -ENOMEM;

	return 0;
}

/* an array of all supported compression types */
static const struct snd_soc_cache_ops cache_types[] = {
	{
		.id = SND_SOC_NO_COMPRESSION,
		.init = snd_soc_flat_cache_init,
		.exit = snd_soc_flat_cache_exit,
		.read = snd_soc_flat_cache_read,
		.write = snd_soc_flat_cache_write,
		.sync = snd_soc_flat_cache_sync
	}
};

int snd_soc_cache_init(struct snd_soc_codec *codec)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(cache_types); ++i)
		if (cache_types[i].id == codec->driver->compress_type)
			break;
	if (i == ARRAY_SIZE(cache_types)) {
		dev_err(codec->dev, "Could not match compress type: %d\n",
			codec->driver->compress_type);
		return -EINVAL;
	}

	mutex_init(&codec->cache_rw_mutex);
	codec->cache_ops = &cache_types[i];

	if (codec->cache_ops->init)
		return codec->cache_ops->init(codec);
	return -EINVAL;
}

/*
 * NOTE: keep in mind that this function might be called
 * multiple times.
 */
int snd_soc_cache_exit(struct snd_soc_codec *codec)
{
	if (codec->cache_ops && codec->cache_ops->exit)
		return codec->cache_ops->exit(codec);
	return -EINVAL;
}

/**
 * snd_soc_cache_read: Fetch the value of a given register from the cache.
 *
 * @codec: CODEC to configure.
 * @reg: The register index.
 * @value: The value to be returned.
 */
int snd_soc_cache_read(struct snd_soc_codec *codec,
		       unsigned int reg, unsigned int *value)
{
	int ret;

	mutex_lock(&codec->cache_rw_mutex);

	if (value && codec->cache_ops && codec->cache_ops->read) {
		ret = codec->cache_ops->read(codec, reg, value);
		mutex_unlock(&codec->cache_rw_mutex);
		return ret;
	}

	mutex_unlock(&codec->cache_rw_mutex);
	return -EINVAL;
}
EXPORT_SYMBOL_GPL(snd_soc_cache_read);

/**
 * snd_soc_cache_write: Set the value of a given register in the cache.
 *
 * @codec: CODEC to configure.
 * @reg: The register index.
 * @value: The new register value.
 */
int snd_soc_cache_write(struct snd_soc_codec *codec,
			unsigned int reg, unsigned int value)
{
	int ret;

	mutex_lock(&codec->cache_rw_mutex);

	if (codec->cache_ops && codec->cache_ops->write) {
		ret = codec->cache_ops->write(codec, reg, value);
		mutex_unlock(&codec->cache_rw_mutex);
		return ret;
	}

	mutex_unlock(&codec->cache_rw_mutex);
	return -EINVAL;
}
EXPORT_SYMBOL_GPL(snd_soc_cache_write);

/**
 * snd_soc_cache_sync: Sync the register cache with the hardware.
 *
 * @codec: CODEC to configure.
 *
 * Any registers that should not be synced should be marked as
 * volatile.  In general drivers can choose not to use the provided
 * syncing functionality if they so require.
 */
int snd_soc_cache_sync(struct snd_soc_codec *codec)
{
	int ret;

	if (!codec->cache_sync) {
		return 0;
	}

	if (codec->cache_ops && codec->cache_ops->sync) {
		ret = codec->cache_ops->sync(codec);
		if (!ret)
			codec->cache_sync = 0;
		return ret;
	}

	return -EINVAL;
}
EXPORT_SYMBOL_GPL(snd_soc_cache_sync);
