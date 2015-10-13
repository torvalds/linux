/*
 * Digital Beep Input Interface for HD-audio codec
 *
 * Author: Matt Ranostay <mranostay@gmail.com>
 * Copyright (c) 2008 Embedded Alley Solutions Inc
 *
 *  This driver is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This driver is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include <linux/input.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/export.h>
#include <sound/core.h>
#include "hda_beep.h"
#include "hda_local.h"

enum {
	DIGBEEP_HZ_STEP = 46875,	/* 46.875 Hz */
	DIGBEEP_HZ_MIN = 93750,		/* 93.750 Hz */
	DIGBEEP_HZ_MAX = 12000000,	/* 12 KHz */
};

/* generate or stop tone */
static void generate_tone(struct hda_beep *beep, int tone)
{
	struct hda_codec *codec = beep->codec;

	if (tone && !beep->playing) {
		snd_hda_power_up(codec);
		if (beep->power_hook)
			beep->power_hook(beep, true);
		beep->playing = 1;
	}
	snd_hda_codec_write(codec, beep->nid, 0,
			    AC_VERB_SET_BEEP_CONTROL, tone);
	if (!tone && beep->playing) {
		beep->playing = 0;
		if (beep->power_hook)
			beep->power_hook(beep, false);
		snd_hda_power_down(codec);
	}
}

static void snd_hda_generate_beep(struct work_struct *work)
{
	struct hda_beep *beep =
		container_of(work, struct hda_beep, beep_work);

	if (beep->enabled)
		generate_tone(beep, beep->tone);
}

/* (non-standard) Linear beep tone calculation for IDT/STAC codecs 
 *
 * The tone frequency of beep generator on IDT/STAC codecs is
 * defined from the 8bit tone parameter, in Hz,
 *    freq = 48000 * (257 - tone) / 1024
 * that is from 12kHz to 93.75Hz in steps of 46.875 Hz
 */
static int beep_linear_tone(struct hda_beep *beep, int hz)
{
	if (hz <= 0)
		return 0;
	hz *= 1000; /* fixed point */
	hz = hz - DIGBEEP_HZ_MIN
		+ DIGBEEP_HZ_STEP / 2; /* round to nearest step */
	if (hz < 0)
		hz = 0; /* turn off PC beep*/
	else if (hz >= (DIGBEEP_HZ_MAX - DIGBEEP_HZ_MIN))
		hz = 1; /* max frequency */
	else {
		hz /= DIGBEEP_HZ_STEP;
		hz = 255 - hz;
	}
	return hz;
}

/* HD-audio standard beep tone parameter calculation
 *
 * The tone frequency in Hz is calculated as
 *   freq = 48000 / (tone * 4)
 * from 47Hz to 12kHz
 */
static int beep_standard_tone(struct hda_beep *beep, int hz)
{
	if (hz <= 0)
		return 0; /* disabled */
	hz = 12000 / hz;
	if (hz > 0xff)
		return 0xff;
	if (hz <= 0)
		return 1;
	return hz;
}

static int snd_hda_beep_event(struct input_dev *dev, unsigned int type,
				unsigned int code, int hz)
{
	struct hda_beep *beep = input_get_drvdata(dev);

	switch (code) {
	case SND_BELL:
		if (hz)
			hz = 1000;
		/* fallthru */
	case SND_TONE:
		if (beep->linear_tone)
			beep->tone = beep_linear_tone(beep, hz);
		else
			beep->tone = beep_standard_tone(beep, hz);
		break;
	default:
		return -1;
	}

	/* schedule beep event */
	schedule_work(&beep->beep_work);
	return 0;
}

static void turn_off_beep(struct hda_beep *beep)
{
	cancel_work_sync(&beep->beep_work);
	if (beep->playing) {
		/* turn off beep */
		generate_tone(beep, 0);
	}
}

static void snd_hda_do_detach(struct hda_beep *beep)
{
	if (beep->registered)
		input_unregister_device(beep->dev);
	else
		input_free_device(beep->dev);
	beep->dev = NULL;
	turn_off_beep(beep);
}

static int snd_hda_do_attach(struct hda_beep *beep)
{
	struct input_dev *input_dev;
	struct hda_codec *codec = beep->codec;

	input_dev = input_allocate_device();
	if (!input_dev)
		return -ENOMEM;

	/* setup digital beep device */
	input_dev->name = "HDA Digital PCBeep";
	input_dev->phys = beep->phys;
	input_dev->id.bustype = BUS_PCI;
	input_dev->dev.parent = &codec->card->card_dev;

	input_dev->id.vendor = codec->core.vendor_id >> 16;
	input_dev->id.product = codec->core.vendor_id & 0xffff;
	input_dev->id.version = 0x01;

	input_dev->evbit[0] = BIT_MASK(EV_SND);
	input_dev->sndbit[0] = BIT_MASK(SND_BELL) | BIT_MASK(SND_TONE);
	input_dev->event = snd_hda_beep_event;
	input_set_drvdata(input_dev, beep);

	beep->dev = input_dev;
	return 0;
}

/**
 * snd_hda_enable_beep_device - Turn on/off beep sound
 * @codec: the HDA codec
 * @enable: flag to turn on/off
 */
