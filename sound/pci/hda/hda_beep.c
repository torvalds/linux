/*
 * Digital Beep Input Interface for HD-audio codec
 *
 * Author: Matthew Ranostay <mranostay@embeddedalley.com>
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
#include <linux/pci.h>
#include <linux/workqueue.h>
#include <sound/core.h>
#include "hda_beep.h"

enum {
	DIGBEEP_HZ_STEP = 46875,	/* 46.875 Hz */
	DIGBEEP_HZ_MIN = 93750,		/* 93.750 Hz */
	DIGBEEP_HZ_MAX = 12000000,	/* 12 KHz */
};

static void snd_hda_generate_beep(struct work_struct *work)
{
	struct hda_beep *beep =
		container_of(work, struct hda_beep, beep_work);
	struct hda_codec *codec = beep->codec;

	if (!beep->enabled)
		return;

	/* generate tone */
	snd_hda_codec_write_cache(codec, beep->nid, 0,
			AC_VERB_SET_BEEP_CONTROL, beep->tone);
}

static int snd_hda_beep_event(struct input_dev *dev, unsigned int type,
				unsigned int code, int hz)
{
	struct hda_beep *beep = input_get_drvdata(dev);

	switch (code) {
	case SND_BELL:
		if (hz)
			hz = 1000;
	case SND_TONE:
		hz *= 1000; /* fixed point */
		hz = hz - DIGBEEP_HZ_MIN;
		if (hz < 0)
			hz = 0; /* turn off PC beep*/
		else if (hz >= (DIGBEEP_HZ_MAX - DIGBEEP_HZ_MIN))
			hz = 0xff;
		else {
			hz /= DIGBEEP_HZ_STEP;
			hz++;
		}
		break;
	default:
		return -1;
	}
	beep->tone = hz;

	/* schedule beep event */
	schedule_work(&beep->beep_work);
	return 0;
}

int snd_hda_attach_beep_device(struct hda_codec *codec, int nid)
{
	struct input_dev *input_dev;
	struct hda_beep *beep;
	int err;

	beep = kzalloc(sizeof(*beep), GFP_KERNEL);
	if (beep == NULL)
		return -ENOMEM;
	snprintf(beep->phys, sizeof(beep->phys),
		"card%d/codec#%d/beep0", codec->bus->card->number, codec->addr);
	input_dev = input_allocate_device();
	if (!input_dev) {
		kfree(beep);
		return -ENOMEM;
	}

	/* setup digital beep device */
	input_dev->name = "HDA Digital PCBeep";
	input_dev->phys = beep->phys;
	input_dev->id.bustype = BUS_PCI;

	input_dev->id.vendor = codec->vendor_id >> 16;
	input_dev->id.product = codec->vendor_id & 0xffff;
	input_dev->id.version = 0x01;

	input_dev->evbit[0] = BIT_MASK(EV_SND);
	input_dev->sndbit[0] = BIT_MASK(SND_BELL) | BIT_MASK(SND_TONE);
	input_dev->event = snd_hda_beep_event;
	input_dev->dev.parent = &codec->bus->pci->dev;
	input_set_drvdata(input_dev, beep);

	err = input_register_device(input_dev);
	if (err < 0) {
		input_free_device(input_dev);
		kfree(beep);
		return err;
	}

	/* enable linear scale */
	snd_hda_codec_write(codec, nid, 0,
		AC_VERB_SET_DIGI_CONVERT_2, 0x01);

	beep->nid = nid;
	beep->dev = input_dev;
	beep->codec = codec;
	beep->enabled = 1;
	codec->beep = beep;

	INIT_WORK(&beep->beep_work, &snd_hda_generate_beep);
	return 0;
}
EXPORT_SYMBOL_HDA(snd_hda_attach_beep_device);

void snd_hda_detach_beep_device(struct hda_codec *codec)
{
	struct hda_beep *beep = codec->beep;
	if (beep) {
		cancel_work_sync(&beep->beep_work);

		input_unregister_device(beep->dev);
		kfree(beep);
	}
}
EXPORT_SYMBOL_HDA(snd_hda_detach_beep_device);
