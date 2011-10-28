/*
 * Linux driver for TerraTec DMX 6Fire USB
 *
 * Mixer control
 *
 * Author:	Torsten Schenk <torsten.schenk@zoho.com>
 * Created:	Jan 01, 2011
 * Version:	0.3.0
 * Copyright:	(C) Torsten Schenk
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/interrupt.h>
#include <sound/control.h>

#include "control.h"
#include "comm.h"
#include "chip.h"

static char *opt_coax_texts[2] = { "Optical", "Coax" };
static char *line_phono_texts[2] = { "Line", "Phono" };

/*
 * calculated with $value\[i\] = 128 \cdot sqrt[3]{\frac{i}{128}}$
 * this is done because the linear values cause rapid degredation
 * of volume in the uppermost region.
 */
static const u8 log_volume_table[128] = {
	0x00, 0x19, 0x20, 0x24, 0x28, 0x2b, 0x2e, 0x30, 0x32, 0x34,
	0x36, 0x38, 0x3a, 0x3b, 0x3d, 0x3e, 0x40, 0x41, 0x42, 0x43,
	0x44, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e,
	0x4e, 0x4f, 0x50, 0x51, 0x52, 0x53, 0x53, 0x54, 0x55, 0x56,
	0x56, 0x57, 0x58, 0x58, 0x59, 0x5a, 0x5b, 0x5b, 0x5c, 0x5c,
	0x5d, 0x5e, 0x5e, 0x5f, 0x60, 0x60, 0x61, 0x61, 0x62, 0x62,
	0x63, 0x63, 0x64, 0x65, 0x65, 0x66, 0x66, 0x67, 0x67, 0x68,
	0x68, 0x69, 0x69, 0x6a, 0x6a, 0x6b, 0x6b, 0x6c, 0x6c, 0x6c,
	0x6d, 0x6d, 0x6e, 0x6e, 0x6f, 0x6f, 0x70, 0x70, 0x70, 0x71,
	0x71, 0x72, 0x72, 0x73, 0x73, 0x73, 0x74, 0x74, 0x75, 0x75,
	0x75, 0x76, 0x76, 0x77, 0x77, 0x77, 0x78, 0x78, 0x78, 0x79,
	0x79, 0x7a, 0x7a, 0x7a, 0x7b, 0x7b, 0x7b, 0x7c, 0x7c, 0x7c,
	0x7d, 0x7d, 0x7d, 0x7e, 0x7e, 0x7e, 0x7f, 0x7f };

/*
 * data that needs to be sent to device. sets up card internal stuff.
 * values dumped from windows driver and filtered by trial'n'error.
 */
static const struct {
	u8 type;
	u8 reg;
	u8 value;
}
init_data[] = {
	{ 0x22, 0x00, 0x00 }, { 0x20, 0x00, 0x08 }, { 0x22, 0x01, 0x01 },
	{ 0x20, 0x01, 0x08 }, { 0x22, 0x02, 0x00 }, { 0x20, 0x02, 0x08 },
	{ 0x22, 0x03, 0x00 }, { 0x20, 0x03, 0x08 }, { 0x22, 0x04, 0x00 },
	{ 0x20, 0x04, 0x08 }, { 0x22, 0x05, 0x01 }, { 0x20, 0x05, 0x08 },
	{ 0x22, 0x04, 0x01 }, { 0x12, 0x04, 0x00 }, { 0x12, 0x05, 0x00 },
	{ 0x12, 0x0d, 0x78 }, { 0x12, 0x21, 0x82 }, { 0x12, 0x22, 0x80 },
	{ 0x12, 0x23, 0x00 }, { 0x12, 0x06, 0x02 }, { 0x12, 0x03, 0x00 },
	{ 0x12, 0x02, 0x00 }, { 0x22, 0x03, 0x01 },
	{ 0 } /* TERMINATING ENTRY */
};

static const int rates_altsetting[] = { 1, 1, 2, 2, 3, 3 };
/* values to write to soundcard register for all samplerates */
static const u16 rates_6fire_vl[] = {0x00, 0x01, 0x00, 0x01, 0x00, 0x01};
static const u16 rates_6fire_vh[] = {0x11, 0x11, 0x10, 0x10, 0x00, 0x00};

enum {
	DIGITAL_THRU_ONLY_SAMPLERATE = 3
};

