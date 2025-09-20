// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Driver for Digigram VXpocket soundcards
 *
 * VX-pocket mixer
 *
 * Copyright (c) 2002 by Takashi Iwai <tiwai@suse.de>
 */

#include <sound/core.h>
#include <sound/control.h>
#include <sound/tlv.h>
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
	struct snd_vxpocket *chip = to_vxpocket(_chip);
	ucontrol->value.integer.value[0] = chip->mic_level;
	return 0;
}

static int vx_mic_level_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct vx_core *_chip = snd_kcontrol_chip(kcontrol);
	struct snd_vxpocket *chip = to_vxpocket(_chip);
	unsigned int val = ucontrol->value.integer.value[0];

	if (val > MIC_LEVEL_MAX)
		return -EINVAL;
	guard(mutex)(&_chip->mixer_mutex);
	if (chip->mic_level != ucontrol->value.integer.value[0]) {
		vx_set_mic_level(_chip, ucontrol->value.integer.value[0]);
		chip->mic_level = ucontrol->value.integer.value[0];
		return 1;
	}
	return 0;
}

static const DECLARE_TLV_DB_SCALE(db_scale_mic, -21, 3, 0);

static const struct snd_kcontrol_new vx_control_mic_level = {
	.iface =	SNDRV_CTL_ELEM_IFACE_MIXER,
	.access =	(SNDRV_CTL_ELEM_ACCESS_READWRITE |
			 SNDRV_CTL_ELEM_ACCESS_TLV_READ),
	.name =		"Mic Capture Volume",
	.info =		vx_mic_level_info,
	.get =		vx_mic_level_get,
	.put =		vx_mic_level_put,
	.tlv = { .p = db_scale_mic },
};

/*
 * mic boost level control (for VXP440)
 */
#define vx_mic_boost_info		snd_ctl_boolean_mono_info

static int vx_mic_boost_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct vx_core *_chip = snd_kcontrol_chip(kcontrol);
	struct snd_vxpocket *chip = to_vxpocket(_chip);
	ucontrol->value.integer.value[0] = chip->mic_level;
	return 0;
}

static int vx_mic_boost_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct vx_core *_chip = snd_kcontrol_chip(kcontrol);
	struct snd_vxpocket *chip = to_vxpocket(_chip);
	int val = !!ucontrol->value.integer.value[0];

	guard(mutex)(&_chip->mixer_mutex);
	if (chip->mic_level != val) {
		vx_set_mic_boost(_chip, val);
		chip->mic_level = val;
		return 1;
	}
	return 0;
}

static const struct snd_kcontrol_new vx_control_mic_boost = {
	.iface =	SNDRV_CTL_ELEM_IFACE_MIXER,
	.name =		"Mic Boost",
	.info =		vx_mic_boost_info,
	.get =		vx_mic_boost_get,
	.put =		vx_mic_boost_put,
};


int vxp_add_mic_controls(struct vx_core *_chip)
{
	struct snd_vxpocket *chip = to_vxpocket(_chip);
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
		err = snd_ctl_add(_chip->card, snd_ctl_new1(&vx_control_mic_level, chip));
		if (err < 0)
			return err;
		break;
	case VX_TYPE_VXP440:
		err = snd_ctl_add(_chip->card, snd_ctl_new1(&vx_control_mic_boost, chip));
		if (err < 0)
			return err;
		break;
	}

	return 0;
}