int snd_hda_enable_beep_device(struct hda_codec *codec, int enable)
{
	struct hda_beep *beep = codec->beep;
	if (!beep)
		return 0;
	enable = !!enable;
	if (beep->enabled != enable) {
		beep->enabled = enable;
		if (!enable)
			turn_off_beep(beep);
		return 1;
	}
	return 0;
}
EXPORT_SYMBOL_GPL(snd_hda_enable_beep_device);

/**
 * snd_hda_attach_beep_device - Attach a beep input device
 * @codec: the HDA codec
 * @nid: beep NID
 *
 * Attach a beep object to the given widget.  If beep hint is turned off
 * explicitly or beep_mode of the codec is turned off, this doesn't nothing.
 *
 * The attached beep device has to be registered via
 * snd_hda_register_beep_device() and released via snd_hda_detach_beep_device()
 * appropriately.
 *
 * Currently, only one beep device is allowed to each codec.
 */
int snd_hda_attach_beep_device(struct hda_codec *codec, int nid)
{
	struct hda_beep *beep;
	int err;

	if (!snd_hda_get_bool_hint(codec, "beep"))
		return 0; /* disabled explicitly by hints */
	if (codec->beep_mode == HDA_BEEP_MODE_OFF)
		return 0; /* disabled by module option */

	beep = kzalloc(sizeof(*beep), GFP_KERNEL);
	if (beep == NULL)
		return -ENOMEM;
	snprintf(beep->phys, sizeof(beep->phys),
		"card%d/codec#%d/beep0", codec->card->number, codec->addr);
	/* enable linear scale */
	snd_hda_codec_write_cache(codec, nid, 0,
		AC_VERB_SET_DIGI_CONVERT_2, 0x01);

	beep->nid = nid;
	beep->codec = codec;
	codec->beep = beep;

	INIT_WORK(&beep->beep_work, &snd_hda_generate_beep);
	mutex_init(&beep->mutex);

	err = snd_hda_do_attach(beep);
	if (err < 0) {
		kfree(beep);
		codec->beep = NULL;
		return err;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(snd_hda_attach_beep_device);

/**
 * snd_hda_detach_beep_device - Detach the beep device
 * @codec: the HDA codec
 */
void snd_hda_detach_beep_device(struct hda_codec *codec)
{
	struct hda_beep *beep = codec->beep;
	if (beep) {
		if (beep->dev)
			snd_hda_do_detach(beep);
		codec->beep = NULL;
		kfree(beep);
	}
}
EXPORT_SYMBOL_GPL(snd_hda_detach_beep_device);

/**
 * snd_hda_register_beep_device - Register the beep device
 * @codec: the HDA codec
 */
int snd_hda_register_beep_device(struct hda_codec *codec)
{
	struct hda_beep *beep = codec->beep;
	int err;

	if (!beep || !beep->dev)
		return 0;

	err = input_register_device(beep->dev);
	if (err < 0) {
		codec_err(codec, "hda_beep: unable to register input device\n");
		input_free_device(beep->dev);
		codec->beep = NULL;
		kfree(beep);
		return err;
	}
	beep->registered = true;
	return 0;
}
EXPORT_SYMBOL_GPL(snd_hda_register_beep_device);

static bool ctl_has_mute(struct snd_kcontrol *kcontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	return query_amp_caps(codec, get_amp_nid(kcontrol),
			      get_amp_direction(kcontrol)) & AC_AMPCAP_MUTE;
}

/* get/put callbacks for beep mute mixer switches */

/**
 * snd_hda_mixer_amp_switch_get_beep - Get callback for beep controls
 * @kcontrol: ctl element
 * @ucontrol: pointer to get/store the data
 */
int snd_hda_mixer_amp_switch_get_beep(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct hda_beep *beep = codec->beep;
	if (beep && (!beep->enabled || !ctl_has_mute(kcontrol))) {
		ucontrol->value.integer.value[0] =
			ucontrol->value.integer.value[1] = beep->enabled;
		return 0;
	}
	return snd_hda_mixer_amp_switch_get(kcontrol, ucontrol);
}
EXPORT_SYMBOL_GPL(snd_hda_mixer_amp_switch_get_beep);

/**
 * snd_hda_mixer_amp_switch_put_beep - Put callback for beep controls
 * @kcontrol: ctl element
 * @ucontrol: pointer to get/store the data
 */
int snd_hda_mixer_amp_switch_put_beep(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	struct hda_codec *codec = snd_kcontrol_chip(kcontrol);
	struct hda_beep *beep = codec->beep;
	if (beep) {
		u8 chs = get_amp_channels(kcontrol);
		int enable = 0;
		long *valp = ucontrol->value.integer.value;
		if (chs & 1) {
			enable |= *valp;
			valp++;
		}
		if (chs & 2)
			enable |= *valp;
		snd_hda_enable_beep_device(codec, enable);
	}
	if (!ctl_has_mute(kcontrol))
		return 0;
	return snd_hda_mixer_amp_switch_put(kcontrol, ucontrol);
}
EXPORT_SYMBOL_GPL(snd_hda_mixer_amp_switch_put_beep);
