/*
 * max9877.c  --  amp driver for max9877
 *
 * Copyright (C) 2009 Samsung Electronics Co.Ltd
 * Author: Joonyoung Shim <jy0922.shim@samsung.com>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <sound/soc.h>
#include <sound/tlv.h>

#include "max9877.h"

static struct i2c_client *i2c;

static u8 max9877_regs[5] = { 0x40, 0x00, 0x00, 0x00, 0x49 };

static void max9877_write_regs(void)
{
	unsigned int i;
	u8 data[6];

	data[0] = MAX9877_INPUT_MODE;
	for (i = 0; i < ARRAY_SIZE(max9877_regs); i++)
		data[i + 1] = max9877_regs[i];

	if (i2c_master_send(i2c, data, 6) != 6)
		dev_err(&i2c->dev, "i2c write failed\n");
}

static int max9877_get_reg(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	unsigned int reg = mc->reg;
	unsigned int shift = mc->shift;
	unsigned int mask = mc->max;
	unsigned int invert = mc->invert;

	ucontrol->value.integer.value[0] = (max9877_regs[reg] >> shift) & mask;

	if (invert)
		ucontrol->value.integer.value[0] =
			mask - ucontrol->value.integer.value[0];

	return 0;
}

static int max9877_set_reg(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	unsigned int reg = mc->reg;
	unsigned int shift = mc->shift;
	unsigned int mask = mc->max;
	unsigned int invert = mc->invert;
	unsigned int val = (ucontrol->value.integer.value[0] & mask);

	if (invert)
		val = mask - val;

	if (((max9877_regs[reg] >> shift) & mask) == val)
		return 0;

	max9877_regs[reg] &= ~(mask << shift);
	max9877_regs[reg] |= val << shift;
	max9877_write_regs();

	return 1;
}

static int max9877_get_2reg(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	unsigned int reg = mc->reg;
	unsigned int reg2 = mc->rreg;
	unsigned int shift = mc->shift;
	unsigned int mask = mc->max;

	ucontrol->value.integer.value[0] = (max9877_regs[reg] >> shift) & mask;
	ucontrol->value.integer.value[1] = (max9877_regs[reg2] >> shift) & mask;

	return 0;
}

static int max9877_set_2reg(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct soc_mixer_control *mc =
		(struct soc_mixer_control *)kcontrol->private_value;
	unsigned int reg = mc->reg;
	unsigned int reg2 = mc->rreg;
	unsigned int shift = mc->shift;
	unsigned int mask = mc->max;
	unsigned int val = (ucontrol->value.integer.value[0] & mask);
	unsigned int val2 = (ucontrol->value.integer.value[1] & mask);
	unsigned int change = 0;

	if (((max9877_regs[reg] >> shift) & mask) != val)
		change = 1;

	if (((max9877_regs[reg2] >> shift) & mask) != val2)
		change = 1;

	if (change) {
		max9877_regs[reg] &= ~(mask << shift);
		max9877_regs[reg] |= val << shift;
		max9877_regs[reg2] &= ~(mask << shift);
		max9877_regs[reg2] |= val2 << shift;
		max9877_write_regs();
	}

	return change;
}

static int max9877_get_out_mode(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	u8 value = max9877_regs[MAX9877_OUTPUT_MODE] & MAX9877_OUTMODE_MASK;

	if (value)
		value -= 1;

	ucontrol->value.integer.value[0] = value;
	return 0;
}

static int max9877_set_out_mode(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	u8 value = ucontrol->value.integer.value[0];

	value += 1;

	if ((max9877_regs[MAX9877_OUTPUT_MODE] & MAX9877_OUTMODE_MASK) == value)
		return 0;

	max9877_regs[MAX9877_OUTPUT_MODE] &= ~MAX9877_OUTMODE_MASK;
	max9877_regs[MAX9877_OUTPUT_MODE] |= value;
	max9877_write_regs();
	return 1;
}

static int max9877_get_osc_mode(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	u8 value = (max9877_regs[MAX9877_OUTPUT_MODE] & MAX9877_OSC_MASK);

	value = value >> MAX9877_OSC_OFFSET;

	ucontrol->value.integer.value[0] = value;
	return 0;
}

static int max9877_set_osc_mode(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	u8 value = ucontrol->value.integer.value[0];

	value = value << MAX9877_OSC_OFFSET;
	if ((max9877_regs[MAX9877_OUTPUT_MODE] & MAX9877_OSC_MASK) == value)
		return 0;

	max9877_regs[MAX9877_OUTPUT_MODE] &= ~MAX9877_OSC_MASK;
	max9877_regs[MAX9877_OUTPUT_MODE] |= value;
	max9877_write_regs();
	return 1;
}

static const unsigned int max9877_pgain_tlv[] = {
	TLV_DB_RANGE_HEAD(2),
	0, 1, TLV_DB_SCALE_ITEM(0, 900, 0),
	2, 2, TLV_DB_SCALE_ITEM(2000, 0, 0),
};

static const unsigned int max9877_output_tlv[] = {
	TLV_DB_RANGE_HEAD(4),
	0, 7, TLV_DB_SCALE_ITEM(-7900, 400, 1),
	8, 15, TLV_DB_SCALE_ITEM(-4700, 300, 0),
	16, 23, TLV_DB_SCALE_ITEM(-2300, 200, 0),
	24, 31, TLV_DB_SCALE_ITEM(-700, 100, 0),
};

static const char *max9877_out_mode[] = {
	"INA -> SPK",
	"INA -> HP",
	"INA -> SPK and HP",
	"INB -> SPK",
	"INB -> HP",
	"INB -> SPK and HP",
	"INA + INB -> SPK",
	"INA + INB -> HP",
	"INA + INB -> SPK and HP",
};

static const char *max9877_osc_mode[] = {
	"1176KHz",
	"1100KHz",
	"700KHz",
};

static const struct soc_enum max9877_enum[] = {
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(max9877_out_mode), max9877_out_mode),
	SOC_ENUM_SINGLE_EXT(ARRAY_SIZE(max9877_osc_mode), max9877_osc_mode),
};

static const struct snd_kcontrol_new max9877_controls[] = {
	SOC_SINGLE_EXT_TLV("MAX9877 PGAINA Playback Volume",
			MAX9877_INPUT_MODE, 0, 2, 0,
			max9877_get_reg, max9877_set_reg, max9877_pgain_tlv),
	SOC_SINGLE_EXT_TLV("MAX9877 PGAINB Playback Volume",
			MAX9877_INPUT_MODE, 2, 2, 0,
			max9877_get_reg, max9877_set_reg, max9877_pgain_tlv),
	SOC_SINGLE_EXT_TLV("MAX9877 Amp Speaker Playback Volume",
			MAX9877_SPK_VOLUME, 0, 31, 0,
			max9877_get_reg, max9877_set_reg, max9877_output_tlv),
	SOC_DOUBLE_R_EXT_TLV("MAX9877 Amp HP Playback Volume",
			MAX9877_HPL_VOLUME, MAX9877_HPR_VOLUME, 0, 31, 0,
			max9877_get_2reg, max9877_set_2reg, max9877_output_tlv),
	SOC_SINGLE_EXT("MAX9877 INB Stereo Switch",
			MAX9877_INPUT_MODE, 4, 1, 1,
			max9877_get_reg, max9877_set_reg),
	SOC_SINGLE_EXT("MAX9877 INA Stereo Switch",
			MAX9877_INPUT_MODE, 5, 1, 1,
			max9877_get_reg, max9877_set_reg),
	SOC_SINGLE_EXT("MAX9877 Zero-crossing detection Switch",
			MAX9877_INPUT_MODE, 6, 1, 0,
			max9877_get_reg, max9877_set_reg),
	SOC_SINGLE_EXT("MAX9877 Bypass Mode Switch",
			MAX9877_OUTPUT_MODE, 6, 1, 0,
			max9877_get_reg, max9877_set_reg),
	SOC_SINGLE_EXT("MAX9877 Shutdown Mode Switch",
			MAX9877_OUTPUT_MODE, 7, 1, 1,
			max9877_get_reg, max9877_set_reg),
	SOC_ENUM_EXT("MAX9877 Output Mode", max9877_enum[0],
			max9877_get_out_mode, max9877_set_out_mode),
	SOC_ENUM_EXT("MAX9877 Oscillator Mode", max9877_enum[1],
			max9877_get_osc_mode, max9877_set_osc_mode),
};

/* This function is called from ASoC machine driver */
int max9877_add_controls(struct snd_soc_codec *codec)
{
	return snd_soc_add_codec_controls(codec, max9877_controls,
			ARRAY_SIZE(max9877_controls));
}
EXPORT_SYMBOL_GPL(max9877_add_controls);

static int max9877_i2c_probe(struct i2c_client *client,
			     const struct i2c_device_id *id)
{
	i2c = client;

	max9877_write_regs();

	return 0;
}

static int max9877_i2c_remove(struct i2c_client *client)
{
	i2c = NULL;

	return 0;
}

static const struct i2c_device_id max9877_i2c_id[] = {
	{ "max9877", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, max9877_i2c_id);

static struct i2c_driver max9877_i2c_driver = {
	.driver = {
		.name = "max9877",
		.owner = THIS_MODULE,
	},
	.probe = max9877_i2c_probe,
	.remove = max9877_i2c_remove,
	.id_table = max9877_i2c_id,
};

module_i2c_driver(max9877_i2c_driver);

MODULE_DESCRIPTION("ASoC MAX9877 amp driver");
MODULE_AUTHOR("Joonyoung Shim <jy0922.shim@samsung.com>");
MODULE_LICENSE("GPL");
