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
#include <linux/export.h>
#include <linux/slab.h>

#include <trace/events/asoc.h>

static bool snd_soc_set_cache_val(void *base, unsigned int idx,
				  unsigned int val, unsigned int word_size)
{
	switch (word_size) {
	case 1: {
		u8 *cache = base;
		if (cache[idx] == val)
			return true;
		cache[idx] = val;
		break;
	}
	case 2: {
		u16 *cache = base;
		if (cache[idx] == val)
			return true;
		cache[idx] = val;
		break;
	}
	default:
		WARN(1, "Invalid word_size %d\n", word_size);
		break;
	}
	return false;
}

static unsigned int snd_soc_get_cache_val(const void *base, unsigned int idx,
		unsigned int word_size)
{
	if (!base)
		return -1;

	switch (word_size) {
	case 1: {
		const u8 *cache = base;
		return cache[idx];
	}
	case 2: {
		const u16 *cache = base;
		return cache[idx];
	}
	default:
		WARN(1, "Invalid word_size %d\n", word_size);
		break;
	}
	/* unreachable */
	return -1;
}

int snd_soc_cache_init(struct snd_soc_codec *codec)
{
	const struct snd_soc_codec_driver *codec_drv = codec->driver;
	size_t reg_size;

	reg_size = codec_drv->reg_cache_size * codec_drv->reg_word_size;

	if (!reg_size)
		return 0;

	mutex_init(&codec->cache_rw_mutex);

	dev_dbg(codec->dev, "ASoC: Initializing cache for %s codec\n",
				codec->component.name);

	if (codec_drv->reg_cache_default)
		codec->reg_cache = kmemdup(codec_drv->reg_cache_default,
					   reg_size, GFP_KERNEL);
	else
		codec->reg_cache = kzalloc(reg_size, GFP_KERNEL);
	if (!codec->reg_cache)
		return -ENOMEM;

	return 0;
}

/*
 * NOTE: keep in mind that this function might be called
 * multiple times.
 */
int snd_soc_cache_exit(struct snd_soc_codec *codec)
{
	dev_dbg(codec->dev, "ASoC: Destroying cache for %s codec\n",
			codec->component.name);
	kfree(codec->reg_cache);
	codec->reg_cache = NULL;
	return 0;
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
	if (!value)
		return -EINVAL;

	mutex_lock(&codec->cache_rw_mutex);
	if (!ZERO_OR_NULL_PTR(codec->reg_cache))
		*value = snd_soc_get_cache_val(codec->reg_cache, reg,
					       codec->driver->reg_word_size);
	mutex_unlock(&codec->cache_rw_mutex);

	return 0;
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
	mutex_lock(&codec->cache_rw_mutex);
	if (!ZERO_OR_NULL_PTR(codec->reg_cache))
		snd_soc_set_cache_val(codec->reg_cache, reg, value,
				      codec->driver->reg_word_size);
	mutex_unlock(&codec->cache_rw_mutex);

	return 0;
}
EXPORT_SYMBOL_GPL(snd_soc_cache_write);

static int snd_soc_flat_cache_sync(struct snd_soc_codec *codec)
{
	int i;
	int ret;
	const struct snd_soc_codec_driver *codec_drv;
	unsigned int val;

	codec_drv = codec->driver;
	for (i = 0; i < codec_drv->reg_cache_size; ++i) {
		ret = snd_soc_cache_read(codec, i, &val);
		if (ret)
			return ret;
		if (codec_drv->reg_cache_default)
			if (snd_soc_get_cache_val(codec_drv->reg_cache_default,
						  i, codec_drv->reg_word_size) == val)
				continue;

		ret = snd_soc_write(codec, i, val);
		if (ret)
			return ret;
		dev_dbg(codec->dev, "ASoC: Synced register %#x, value = %#x\n",
			i, val);
	}
	return 0;
}

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
	const char *name = "flat";
	int ret;

	if (!codec->cache_sync)
		return 0;

	dev_dbg(codec->dev, "ASoC: Syncing cache for %s codec\n",
		codec->component.name);
	trace_snd_soc_cache_sync(codec, name, "start");
	ret = snd_soc_flat_cache_sync(codec);
	if (!ret)
		codec->cache_sync = 0;
	trace_snd_soc_cache_sync(codec, name, "end");
	return ret;
}
EXPORT_SYMBOL_GPL(snd_soc_cache_sync);
