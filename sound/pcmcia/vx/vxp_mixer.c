/*
 * Driver for Digigram VXpocket soundcards
 *
 * VX-pocket mixer
 *
 * Copyright (c) 2002 by Takashi Iwai <tiwai@suse.de>
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

#include <sound/driver.h>
#include <sound/core.h>
#include <sound/control.h>
#include "vxpocket.h"

#define MIC_LEVEL_MIN	0
#define MIC_LEVEL_MAX	8

/*
 * mic level control (for VXPocket)
 */
static int vx_mic_level_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = MIC_LEVEL_MAX;
	return 0;
}

static int vx_mic_level_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct vx_core *_chip = snd_kcontrol_chip(kcontrol);
	struct snd_vxpocket *chip = (struct snd_vxpocket *)_chip;
	ucontrol->value.integer.value[0] = chip->mic_level;
	return 0;
}

static int vx_mic_level_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct vx_core *_chip = snd_kcontrol_chip(kcontrol);
	struct snd_vxpocket *chip = (struct snd_vxpocket *)_chip;
	mutex_lock(&_chip->mixer_mutex);
	if (chip->mic_level != ucontrol->value.integer.value[0]) {
		vx_set_mic_level(_chip, ucontrol->value.integer.value[0]);
		chip->mic_level = ucontrol->value.integer.value[0];
		mutex_unlock(&_chip->mixer_mutex);
		return 1;
	}
	mutex_unlock(&_chip->mixer_mutex);
	return 0;
}

static struct snd_kcontrol_new vx_control_mic_level = {
	.iface =	SNDRV_CTL_ELEM_IFACE_MIXER,
	.name =		"Mic Capture Volume",
	.info =		vx_mic_level_info,
	.get =		vx_mic_level_get,
	.put =		vx_mic_level_put,
};

/*
 * mic boost level control (for VXP440)
 */
static int vx_mic_boost_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;
	return 0;
}

static int vx_mic_boost_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct vx_core *_chip = snd_kcontrol_chip(kcontrol);
	struct snd_vxpocket *chip = (struct snd_vxpocket *)_chip;
	ucontrol->value.integer.value[0] = chip->mic_level;
	return 0;
}

static int vx_mic_boost_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct vx_core *_chip = snd_kcontrol_chip(kcontrol);
	struct snd_vxpocket *chip = (struct snd_vxpocket *)_chip;
	mutex_lock(&_chip->mixer_mutex);
	if (chip->mic_level != ucontrol->value.integer.value[0]) {
		vx_set_mic_boost(_chip, ucontrol->value.integer.value[0]);
		chip->mic_level = ucontrol->value.integer.value[0];
		mutex_unlock(&_chip->mixer_mutex);
		return 1;
	}
	mutex_unlock(&_chip->mixer_mutex);
	return 0;
}

static struct snd_kcontrol_new vx_control_mic_boost = {
	.iface =	SNDRV_CTL_ELEM_IFACE_MIXER,
	.name =		"Mic Boost",
	.info =		vx_mic_boost_info,
	.get =		vx_mic_boost_get,
	.put =		vx_mic_boost_put,
};


int vxp_add_mic_controls(struct vx_core *_chip)
{
	struct snd_vxpocket *chip = (struct snd_vxpocket *)_chip;
	int err;

	/* mute input levels */
	chip->mic_level = 0;
	switch (_chip->type) {
	case VX_TYPE_VXPOCKET:
		vx_set_mic_level(_chip, 0);
		break;
	case VX_TYPE_VXP440:
		vx_set_mic_boost(_chip, 0);
		break;
	}

	/* mic level */
	switch (_chip->type) {
	case VX_TYPE_VXPOCKET:
		if ((err = snd_ctl_add(_chip->card, snd_ctl_new1(&vx_control_mic_level, chip))) < 0)
			return err;
		break;
	case VX_TYPE_VXP440:
		if ((err = snd_ctl_add(_chip->card, snd_ctl_new1(&vx_control_mic_boost, chip))) < 0)
			return err;
		break;
	}

	return 0;
}

