/*
 * Beep using pcm
 *
 * Copyright (c) by Takashi Iwai <tiwai@suse.de>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include <linux/io.h>
#include <asm/irq.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/pci.h>
#include <linux/dma-mapping.h>
#include <sound/core.h>
#include <sound/control.h>
#include "pmac.h"

struct pmac_beep {
	int running;		/* boolean */
	int volume;		/* mixer volume: 0-100 */
	int volume_play;	/* currently playing volume */
	int hz;
	int nsamples;
	short *buf;		/* allocated wave buffer */
	dma_addr_t addr;	/* physical address of buffer */
	struct input_dev *dev;
};

/*
 * stop beep if running
 */
void snd_pmac_beep_stop(struct snd_pmac *chip)
{
	struct pmac_beep *beep = chip->beep;
	if (beep && beep->running) {
		beep->running = 0;
		snd_pmac_beep_dma_stop(chip);
	}
}

/*
 * Stuff for outputting a beep.  The values range from -327 to +327
 * so we can multiply by an amplitude in the range 0..100 to get a
 * signed short value to put in the output buffer.
 */
static short beep_wform[256] = {
	0,	40,	79,	117,	153,	187,	218,	245,
	269,	288,	304,	316,	323,	327,	327,	324,
	318,	310,	299,	288,	275,	262,	249,	236,
	224,	213,	204,	196,	190,	186,	183,	182,
	182,	183,	186,	189,	192,	196,	200,	203,
	206,	208,	209,	209,	209,	207,	204,	201,
	197,	193,	188,	183,	179,	174,	170,	166,
	163,	161,	160,	159,	159,	160,	161,	162,
	164,	166,	168,	169,	171,	171,	171,	170,
	169,	167,	163,	159,	155,	150,	144,	139,
	133,	128,	122,	117,	113,	110,	107,	105,
	103,	103,	103,	103,	104,	104,	105,	105,
	105,	103,	101,	97,	92,	86,	78,	68,
	58,	45,	32,	18,	3,	-11,	-26,	-41,
	-55,	-68,	-79,	-88,	-95,	-100,	-102,	-102,
	-99,	-93,	-85,	-75,	-62,	-48,	-33,	-16,
	0,	16,	33,	48,	62,	75,	85,	93,
	99,	102,	102,	100,	95,	88,	79,	68,
	55,	41,	26,	11,	-3,	-18,	-32,	-45,
	-58,	-68,	-78,	-86,	-92,	-97,	-101,	-103,
	-105,	-105,	-105,	-104,	-104,	-103,	-103,	-103,
	-103,	-105,	-107,	-110,	-113,	-117,	-122,	-128,
	-133,	-139,	-144,	-150,	-155,	-159,	-163,	-167,
	-169,	-170,	-171,	-171,	-171,	-169,	-168,	-166,
	-164,	-162,	-161,	-160,	-159,	-159,	-160,	-161,
	-163,	-166,	-170,	-174,	-179,	-183,	-188,	-193,
	-197,	-201,	-204,	-207,	-209,	-209,	-209,	-208,
	-206,	-203,	-200,	-196,	-192,	-189,	-186,	-183,
	-182,	-182,	-183,	-186,	-190,	-196,	-204,	-213,
	-224,	-236,	-249,	-262,	-275,	-288,	-299,	-310,
	-318,	-324,	-327,	-327,	-323,	-316,	-304,	-288,
	-269,	-245,	-218,	-187,	-153,	-117,	-79,	-40,
};

#define BEEP_SRATE	22050	/* 22050 Hz sample rate */
#define BEEP_BUFLEN	512
#define BEEP_VOLUME	15	/* 0 - 100 */

