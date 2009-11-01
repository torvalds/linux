/*
 * PC-Speaker driver for Linux
 *
 * Mixer implementation.
 * Copyright (C) 2001-2008  Stas Sergeev
 */

#include <sound/core.h>
#include <sound/control.h>
#include "pcsp.h"


static int pcsp_enable_info(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;
	return 0;
}

static int pcsp_enable_get(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_pcsp *chip = snd_kcontrol_chip(kcontrol);
	ucontrol->value.integer.value[0] = chip->enable;
	return 0;
}

static int pcsp_enable_put(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_pcsp *chip = snd_kcontrol_chip(kcontrol);
	int changed = 0;
	int enab = ucontrol->value.integer.value[0];
	if (enab != chip->enable) {
		chip->enable = enab;
		changed = 1;
	}
	return changed;
}

static int pcsp_treble_info(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_info *uinfo)
{
	struct snd_pcsp *chip = snd_kcontrol_chip(kcontrol);
	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = chip->max_treble + 1;
	if (uinfo->value.enumerated.item > chip->max_treble)
		uinfo->value.enumerated.item = chip->max_treble;
	sprintf(uinfo->value.enumerated.name, "%lu",
		(unsigned long)PCSP_CALC_RATE(uinfo->value.enumerated.item));
	return 0;
}

static int pcsp_treble_get(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_pcsp *chip = snd_kcontrol_chip(kcontrol);
	ucontrol->value.enumerated.item[0] = chip->treble;
	return 0;
}

static int pcsp_treble_put(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_pcsp *chip = snd_kcontrol_chip(kcontrol);
	int changed = 0;
	int treble = ucontrol->value.enumerated.item[0];
	if (treble != chip->treble) {
		chip->treble = treble;
#if PCSP_DEBUG
		printk(KERN_INFO "PCSP: rate set to %li\n", PCSP_RATE());
#endif
		changed = 1;
	}
	return changed;
}

static int pcsp_pcspkr_info(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;
	return 0;
}

static int pcsp_pcspkr_get(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_pcsp *chip = snd_kcontrol_chip(kcontrol);
	ucontrol->value.integer.value[0] = chip->pcspkr;
	return 0;
}

static int pcsp_pcspkr_put(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_pcsp *chip = snd_kcontrol_chip(kcontrol);
	int changed = 0;
	int spkr = ucontrol->value.integer.value[0];
	if (spkr != chip->pcspkr) {
		chip->pcspkr = spkr;
		changed = 1;
	}
	return changed;
}

#define PCSP_MIXER_CONTROL(ctl_type, ctl_name) \
{ \
	.iface =	SNDRV_CTL_ELEM_IFACE_MIXER, \
	.name =		ctl_name, \
	.info =		pcsp_##ctl_type##_info, \
	.get =		pcsp_##ctl_type##_get, \
	.put =		pcsp_##ctl_type##_put, \
}

static struct snd_kcontrol_new __devinitdata snd_pcsp_controls_pcm[] = {
	PCSP_MIXER_CONTROL(enable, "Master Playback Switch"),
	PCSP_MIXER_CONTROL(treble, "BaseFRQ Playback Volume"),
};

static struct snd_kcontrol_new __devinitdata snd_pcsp_controls_spkr[] = {
	PCSP_MIXER_CONTROL(pcspkr, "PC Speaker Playback Switch"),
};

static int __devinit snd_pcsp_ctls_add(struct snd_pcsp *chip,
	struct snd_kcontrol_new *ctls, int num)
{
	int i, err;
	struct snd_card *card = chip->card;
	for (i = 0; i < num; i++) {
		err = snd_ctl_add(card, snd_ctl_new1(ctls + i, chip));
		if (err < 0)
			return err;
	}
	return 0;
}

int __devinit snd_pcsp_new_mixer(struct snd_pcsp *chip, int nopcm)
{
	int err;
	struct snd_card *card = chip->card;

	if (!nopcm) {
		err = snd_pcsp_ctls_add(chip, snd_pcsp_controls_pcm,
			ARRAY_SIZE(snd_pcsp_controls_pcm));
		if (err < 0)
			return err;
	}
	err = snd_pcsp_ctls_add(chip, snd_pcsp_controls_spkr,
		ARRAY_SIZE(snd_pcsp_controls_spkr));
	if (err < 0)
		return err;

	strcpy(card->mixername, "PC-Speaker");

	return 0;
}