static void usb6fire_control_master_vol_update(struct control_runtime *rt)
{
	struct comm_runtime *comm_rt = rt->chip->comm;
	if (comm_rt) {
		/* set volume */
		comm_rt->write8(comm_rt, 0x12, 0x0f, 0x7f -
				log_volume_table[rt->master_vol]);
		 /* unmute */
		comm_rt->write8(comm_rt, 0x12, 0x0e, 0x00);
	}
}

static void usb6fire_control_line_phono_update(struct control_runtime *rt)
{
	struct comm_runtime *comm_rt = rt->chip->comm;
	if (comm_rt) {
		comm_rt->write8(comm_rt, 0x22, 0x02, rt->line_phono_switch);
		comm_rt->write8(comm_rt, 0x21, 0x02, rt->line_phono_switch);
	}
}

static void usb6fire_control_opt_coax_update(struct control_runtime *rt)
{
	struct comm_runtime *comm_rt = rt->chip->comm;
	if (comm_rt) {
		comm_rt->write8(comm_rt, 0x22, 0x00, rt->opt_coax_switch);
		comm_rt->write8(comm_rt, 0x21, 0x00, rt->opt_coax_switch);
	}
}

static int usb6fire_control_set_rate(struct control_runtime *rt, int rate)
{
	int ret;
	struct usb_device *device = rt->chip->dev;
	struct comm_runtime *comm_rt = rt->chip->comm;

	if (rate < 0 || rate >= CONTROL_N_RATES)
		return -EINVAL;

	ret = usb_set_interface(device, 1, rates_altsetting[rate]);
	if (ret < 0)
		return ret;

	/* set soundcard clock */
	ret = comm_rt->write16(comm_rt, 0x02, 0x01, rates_6fire_vl[rate],
			rates_6fire_vh[rate]);
	if (ret < 0)
		return ret;

	return 0;
}

static int usb6fire_control_set_channels(
	struct control_runtime *rt, int n_analog_out,
	int n_analog_in, bool spdif_out, bool spdif_in)
{
	int ret;
	struct comm_runtime *comm_rt = rt->chip->comm;

	/* enable analog inputs and outputs
	 * (one bit per stereo-channel) */
	ret = comm_rt->write16(comm_rt, 0x02, 0x02,
			(1 << (n_analog_out / 2)) - 1,
			(1 << (n_analog_in / 2)) - 1);
	if (ret < 0)
		return ret;

	/* disable digital inputs and outputs */
	/* TODO: use spdif_x to enable/disable digital channels */
	ret = comm_rt->write16(comm_rt, 0x02, 0x03, 0x00, 0x00);
	if (ret < 0)
		return ret;

	return 0;
}

static int usb6fire_control_streaming_update(struct control_runtime *rt)
{
	struct comm_runtime *comm_rt = rt->chip->comm;

	if (comm_rt) {
		if (!rt->usb_streaming && rt->digital_thru_switch)
			usb6fire_control_set_rate(rt,
				DIGITAL_THRU_ONLY_SAMPLERATE);
		return comm_rt->write16(comm_rt, 0x02, 0x00, 0x00,
			(rt->usb_streaming ? 0x01 : 0x00) |
			(rt->digital_thru_switch ? 0x08 : 0x00));
	}
	return -EINVAL;
}

static int usb6fire_control_master_vol_info(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 127;
	return 0;
}

static int usb6fire_control_master_vol_put(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct control_runtime *rt = snd_kcontrol_chip(kcontrol);
	int changed = 0;
	if (rt->master_vol != ucontrol->value.integer.value[0]) {
		rt->master_vol = ucontrol->value.integer.value[0];
		usb6fire_control_master_vol_update(rt);
		changed = 1;
	}
	return changed;
}

static int usb6fire_control_master_vol_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct control_runtime *rt = snd_kcontrol_chip(kcontrol);
	ucontrol->value.integer.value[0] = rt->master_vol;
	return 0;
}

static int usb6fire_control_line_phono_info(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 2;
	if (uinfo->value.enumerated.item > 1)
		uinfo->value.enumerated.item = 1;
	strcpy(uinfo->value.enumerated.name,
			line_phono_texts[uinfo->value.enumerated.item]);
	return 0;
}

static int usb6fire_control_line_phono_put(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct control_runtime *rt = snd_kcontrol_chip(kcontrol);
	int changed = 0;
	if (rt->line_phono_switch != ucontrol->value.integer.value[0]) {
		rt->line_phono_switch = ucontrol->value.integer.value[0];
		usb6fire_control_line_phono_update(rt);
		changed = 1;
	}
	return changed;
}

