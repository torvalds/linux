/*
 * OPL4 mixer functions
 * Copyright (c) 2003 by Clemens Ladisch <clemens@ladisch.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include "opl4_local.h"
#include <sound/control.h>

static int snd_opl4_ctl_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 7;
	return 0;
}

static int snd_opl4_ctl_get(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	opl4_t *opl4 = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	u8 reg = kcontrol->private_value;
	u8 value;

	spin_lock_irqsave(&opl4->reg_lock, flags);
	value = snd_opl4_read(opl4, reg);
	spin_unlock_irqrestore(&opl4->reg_lock, flags);
	ucontrol->value.integer.value[0] = 7 - (value & 7);
	ucontrol->value.integer.value[1] = 7 - ((value >> 3) & 7);
	return 0;
}

static int snd_opl4_ctl_put(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	opl4_t *opl4 = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	u8 reg = kcontrol->private_value;
	u8 value, old_value;

	value = (7 - (ucontrol->value.integer.value[0] & 7)) |
		((7 - (ucontrol->value.integer.value[1] & 7)) << 3);
	spin_lock_irqsave(&opl4->reg_lock, flags);
	old_value = snd_opl4_read(opl4, reg);
	snd_opl4_write(opl4, reg, value);
	spin_unlock_irqrestore(&opl4->reg_lock, flags);
	return value != old_value;
}

static snd_kcontrol_new_t snd_opl4_controls[] = {
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

int snd_opl4_create_mixer(opl4_t *opl4)
{
	snd_card_t *card = opl4->card;
	int i, err;

	strcat(card->mixername, ",OPL4");

	for (i = 0; i < 2; ++i) {
		err = snd_ctl_add(card, snd_ctl_new1(&snd_opl4_controls[i], opl4));
		if (err < 0)
			return err;
	}
	return 0;
}
