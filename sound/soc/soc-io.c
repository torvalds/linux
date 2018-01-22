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

/**
 * snd_soc_component_read() - Read register value
 * @component: Component to read from
 * @reg: Register to read
 * @val: Pointer to where the read value is stored
 *
 * Return: 0 on success, a negative error code otherwise.
 */
int snd_soc_component_read(struct snd_soc_component *component,
	unsigned int reg, unsigned int *val)
{
	int ret;

	if (component->regmap)
		ret = regmap_read(component->regmap, reg, val);
	else if (component->read)
		ret = component->read(component, reg, val);
	else
		ret = -EIO;

	return ret;
}
EXPORT_SYMBOL_GPL(snd_soc_component_read);

unsigned int snd_soc_component_read32(struct snd_soc_component *component,
				      unsigned int reg)
{
	unsigned int val;
	int ret;

	ret = snd_soc_component_read(component, reg, &val);
	if (ret < 0)
		return -1;

	return val;
}
EXPORT_SYMBOL_GPL(snd_soc_component_read32);

/**
 * snd_soc_component_write() - Write register value
 * @component: Component to write to
 * @reg: Register to write
 * @val: Value to write to the register
 *
 * Return: 0 on success, a negative error code otherwise.
 */
int snd_soc_component_write(struct snd_soc_component *component,
	unsigned int reg, unsigned int val)
{
	if (component->regmap)
		return regmap_write(component->regmap, reg, val);
	else if (component->write)
		return component->write(component, reg, val);
	else
		return -EIO;
}
EXPORT_SYMBOL_GPL(snd_soc_component_write);

static int snd_soc_component_update_bits_legacy(
	struct snd_soc_component *component, unsigned int reg,
	unsigned int mask, unsigned int val, bool *change)
{
	unsigned int old, new;
	int ret;

	if (!component->read || !component->write)
		return -EIO;

	mutex_lock(&component->io_mutex);

	ret = component->read(component, reg, &old);
	if (ret < 0)
		goto out_unlock;

	new = (old & ~mask) | (val & mask);
	*change = old != new;
	if (*change)
		ret = component->write(component, reg, new);
out_unlock:
	mutex_unlock(&component->io_mutex);

	return ret;
}

/**
 * snd_soc_component_update_bits() - Perform read/modify/write cycle
 * @component: Component to update
 * @reg: Register to update
 * @mask: Mask that specifies which bits to update
 * @val: New value for the bits specified by mask
 *
 * Return: 1 if the operation was successful and the value of the register
 * changed, 0 if the operation was successful, but the value did not change.
 * Returns a negative error code otherwise.
 */
int snd_soc_component_update_bits(struct snd_soc_component *component,
	unsigned int reg, unsigned int mask, unsigned int val)
{
	bool change;
	int ret;

	if (component->regmap)
		ret = regmap_update_bits_check(component->regmap, reg, mask,
			val, &change);
	else
		ret = snd_soc_component_update_bits_legacy(component, reg,
			mask, val, &change);

	if (ret < 0)
		return ret;
	return change;
}
EXPORT_SYMBOL_GPL(snd_soc_component_update_bits);

/**
 * snd_soc_component_update_bits_async() - Perform asynchronous
 *  read/modify/write cycle
 * @component: Component to update
 * @reg: Register to update
 * @mask: Mask that specifies which bits to update
 * @val: New value for the bits specified by mask
 *
 * This function is similar to snd_soc_component_update_bits(), but the update
 * operation is scheduled asynchronously. This means it may not be completed
 * when the function returns. To make sure that all scheduled updates have been
 * completed snd_soc_component_async_complete() must be called.
 *
 * Return: 1 if the operation was successful and the value of the register
 * changed, 0 if the operation was successful, but the value did not change.
 * Returns a negative error code otherwise.
 */
int snd_soc_component_update_bits_async(struct snd_soc_component *component,
	unsigned int reg, unsigned int mask, unsigned int val)
{
	bool change;
	int ret;

	if (component->regmap)
		ret = regmap_update_bits_check_async(component->regmap, reg,
			mask, val, &change);
	else
		ret = snd_soc_component_update_bits_legacy(component, reg,
			mask, val, &change);

	if (ret < 0)
		return ret;
	return change;
}
EXPORT_SYMBOL_GPL(snd_soc_component_update_bits_async);

/**
 * snd_soc_component_async_complete() - Ensure asynchronous I/O has completed
 * @component: Component for which to wait
 *
 * This function blocks until all asynchronous I/O which has previously been
 * scheduled using snd_soc_component_update_bits_async() has completed.
 */
void snd_soc_component_async_complete(struct snd_soc_component *component)
{
	if (component->regmap)
		regmap_async_complete(component->regmap);
}
EXPORT_SYMBOL_GPL(snd_soc_component_async_complete);

/**
 * snd_soc_component_test_bits - Test register for change
 * @component: component
 * @reg: Register to test
 * @mask: Mask that specifies which bits to test
 * @value: Value to test against
 *
 * Tests a register with a new value and checks if the new value is
 * different from the old value.
 *
 * Return: 1 for change, otherwise 0.
 */
int snd_soc_component_test_bits(struct snd_soc_component *component,
	unsigned int reg, unsigned int mask, unsigned int value)
{
	unsigned int old, new;
	int ret;

	ret = snd_soc_component_read(component, reg, &old);
	if (ret < 0)
		return ret;
	new = (old & ~mask) | value;
	return old != new;
}
EXPORT_SYMBOL_GPL(snd_soc_component_test_bits);

unsigned int snd_soc_read(struct snd_soc_codec *codec, unsigned int reg)
{
	unsigned int val;
	int ret;

	ret = snd_soc_component_read(&codec->component, reg, &val);
	if (ret < 0)
		return -1;

	return val;
}
EXPORT_SYMBOL_GPL(snd_soc_read);

int snd_soc_write(struct snd_soc_codec *codec, unsigned int reg,
	unsigned int val)
{
	return snd_soc_component_write(&codec->component, reg, val);
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
	return snd_soc_component_update_bits(&codec->component, reg, mask,
		value);
}
EXPORT_SYMBOL_GPL(snd_soc_update_bits);

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
	return snd_soc_component_test_bits(&codec->component, reg, mask, value);
}
EXPORT_SYMBOL_GPL(snd_soc_test_bits);

int snd_soc_platform_read(struct snd_soc_platform *platform,
					unsigned int reg)
{
	unsigned int val;
	int ret;

	ret = snd_soc_component_read(&platform->component, reg, &val);
	if (ret < 0)
		return -1;

	return val;
}
EXPORT_SYMBOL_GPL(snd_soc_platform_read);

int snd_soc_platform_write(struct snd_soc_platform *platform,
					 unsigned int reg, unsigned int val)
{
	return snd_soc_component_write(&platform->component, reg, val);
}
EXPORT_SYMBOL_GPL(snd_soc_platform_write);