static int usb6fire_control_line_phono_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct control_runtime *rt = snd_kcontrol_chip(kcontrol);
	ucontrol->value.integer.value[0] = rt->line_phono_switch;
	return 0;
}

static int usb6fire_control_opt_coax_info(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 2;
	if (uinfo->value.enumerated.item > 1)
		uinfo->value.enumerated.item = 1;
	strcpy(uinfo->value.enumerated.name,
			opt_coax_texts[uinfo->value.enumerated.item]);
	return 0;
}

static int usb6fire_control_opt_coax_put(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct control_runtime *rt = snd_kcontrol_chip(kcontrol);
	int changed = 0;

	if (rt->opt_coax_switch != ucontrol->value.enumerated.item[0]) {
		rt->opt_coax_switch = ucontrol->value.enumerated.item[0];
		usb6fire_control_opt_coax_update(rt);
		changed = 1;
	}
	return changed;
}

static int usb6fire_control_opt_coax_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct control_runtime *rt = snd_kcontrol_chip(kcontrol);
	ucontrol->value.enumerated.item[0] = rt->opt_coax_switch;
	return 0;
}

static int usb6fire_control_digital_thru_put(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct control_runtime *rt = snd_kcontrol_chip(kcontrol);
	int changed = 0;

	if (rt->digital_thru_switch != ucontrol->value.integer.value[0]) {
		rt->digital_thru_switch = ucontrol->value.integer.value[0];
		usb6fire_control_streaming_update(rt);
		changed = 1;
	}
	return changed;
}

static int usb6fire_control_digital_thru_get(struct snd_kcontrol *kcontrol,
		struct snd_ctl_elem_value *ucontrol)
{
	struct control_runtime *rt = snd_kcontrol_chip(kcontrol);
	ucontrol->value.integer.value[0] = rt->digital_thru_switch;
	return 0;
}

static struct __devinitdata snd_kcontrol_new elements[] = {
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Master Playback Volume",
		.index = 0,
		.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
		.info = usb6fire_control_master_vol_info,
		.get = usb6fire_control_master_vol_get,
		.put = usb6fire_control_master_vol_put
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Line/Phono Capture Route",
		.index = 0,
		.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
		.info = usb6fire_control_line_phono_info,
		.get = usb6fire_control_line_phono_get,
		.put = usb6fire_control_line_phono_put
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Opt/Coax Capture Route",
		.index = 0,
		.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
		.info = usb6fire_control_opt_coax_info,
		.get = usb6fire_control_opt_coax_get,
		.put = usb6fire_control_opt_coax_put
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Digital Thru Playback Route",
		.index = 0,
		.access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
		.info = snd_ctl_boolean_mono_info,
		.get = usb6fire_control_digital_thru_get,
		.put = usb6fire_control_digital_thru_put
	},
	{}
};

int __devinit usb6fire_control_init(struct sfire_chip *chip)
{
	int i;
	int ret;
	struct control_runtime *rt = kzalloc(sizeof(struct control_runtime),
			GFP_KERNEL);
	struct comm_runtime *comm_rt = chip->comm;

	if (!rt)
		return -ENOMEM;

	rt->chip = chip;
	rt->update_streaming = usb6fire_control_streaming_update;
	rt->set_rate = usb6fire_control_set_rate;
	rt->set_channels = usb6fire_control_set_channels;

	i = 0;
	while (init_data[i].type) {
		comm_rt->write8(comm_rt, init_data[i].type, init_data[i].reg,
				init_data[i].value);
		i++;
	}

	usb6fire_control_opt_coax_update(rt);
	usb6fire_control_line_phono_update(rt);
	usb6fire_control_master_vol_update(rt);
	usb6fire_control_streaming_update(rt);

	i = 0;
	while (elements[i].name) {
		ret = snd_ctl_add(chip->card, snd_ctl_new1(&elements[i], rt));
		if (ret < 0) {
			kfree(rt);
			snd_printk(KERN_ERR PREFIX "cannot add control.\n");
			return ret;
		}
		i++;
	}

	chip->control = rt;
	return 0;
}

void usb6fire_control_abort(struct sfire_chip *chip)
{}

void usb6fire_control_destroy(struct sfire_chip *chip)
{
	kfree(chip->control);
	chip->control = NULL;
}