static int snd_pmac_beep_event(struct input_dev *dev, unsigned int type,
			       unsigned int code, int hz)
{
	struct snd_pmac *chip;
	struct pmac_beep *beep;
	unsigned long flags;
	int beep_speed = 0;
	int srate;
	int period, ncycles, nsamples;
	int i, j, f;
	short *p;

	if (type != EV_SND)
		return -1;

	switch (code) {
	case SND_BELL: if (hz) hz = 1000;
	case SND_TONE: break;
	default: return -1;
	}

	chip = input_get_drvdata(dev);
	if (! chip || (beep = chip->beep) == NULL)
		return -1;

	if (! hz) {
		spin_lock_irqsave(&chip->reg_lock, flags);
		if (beep->running)
			snd_pmac_beep_stop(chip);
		spin_unlock_irqrestore(&chip->reg_lock, flags);
		return 0;
	}

	beep_speed = snd_pmac_rate_index(chip, &chip->playback, BEEP_SRATE);
	srate = chip->freq_table[beep_speed];

	if (hz <= srate / BEEP_BUFLEN || hz > srate / 2)
		hz = 1000;

	spin_lock_irqsave(&chip->reg_lock, flags);
	if (chip->playback.running || chip->capture.running || beep->running) {
		spin_unlock_irqrestore(&chip->reg_lock, flags);
		return 0;
	}
	beep->running = 1;
	spin_unlock_irqrestore(&chip->reg_lock, flags);

	if (hz == beep->hz && beep->volume == beep->volume_play) {
		nsamples = beep->nsamples;
	} else {
		period = srate * 256 / hz;	/* fixed point */
		ncycles = BEEP_BUFLEN * 256 / period;
		nsamples = (period * ncycles) >> 8;
		f = ncycles * 65536 / nsamples;
		j = 0;
		p = beep->buf;
		for (i = 0; i < nsamples; ++i, p += 2) {
			p[0] = p[1] = beep_wform[j >> 8] * beep->volume;
			j = (j + f) & 0xffff;
		}
		beep->hz = hz;
		beep->volume_play = beep->volume;
		beep->nsamples = nsamples;
	}

	spin_lock_irqsave(&chip->reg_lock, flags);
	snd_pmac_beep_dma_start(chip, beep->nsamples * 4, beep->addr, beep_speed);
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	return 0;
}

/*
 * beep volume mixer
 */

static int snd_pmac_info_beep(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 100;
	return 0;
}

static int snd_pmac_get_beep(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_pmac *chip = snd_kcontrol_chip(kcontrol);
	if (snd_BUG_ON(!chip->beep))
		return -ENXIO;
	ucontrol->value.integer.value[0] = chip->beep->volume;
	return 0;
}

static int snd_pmac_put_beep(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_pmac *chip = snd_kcontrol_chip(kcontrol);
	unsigned int oval, nval;
	if (snd_BUG_ON(!chip->beep))
		return -ENXIO;
	oval = chip->beep->volume;
	nval = ucontrol->value.integer.value[0];
	if (nval > 100)
		return -EINVAL;
	chip->beep->volume = nval;
	return oval != chip->beep->volume;
}

static struct snd_kcontrol_new snd_pmac_beep_mixer = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Beep Playback Volume",
	.info = snd_pmac_info_beep,
	.get = snd_pmac_get_beep,
	.put = snd_pmac_put_beep,
};

/* Initialize beep stuff */
int snd_pmac_attach_beep(struct snd_pmac *chip)
{
	struct pmac_beep *beep;
	struct input_dev *input_dev;
	struct snd_kcontrol *beep_ctl;
	void *dmabuf;
	int err = -ENOMEM;

	beep = kzalloc(sizeof(*beep), GFP_KERNEL);
	if (! beep)
		return -ENOMEM;
	dmabuf = dma_alloc_coherent(&chip->pdev->dev, BEEP_BUFLEN * 4,
				    &beep->addr, GFP_KERNEL);
	input_dev = input_allocate_device();
	if (! dmabuf || ! input_dev)
		goto fail1;

	/* FIXME: set more better values */
	input_dev->name = "PowerMac Beep";
	input_dev->phys = "powermac/beep";
	input_dev->id.bustype = BUS_ADB;
	input_dev->id.vendor = 0x001f;
	input_dev->id.product = 0x0001;
	input_dev->id.version = 0x0100;

	input_dev->evbit[0] = BIT_MASK(EV_SND);
	input_dev->sndbit[0] = BIT_MASK(SND_BELL) | BIT_MASK(SND_TONE);
	input_dev->event = snd_pmac_beep_event;
	input_dev->dev.parent = &chip->pdev->dev;
	input_set_drvdata(input_dev, chip);

	beep->dev = input_dev;
	beep->buf = dmabuf;
	beep->volume = BEEP_VOLUME;
	beep->running = 0;

	beep_ctl = snd_ctl_new1(&snd_pmac_beep_mixer, chip);
	err = snd_ctl_add(chip->card, beep_ctl);
	if (err < 0)
		goto fail1;

	chip->beep = beep;

	err = input_register_device(beep->dev);
	if (err)
		goto fail2;
 
 	return 0;
 
 fail2:	snd_ctl_remove(chip->card, beep_ctl);
 fail1:	input_free_device(input_dev);
	if (dmabuf)
		dma_free_coherent(&chip->pdev->dev, BEEP_BUFLEN * 4,
				  dmabuf, beep->addr);
	kfree(beep);
	return err;
}

void snd_pmac_detach_beep(struct snd_pmac *chip)
{
	if (chip->beep) {
		input_unregister_device(chip->beep->dev);
		dma_free_coherent(&chip->pdev->dev, BEEP_BUFLEN * 4,
				  chip->beep->buf, chip->beep->addr);
		kfree(chip->beep);
		chip->beep = NULL;
	}
}
