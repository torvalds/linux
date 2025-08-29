// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * OPL4 mixer functions
 * Copyright (c) 2003 by Clemens Ladisch <clemens@ladisch.de>
 */

#include "opl4_local.h"
#include <sound/control.h>

static int snd_opl4_ctl_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 7;
	return 0;
}

static int snd_opl4_ctl_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_opl4 *opl4 = snd_kcontrol_chip(kcontrol);
	u8 reg = kcontrol->private_value;
	u8 value;

	guard(spinlock_irqsave)(&opl4->reg_lock);
	value = snd_opl4_read(opl4, reg);
	ucontrol->value.integer.value[0] = 7 - (value & 7);
	ucontrol->value.integer.value[1] = 7 - ((value >> 3) & 7);
	return 0;
}

static int snd_opl4_ctl_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_opl4 *opl4 = snd_kcontrol_chip(kcontrol);
	u8 reg = kcontrol->private_value;
	u8 value, old_value;

	value = (7 - (ucontrol->value.integer.value[0] & 7)) |
		((7 - (ucontrol->value.integer.value[1] & 7)) << 3);
	guard(spinlock_irqsave)(&opl4->reg_lock);
	old_value = snd_opl4_read(opl4, reg);
	snd_opl4_write(opl4, reg, value);
	return value != old_value;
}

static const struct snd_kcontrol_new snd_opl4_controls[] = {
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "FM Playback Volume",
		.info = snd_opl4_ctl_info,
		.get = snd_opl4_ctl_get,
		.put = snd_opl4_ctl_put,
		.private_value = OPL4_REG_MIX_CONTROL_FM
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Wavetable Playback Volume",
		.info = snd_opl4_ctl_info,
		.get = snd_opl4_ctl_get,
		.put = snd_opl4_ctl_put,
		.private_value = OPL4_REG_MIX_CONTROL_PCM
	}
};

int snd_opl4_create_mixer(struct snd_opl4 *opl4)
{
	struct snd_card *card = opl4->card;
	int i, err;

	strcat(card->mixername, ",OPL4");

	for (i = 0; i < 2; ++i) {
		err = snd_ctl_add(card, snd_ctl_new1(&snd_opl4_controls[i], opl4));
		if (err < 0)
			return err;
	}
	return 0;
}
