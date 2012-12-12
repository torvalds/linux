/**
 * Copyright (C) 2008, Creative Technology Ltd. All Rights Reserved.
 *
 * This source file is released under GPL v2 license (no other versions).
 * See the COPYING file included in the main directory of this source
 * distribution for the license terms and conditions.
 *
 * @File	ctmixer.c
 *
 * @Brief
 * This file contains the implementation of alsa mixer device functions.
 *
 * @Author	Liu Chun
 * @Date 	May 28 2008
 *
 */


#include "ctmixer.h"
#include "ctamixer.h"
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/control.h>
#include <sound/asoundef.h>
#include <sound/pcm.h>
#include <sound/tlv.h>

enum CT_SUM_CTL {
	SUM_IN_F,
	SUM_IN_R,
	SUM_IN_C,
	SUM_IN_S,
	SUM_IN_F_C,

	NUM_CT_SUMS
};

enum CT_AMIXER_CTL {
	/* volume control mixers */
	AMIXER_MASTER_F,
	AMIXER_MASTER_R,
	AMIXER_MASTER_C,
	AMIXER_MASTER_S,
	AMIXER_PCM_F,
	AMIXER_PCM_R,
	AMIXER_PCM_C,
	AMIXER_PCM_S,
	AMIXER_SPDIFI,
	AMIXER_LINEIN,
	AMIXER_MIC,
	AMIXER_SPDIFO,
	AMIXER_WAVE_F,
	AMIXER_WAVE_R,
	AMIXER_WAVE_C,
	AMIXER_WAVE_S,
	AMIXER_MASTER_F_C,
	AMIXER_PCM_F_C,
	AMIXER_SPDIFI_C,
	AMIXER_LINEIN_C,
	AMIXER_MIC_C,

	/* this should always be the last one */
	NUM_CT_AMIXERS
};

enum CTALSA_MIXER_CTL {
	/* volume control mixers */
	MIXER_MASTER_P,
	MIXER_PCM_P,
	MIXER_LINEIN_P,
	MIXER_MIC_P,
	MIXER_SPDIFI_P,
	MIXER_SPDIFO_P,
	MIXER_WAVEF_P,
	MIXER_WAVER_P,
	MIXER_WAVEC_P,
	MIXER_WAVES_P,
	MIXER_MASTER_C,
	MIXER_PCM_C,
	MIXER_LINEIN_C,
	MIXER_MIC_C,
	MIXER_SPDIFI_C,

	/* switch control mixers */
	MIXER_PCM_C_S,
	MIXER_LINEIN_C_S,
	MIXER_MIC_C_S,
	MIXER_SPDIFI_C_S,
	MIXER_SPDIFO_P_S,
	MIXER_WAVEF_P_S,
	MIXER_WAVER_P_S,
	MIXER_WAVEC_P_S,
	MIXER_WAVES_P_S,
	MIXER_DIGITAL_IO_S,
	MIXER_IEC958_MASK,
	MIXER_IEC958_DEFAULT,
	MIXER_IEC958_STREAM,

	/* this should always be the last one */
	NUM_CTALSA_MIXERS
};

#define VOL_MIXER_START		MIXER_MASTER_P
#define VOL_MIXER_END		MIXER_SPDIFI_C
#define VOL_MIXER_NUM		(VOL_MIXER_END - VOL_MIXER_START + 1)
#define SWH_MIXER_START		MIXER_PCM_C_S
#define SWH_MIXER_END		MIXER_DIGITAL_IO_S
#define SWH_CAPTURE_START	MIXER_PCM_C_S
#define SWH_CAPTURE_END		MIXER_SPDIFI_C_S

#define CHN_NUM		2

struct ct_kcontrol_init {
	unsigned char ctl;
	char *name;
};

static struct ct_kcontrol_init
ct_kcontrol_init_table[NUM_CTALSA_MIXERS] = {
	[MIXER_MASTER_P] = {
		.ctl = 1,
		.name = "Master Playback Volume",
	},
	[MIXER_MASTER_C] = {
		.ctl = 1,
		.name = "Master Capture Volume",
	},
	[MIXER_PCM_P] = {
		.ctl = 1,
		.name = "PCM Playback Volume",
	},
	[MIXER_PCM_C] = {
		.ctl = 1,
		.name = "PCM Capture Volume",
	},
	[MIXER_LINEIN_P] = {
		.ctl = 1,
		.name = "Line Playback Volume",
	},
	[MIXER_LINEIN_C] = {
		.ctl = 1,
		.name = "Line Capture Volume",
	},
	[MIXER_MIC_P] = {
		.ctl = 1,
		.name = "Mic Playback Volume",
	},
	[MIXER_MIC_C] = {
		.ctl = 1,
		.name = "Mic Capture Volume",
	},
	[MIXER_SPDIFI_P] = {
		.ctl = 1,
		.name = "IEC958 Playback Volume",
	},
	[MIXER_SPDIFI_C] = {
		.ctl = 1,
		.name = "IEC958 Capture Volume",
	},
	[MIXER_SPDIFO_P] = {
		.ctl = 1,
		.name = "Digital Playback Volume",
	},
	[MIXER_WAVEF_P] = {
		.ctl = 1,
		.name = "Front Playback Volume",
	},
	[MIXER_WAVES_P] = {
		.ctl = 1,
		.name = "Side Playback Volume",
	},
	[MIXER_WAVEC_P] = {
		.ctl = 1,
		.name = "Center/LFE Playback Volume",
	},
	[MIXER_WAVER_P] = {
		.ctl = 1,
		.name = "Surround Playback Volume",
	},
	[MIXER_PCM_C_S] = {
		.ctl = 1,
		.name = "PCM Capture Switch",
	},
	[MIXER_LINEIN_C_S] = {
		.ctl = 1,
		.name = "Line Capture Switch",
	},
	[MIXER_MIC_C_S] = {
		.ctl = 1,
		.name = "Mic Capture Switch",
	},
	[MIXER_SPDIFI_C_S] = {
		.ctl = 1,
		.name = "IEC958 Capture Switch",
	},
	[MIXER_SPDIFO_P_S] = {
		.ctl = 1,
		.name = "Digital Playback Switch",
	},
	[MIXER_WAVEF_P_S] = {
		.ctl = 1,
		.name = "Front Playback Switch",
	},
	[MIXER_WAVES_P_S] = {
		.ctl = 1,
		.name = "Side Playback Switch",
	},
	[MIXER_WAVEC_P_S] = {
		.ctl = 1,
		.name = "Center/LFE Playback Switch",
	},
	[MIXER_WAVER_P_S] = {
		.ctl = 1,
		.name = "Surround Playback Switch",
	},
	[MIXER_DIGITAL_IO_S] = {
		.ctl = 0,
		.name = "Digit-IO Playback Switch",
	},
};

static void
ct_mixer_recording_select(struct ct_mixer *mixer, enum CT_AMIXER_CTL type);

static void
ct_mixer_recording_unselect(struct ct_mixer *mixer, enum CT_AMIXER_CTL type);

/* FIXME: this static looks like it would fail if more than one card was */
/* installed. */
static struct snd_kcontrol *kctls[2] = {NULL};

static enum CT_AMIXER_CTL get_amixer_index(enum CTALSA_MIXER_CTL alsa_index)
{
	switch (alsa_index) {
	case MIXER_MASTER_P:	return AMIXER_MASTER_F;
	case MIXER_MASTER_C:	return AMIXER_MASTER_F_C;
	case MIXER_PCM_P:	return AMIXER_PCM_F;
	case MIXER_PCM_C:
	case MIXER_PCM_C_S:	return AMIXER_PCM_F_C;
	case MIXER_LINEIN_P:	return AMIXER_LINEIN;
	case MIXER_LINEIN_C:
	case MIXER_LINEIN_C_S:	return AMIXER_LINEIN_C;
	case MIXER_MIC_P:	return AMIXER_MIC;
	case MIXER_MIC_C:
	case MIXER_MIC_C_S:	return AMIXER_MIC_C;
	case MIXER_SPDIFI_P:	return AMIXER_SPDIFI;
	case MIXER_SPDIFI_C:
	case MIXER_SPDIFI_C_S:	return AMIXER_SPDIFI_C;
	case MIXER_SPDIFO_P:	return AMIXER_SPDIFO;
	case MIXER_WAVEF_P:	return AMIXER_WAVE_F;
	case MIXER_WAVES_P:	return AMIXER_WAVE_S;
	case MIXER_WAVEC_P:	return AMIXER_WAVE_C;
	case MIXER_WAVER_P:	return AMIXER_WAVE_R;
	default:		return NUM_CT_AMIXERS;
	}
}

static enum CT_AMIXER_CTL get_recording_amixer(enum CT_AMIXER_CTL index)
{
	switch (index) {
	case AMIXER_MASTER_F:	return AMIXER_MASTER_F_C;
	case AMIXER_PCM_F:	return AMIXER_PCM_F_C;
	case AMIXER_SPDIFI:	return AMIXER_SPDIFI_C;
	case AMIXER_LINEIN:	return AMIXER_LINEIN_C;
	case AMIXER_MIC:	return AMIXER_MIC_C;
	default:		return NUM_CT_AMIXERS;
	}
}

static unsigned char
get_switch_state(struct ct_mixer *mixer, enum CTALSA_MIXER_CTL type)
{
	return (mixer->switch_state & (0x1 << (type - SWH_MIXER_START)))
		? 1 : 0;
}

static void
set_switch_state(struct ct_mixer *mixer,
		 enum CTALSA_MIXER_CTL type, unsigned char state)
{
	if (state)
		mixer->switch_state |= (0x1 << (type - SWH_MIXER_START));
	else
		mixer->switch_state &= ~(0x1 << (type - SWH_MIXER_START));
}

#if 0 /* not used */
/* Map integer value ranging from 0 to 65535 to 14-bit float value ranging
 * from 2^-6 to (1+1023/1024) */
static unsigned int uint16_to_float14(unsigned int x)
{
	unsigned int i;

	if (x < 17)
		return 0;

	x *= 2031;
	x /= 65535;
	x += 16;

	/* i <= 6 */
	for (i = 0; !(x & 0x400); i++)
		x <<= 1;

	x = (((7 - i) & 0x7) << 10) | (x & 0x3ff);

	return x;
}

static unsigned int float14_to_uint16(unsigned int x)
{
	unsigned int e;

	if (!x)
		return x;

	e = (x >> 10) & 0x7;
	x &= 0x3ff;
	x += 1024;
	x >>= (7 - e);
	x -= 16;
	x *= 65535;
	x /= 2031;

	return x;
}
#endif /* not used */

#define VOL_SCALE	0x1c
#define VOL_MAX		0x100

static const DECLARE_TLV_DB_SCALE(ct_vol_db_scale, -6400, 25, 1);

static int ct_alsa_mix_volume_info(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = VOL_MAX;

	return 0;
}

static int ct_alsa_mix_volume_get(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct ct_atc *atc = snd_kcontrol_chip(kcontrol);
	enum CT_AMIXER_CTL type = get_amixer_index(kcontrol->private_value);
	struct amixer *amixer;
	int i, val;

	for (i = 0; i < 2; i++) {
		amixer = ((struct ct_mixer *)atc->mixer)->
						amixers[type*CHN_NUM+i];
		val = amixer->ops->get_scale(amixer) / VOL_SCALE;
		if (val < 0)
			val = 0;
		else if (val > VOL_MAX)
			val = VOL_MAX;
		ucontrol->value.integer.value[i] = val;
	}

	return 0;
}

static int ct_alsa_mix_volume_put(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct ct_atc *atc = snd_kcontrol_chip(kcontrol);
	struct ct_mixer *mixer = atc->mixer;
	enum CT_AMIXER_CTL type = get_amixer_index(kcontrol->private_value);
	struct amixer *amixer;
	int i, j, val, oval, change = 0;

	for (i = 0; i < 2; i++) {
		val = ucontrol->value.integer.value[i];
		if (val < 0)
			val = 0;
		else if (val > VOL_MAX)
			val = VOL_MAX;
		val *= VOL_SCALE;
		amixer = mixer->amixers[type*CHN_NUM+i];
		oval = amixer->ops->get_scale(amixer);
		if (val != oval) {
			amixer->ops->set_scale(amixer, val);
			amixer->ops->commit_write(amixer);
			change = 1;
			/* Synchronize Master/PCM playback AMIXERs. */
			if (AMIXER_MASTER_F == type || AMIXER_PCM_F == type) {
				for (j = 1; j < 4; j++) {
					amixer = mixer->
						amixers[(type+j)*CHN_NUM+i];
					amixer->ops->set_scale(amixer, val);
					amixer->ops->commit_write(amixer);
				}
			}
		}
	}

	return change;
}

static struct snd_kcontrol_new vol_ctl = {
	.access		= SNDRV_CTL_ELEM_ACCESS_READWRITE |
			  SNDRV_CTL_ELEM_ACCESS_TLV_READ,
	.iface		= SNDRV_CTL_ELEM_IFACE_MIXER,
	.info		= ct_alsa_mix_volume_info,
	.get		= ct_alsa_mix_volume_get,
	.put		= ct_alsa_mix_volume_put,
	.tlv		= { .p =  ct_vol_db_scale },
};

static int output_switch_info(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_info *info)
{
	static const char *const names[3] = {
	  "FP Headphones", "Headphones", "Speakers"
	};

	return snd_ctl_enum_info(info, 1, 3, names);
}

static int output_switch_get(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct ct_atc *atc = snd_kcontrol_chip(kcontrol);
	ucontrol->value.enumerated.item[0] = atc->output_switch_get(atc);
	return 0;
}

static int output_switch_put(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct ct_atc *atc = snd_kcontrol_chip(kcontrol);
	if (ucontrol->value.enumerated.item[0] > 2)
		return -EINVAL;
	return atc->output_switch_put(atc, ucontrol->value.enumerated.item[0]);
}

static struct snd_kcontrol_new output_ctl = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Analog Output Playback Enum",
	.info = output_switch_info,
	.get = output_switch_get,
	.put = output_switch_put,
};

static int mic_source_switch_info(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_info *info)
{
	static const char *const names[3] = {
	  "Mic", "FP Mic", "Aux"
	};

	return snd_ctl_enum_info(info, 1, 3, names);
}

static int mic_source_switch_get(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct ct_atc *atc = snd_kcontrol_chip(kcontrol);
	ucontrol->value.enumerated.item[0] = atc->mic_source_switch_get(atc);
	return 0;
}

static int mic_source_switch_put(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct ct_atc *atc = snd_kcontrol_chip(kcontrol);
	if (ucontrol->value.enumerated.item[0] > 2)
		return -EINVAL;
	return atc->mic_source_switch_put(atc,
					ucontrol->value.enumerated.item[0]);
}

static struct snd_kcontrol_new mic_source_ctl = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Mic Source Capture Enum",
	.info = mic_source_switch_info,
	.get = mic_source_switch_get,
	.put = mic_source_switch_put,
};

static void
do_line_mic_switch(struct ct_atc *atc, enum CTALSA_MIXER_CTL type)
{

	if (MIXER_LINEIN_C_S == type) {
		atc->select_line_in(atc);
		set_switch_state(atc->mixer, MIXER_MIC_C_S, 0);
		snd_ctl_notify(atc->card, SNDRV_CTL_EVENT_MASK_VALUE,
							&kctls[1]->id);
	} else if (MIXER_MIC_C_S == type) {
		atc->select_mic_in(atc);
		set_switch_state(atc->mixer, MIXER_LINEIN_C_S, 0);
		snd_ctl_notify(atc->card, SNDRV_CTL_EVENT_MASK_VALUE,
							&kctls[0]->id);
	}
}

static void
do_digit_io_switch(struct ct_atc *atc, int state)
{
	struct ct_mixer *mixer = atc->mixer;

	if (state) {
		atc->select_digit_io(atc);
		atc->spdif_out_unmute(atc,
				get_switch_state(mixer, MIXER_SPDIFO_P_S));
		atc->spdif_in_unmute(atc, 1);
		atc->line_in_unmute(atc, 0);
		return;
	}

	if (get_switch_state(mixer, MIXER_LINEIN_C_S))
		atc->select_line_in(atc);
	else if (get_switch_state(mixer, MIXER_MIC_C_S))
		atc->select_mic_in(atc);

	atc->spdif_out_unmute(atc, 0);
	atc->spdif_in_unmute(atc, 0);
	atc->line_in_unmute(atc, 1);
	return;
}

static void do_switch(struct ct_atc *atc, enum CTALSA_MIXER_CTL type, int state)
{
	struct ct_mixer *mixer = atc->mixer;
	struct capabilities cap = atc->capabilities(atc);

	/* Do changes in mixer. */
	if ((SWH_CAPTURE_START <= type) && (SWH_CAPTURE_END >= type)) {
		if (state) {
			ct_mixer_recording_select(mixer,
						  get_amixer_index(type));
		} else {
			ct_mixer_recording_unselect(mixer,
						    get_amixer_index(type));
		}
	}
	/* Do changes out of mixer. */
	if (!cap.dedicated_mic &&
	    (MIXER_LINEIN_C_S == type || MIXER_MIC_C_S == type)) {
		if (state)
			do_line_mic_switch(atc, type);
		atc->line_in_unmute(atc, state);
	} else if (cap.dedicated_mic && (MIXER_LINEIN_C_S == type))
		atc->line_in_unmute(atc, state);
	else if (cap.dedicated_mic && (MIXER_MIC_C_S == type))
		atc->mic_unmute(atc, state);
	else if (MIXER_SPDIFI_C_S == type)
		atc->spdif_in_unmute(atc, state);
	else if (MIXER_WAVEF_P_S == type)
		atc->line_front_unmute(atc, state);
	else if (MIXER_WAVES_P_S == type)
		atc->line_surround_unmute(atc, state);
	else if (MIXER_WAVEC_P_S == type)
		atc->line_clfe_unmute(atc, state);
	else if (MIXER_WAVER_P_S == type)
		atc->line_rear_unmute(atc, state);
	else if (MIXER_SPDIFO_P_S == type)
		atc->spdif_out_unmute(atc, state);
	else if (MIXER_DIGITAL_IO_S == type)
		do_digit_io_switch(atc, state);

	return;
}

static int ct_alsa_mix_switch_info(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;
	uinfo->value.integer.step = 1;

	return 0;
}

static int ct_alsa_mix_switch_get(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct ct_mixer *mixer =
		((struct ct_atc *)snd_kcontrol_chip(kcontrol))->mixer;
	enum CTALSA_MIXER_CTL type = kcontrol->private_value;

	ucontrol->value.integer.value[0] = get_switch_state(mixer, type);
	return 0;
}

static int ct_alsa_mix_switch_put(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct ct_atc *atc = snd_kcontrol_chip(kcontrol);
	struct ct_mixer *mixer = atc->mixer;
	enum CTALSA_MIXER_CTL type = kcontrol->private_value;
	int state;

	state = ucontrol->value.integer.value[0];
	if (get_switch_state(mixer, type) == state)
		return 0;

	set_switch_state(mixer, type, state);
	do_switch(atc, type, state);

	return 1;
}

static struct snd_kcontrol_new swh_ctl = {
	.access		= SNDRV_CTL_ELEM_ACCESS_READWRITE,
	.iface		= SNDRV_CTL_ELEM_IFACE_MIXER,
	.info		= ct_alsa_mix_switch_info,
	.get		= ct_alsa_mix_switch_get,
	.put		= ct_alsa_mix_switch_put
};

static int ct_spdif_info(struct snd_kcontrol *kcontrol,
			 struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_IEC958;
	uinfo->count = 1;
	return 0;
}

static int ct_spdif_get_mask(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.iec958.status[0] = 0xff;
	ucontrol->value.iec958.status[1] = 0xff;
	ucontrol->value.iec958.status[2] = 0xff;
	ucontrol->value.iec958.status[3] = 0xff;
	return 0;
}

static int ct_spdif_get(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct ct_atc *atc = snd_kcontrol_chip(kcontrol);
	unsigned int status;

	atc->spdif_out_get_status(atc, &status);

	if (status == 0)
		status = SNDRV_PCM_DEFAULT_CON_SPDIF;

	ucontrol->value.iec958.status[0] = (status >> 0) & 0xff;
	ucontrol->value.iec958.status[1] = (status >> 8) & 0xff;
	ucontrol->value.iec958.status[2] = (status >> 16) & 0xff;
	ucontrol->value.iec958.status[3] = (status >> 24) & 0xff;

	return 0;
}

static int ct_spdif_put(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct ct_atc *atc = snd_kcontrol_chip(kcontrol);
	int change;
	unsigned int status, old_status;

	status = (ucontrol->value.iec958.status[0] << 0) |
		 (ucontrol->value.iec958.status[1] << 8) |
		 (ucontrol->value.iec958.status[2] << 16) |
		 (ucontrol->value.iec958.status[3] << 24);

	atc->spdif_out_get_status(atc, &old_status);
	change = (old_status != status);
	if (change)
		atc->spdif_out_set_status(atc, status);

	return change;
}

static struct snd_kcontrol_new iec958_mask_ctl = {
	.access		= SNDRV_CTL_ELEM_ACCESS_READ,
	.iface		= SNDRV_CTL_ELEM_IFACE_PCM,
	.name		= SNDRV_CTL_NAME_IEC958("", PLAYBACK, MASK),
	.count		= 1,
	.info		= ct_spdif_info,
	.get		= ct_spdif_get_mask,
	.private_value	= MIXER_IEC958_MASK
};

static struct snd_kcontrol_new iec958_default_ctl = {
	.iface		= SNDRV_CTL_ELEM_IFACE_PCM,
	.name		= SNDRV_CTL_NAME_IEC958("", PLAYBACK, DEFAULT),
	.count		= 1,
	.info		= ct_spdif_info,
	.get		= ct_spdif_get,
	.put		= ct_spdif_put,
	.private_value	= MIXER_IEC958_DEFAULT
};

static struct snd_kcontrol_new iec958_ctl = {
	.access		= SNDRV_CTL_ELEM_ACCESS_READWRITE,
	.iface		= SNDRV_CTL_ELEM_IFACE_PCM,
	.name		= SNDRV_CTL_NAME_IEC958("", PLAYBACK, PCM_STREAM),
	.count		= 1,
	.info		= ct_spdif_info,
	.get		= ct_spdif_get,
	.put		= ct_spdif_put,
	.private_value	= MIXER_IEC958_STREAM
};

#define NUM_IEC958_CTL 3

static int
ct_mixer_kcontrol_new(struct ct_mixer *mixer, struct snd_kcontrol_new *new)
{
	struct snd_kcontrol *kctl;
	int err;

	kctl = snd_ctl_new1(new, mixer->atc);
	if (!kctl)
		return -ENOMEM;

	if (SNDRV_CTL_ELEM_IFACE_PCM == kctl->id.iface)
		kctl->id.device = IEC958;

	err = snd_ctl_add(mixer->atc->card, kctl);
	if (err)
		return err;

	switch (new->private_value) {
	case MIXER_LINEIN_C_S:
		kctls[0] = kctl; break;
	case MIXER_MIC_C_S:
		kctls[1] = kctl; break;
	default:
		break;
	}

	return 0;
}

static int ct_mixer_kcontrols_create(struct ct_mixer *mixer)
{
	enum CTALSA_MIXER_CTL type;
	struct ct_atc *atc = mixer->atc;
	struct capabilities cap = atc->capabilities(atc);
	int err;

	/* Create snd kcontrol instances on demand */
	for (type = VOL_MIXER_START; type <= VOL_MIXER_END; type++) {
		if (ct_kcontrol_init_table[type].ctl) {
			vol_ctl.name = ct_kcontrol_init_table[type].name;
			vol_ctl.private_value = (unsigned long)type;
			err = ct_mixer_kcontrol_new(mixer, &vol_ctl);
			if (err)
				return err;
		}
	}

	ct_kcontrol_init_table[MIXER_DIGITAL_IO_S].ctl = cap.digit_io_switch;

	for (type = SWH_MIXER_START; type <= SWH_MIXER_END; type++) {
		if (ct_kcontrol_init_table[type].ctl) {
			swh_ctl.name = ct_kcontrol_init_table[type].name;
			swh_ctl.private_value = (unsigned long)type;
			err = ct_mixer_kcontrol_new(mixer, &swh_ctl);
			if (err)
				return err;
		}
	}

	err = ct_mixer_kcontrol_new(mixer, &iec958_mask_ctl);
	if (err)
		return err;

	err = ct_mixer_kcontrol_new(mixer, &iec958_default_ctl);
	if (err)
		return err;

	err = ct_mixer_kcontrol_new(mixer, &iec958_ctl);
	if (err)
		return err;

	if (cap.output_switch) {
		err = ct_mixer_kcontrol_new(mixer, &output_ctl);
		if (err)
			return err;
	}

	if (cap.mic_source_switch) {
		err = ct_mixer_kcontrol_new(mixer, &mic_source_ctl);
		if (err)
			return err;
	}
	atc->line_front_unmute(atc, 1);
	set_switch_state(mixer, MIXER_WAVEF_P_S, 1);
	atc->line_surround_unmute(atc, 0);
	set_switch_state(mixer, MIXER_WAVES_P_S, 0);
	atc->line_clfe_unmute(atc, 0);
	set_switch_state(mixer, MIXER_WAVEC_P_S, 0);
	atc->line_rear_unmute(atc, 0);
	set_switch_state(mixer, MIXER_WAVER_P_S, 0);
	atc->spdif_out_unmute(atc, 0);
	set_switch_state(mixer, MIXER_SPDIFO_P_S, 0);
	atc->line_in_unmute(atc, 0);
	if (cap.dedicated_mic)
		atc->mic_unmute(atc, 0);
	atc->spdif_in_unmute(atc, 0);
	set_switch_state(mixer, MIXER_PCM_C_S, 0);
	set_switch_state(mixer, MIXER_LINEIN_C_S, 0);
	set_switch_state(mixer, MIXER_SPDIFI_C_S, 0);

	return 0;
}

static void
ct_mixer_recording_select(struct ct_mixer *mixer, enum CT_AMIXER_CTL type)
{
	struct amixer *amix_d;
	struct sum *sum_c;
	int i;

	for (i = 0; i < 2; i++) {
		amix_d = mixer->amixers[type*CHN_NUM+i];
		sum_c = mixer->sums[SUM_IN_F_C*CHN_NUM+i];
		amix_d->ops->set_sum(amix_d, sum_c);
		amix_d->ops->commit_write(amix_d);
	}
}

static void
ct_mixer_recording_unselect(struct ct_mixer *mixer, enum CT_AMIXER_CTL type)
{
	struct amixer *amix_d;
	int i;

	for (i = 0; i < 2; i++) {
		amix_d = mixer->amixers[type*CHN_NUM+i];
		amix_d->ops->set_sum(amix_d, NULL);
		amix_d->ops->commit_write(amix_d);
	}
}

static int ct_mixer_get_resources(struct ct_mixer *mixer)
{
	struct sum_mgr *sum_mgr;
	struct sum *sum;
	struct sum_desc sum_desc = {0};
	struct amixer_mgr *amixer_mgr;
	struct amixer *amixer;
	struct amixer_desc am_desc = {0};
	int err;
	int i;

	/* Allocate sum resources for mixer obj */
	sum_mgr = (struct sum_mgr *)mixer->atc->rsc_mgrs[SUM];
	sum_desc.msr = mixer->atc->msr;
	for (i = 0; i < (NUM_CT_SUMS * CHN_NUM); i++) {
		err = sum_mgr->get_sum(sum_mgr, &sum_desc, &sum);
		if (err) {
			printk(KERN_ERR "ctxfi:Failed to get sum resources for "
					  "front output!\n");
			break;
		}
		mixer->sums[i] = sum;
	}
	if (err)
		goto error1;

	/* Allocate amixer resources for mixer obj */
	amixer_mgr = (struct amixer_mgr *)mixer->atc->rsc_mgrs[AMIXER];
	am_desc.msr = mixer->atc->msr;
	for (i = 0; i < (NUM_CT_AMIXERS * CHN_NUM); i++) {
		err = amixer_mgr->get_amixer(amixer_mgr, &am_desc, &amixer);
		if (err) {
			printk(KERN_ERR "ctxfi:Failed to get amixer resources "
			       "for mixer obj!\n");
			break;
		}
		mixer->amixers[i] = amixer;
	}
	if (err)
		goto error2;

	return 0;

error2:
	for (i = 0; i < (NUM_CT_AMIXERS * CHN_NUM); i++) {
		if (NULL != mixer->amixers[i]) {
			amixer = mixer->amixers[i];
			amixer_mgr->put_amixer(amixer_mgr, amixer);
			mixer->amixers[i] = NULL;
		}
	}
error1:
	for (i = 0; i < (NUM_CT_SUMS * CHN_NUM); i++) {
		if (NULL != mixer->sums[i]) {
			sum_mgr->put_sum(sum_mgr, (struct sum *)mixer->sums[i]);
			mixer->sums[i] = NULL;
		}
	}

	return err;
}

static int ct_mixer_get_mem(struct ct_mixer **rmixer)
{
	struct ct_mixer *mixer;
	int err;

	*rmixer = NULL;
	/* Allocate mem for mixer obj */
	mixer = kzalloc(sizeof(*mixer), GFP_KERNEL);
	if (!mixer)
		return -ENOMEM;

	mixer->amixers = kzalloc(sizeof(void *)*(NUM_CT_AMIXERS*CHN_NUM),
				 GFP_KERNEL);
	if (!mixer->amixers) {
		err = -ENOMEM;
		goto error1;
	}
	mixer->sums = kzalloc(sizeof(void *)*(NUM_CT_SUMS*CHN_NUM), GFP_KERNEL);
	if (!mixer->sums) {
		err = -ENOMEM;
		goto error2;
	}

	*rmixer = mixer;
	return 0;

error2:
	kfree(mixer->amixers);
error1:
	kfree(mixer);
	return err;
}

static int ct_mixer_topology_build(struct ct_mixer *mixer)
{
	struct sum *sum;
	struct amixer *amix_d, *amix_s;
	enum CT_AMIXER_CTL i, j;

	/* Build topology from destination to source */

	/* Set up Master mixer */
	for (i = AMIXER_MASTER_F, j = SUM_IN_F;
					i <= AMIXER_MASTER_S; i++, j++) {
		amix_d = mixer->amixers[i*CHN_NUM];
		sum = mixer->sums[j*CHN_NUM];
		amix_d->ops->setup(amix_d, &sum->rsc, INIT_VOL, NULL);
		amix_d = mixer->amixers[i*CHN_NUM+1];
		sum = mixer->sums[j*CHN_NUM+1];
		amix_d->ops->setup(amix_d, &sum->rsc, INIT_VOL, NULL);
	}

	/* Set up Wave-out mixer */
	for (i = AMIXER_WAVE_F, j = AMIXER_MASTER_F;
					i <= AMIXER_WAVE_S; i++, j++) {
		amix_d = mixer->amixers[i*CHN_NUM];
		amix_s = mixer->amixers[j*CHN_NUM];
		amix_d->ops->setup(amix_d, &amix_s->rsc, INIT_VOL, NULL);
		amix_d = mixer->amixers[i*CHN_NUM+1];
		amix_s = mixer->amixers[j*CHN_NUM+1];
		amix_d->ops->setup(amix_d, &amix_s->rsc, INIT_VOL, NULL);
	}

	/* Set up S/PDIF-out mixer */
	amix_d = mixer->amixers[AMIXER_SPDIFO*CHN_NUM];
	amix_s = mixer->amixers[AMIXER_MASTER_F*CHN_NUM];
	amix_d->ops->setup(amix_d, &amix_s->rsc, INIT_VOL, NULL);
	amix_d = mixer->amixers[AMIXER_SPDIFO*CHN_NUM+1];
	amix_s = mixer->amixers[AMIXER_MASTER_F*CHN_NUM+1];
	amix_d->ops->setup(amix_d, &amix_s->rsc, INIT_VOL, NULL);

	/* Set up PCM-in mixer */
	for (i = AMIXER_PCM_F, j = SUM_IN_F; i <= AMIXER_PCM_S; i++, j++) {
		amix_d = mixer->amixers[i*CHN_NUM];
		sum = mixer->sums[j*CHN_NUM];
		amix_d->ops->setup(amix_d, NULL, INIT_VOL, sum);
		amix_d = mixer->amixers[i*CHN_NUM+1];
		sum = mixer->sums[j*CHN_NUM+1];
		amix_d->ops->setup(amix_d, NULL, INIT_VOL, sum);
	}

	/* Set up Line-in mixer */
	amix_d = mixer->amixers[AMIXER_LINEIN*CHN_NUM];
	sum = mixer->sums[SUM_IN_F*CHN_NUM];
	amix_d->ops->setup(amix_d, NULL, INIT_VOL, sum);
	amix_d = mixer->amixers[AMIXER_LINEIN*CHN_NUM+1];
	sum = mixer->sums[SUM_IN_F*CHN_NUM+1];
	amix_d->ops->setup(amix_d, NULL, INIT_VOL, sum);

	/* Set up Mic-in mixer */
	amix_d = mixer->amixers[AMIXER_MIC*CHN_NUM];
	sum = mixer->sums[SUM_IN_F*CHN_NUM];
	amix_d->ops->setup(amix_d, NULL, INIT_VOL, sum);
	amix_d = mixer->amixers[AMIXER_MIC*CHN_NUM+1];
	sum = mixer->sums[SUM_IN_F*CHN_NUM+1];
	amix_d->ops->setup(amix_d, NULL, INIT_VOL, sum);

	/* Set up S/PDIF-in mixer */
	amix_d = mixer->amixers[AMIXER_SPDIFI*CHN_NUM];
	sum = mixer->sums[SUM_IN_F*CHN_NUM];
	amix_d->ops->setup(amix_d, NULL, INIT_VOL, sum);
	amix_d = mixer->amixers[AMIXER_SPDIFI*CHN_NUM+1];
	sum = mixer->sums[SUM_IN_F*CHN_NUM+1];
	amix_d->ops->setup(amix_d, NULL, INIT_VOL, sum);

	/* Set up Master recording mixer */
	amix_d = mixer->amixers[AMIXER_MASTER_F_C*CHN_NUM];
	sum = mixer->sums[SUM_IN_F_C*CHN_NUM];
	amix_d->ops->setup(amix_d, &sum->rsc, INIT_VOL, NULL);
	amix_d = mixer->amixers[AMIXER_MASTER_F_C*CHN_NUM+1];
	sum = mixer->sums[SUM_IN_F_C*CHN_NUM+1];
	amix_d->ops->setup(amix_d, &sum->rsc, INIT_VOL, NULL);

	/* Set up PCM-in recording mixer */
	amix_d = mixer->amixers[AMIXER_PCM_F_C*CHN_NUM];
	sum = mixer->sums[SUM_IN_F_C*CHN_NUM];
	amix_d->ops->setup(amix_d, NULL, INIT_VOL, sum);
	amix_d = mixer->amixers[AMIXER_PCM_F_C*CHN_NUM+1];
	sum = mixer->sums[SUM_IN_F_C*CHN_NUM+1];
	amix_d->ops->setup(amix_d, NULL, INIT_VOL, sum);

	/* Set up Line-in recording mixer */
	amix_d = mixer->amixers[AMIXER_LINEIN_C*CHN_NUM];
	sum = mixer->sums[SUM_IN_F_C*CHN_NUM];
	amix_d->ops->setup(amix_d, NULL, INIT_VOL, sum);
	amix_d = mixer->amixers[AMIXER_LINEIN_C*CHN_NUM+1];
	sum = mixer->sums[SUM_IN_F_C*CHN_NUM+1];
	amix_d->ops->setup(amix_d, NULL, INIT_VOL, sum);

	/* Set up Mic-in recording mixer */
	amix_d = mixer->amixers[AMIXER_MIC_C*CHN_NUM];
	sum = mixer->sums[SUM_IN_F_C*CHN_NUM];
	amix_d->ops->setup(amix_d, NULL, INIT_VOL, sum);
	amix_d = mixer->amixers[AMIXER_MIC_C*CHN_NUM+1];
	sum = mixer->sums[SUM_IN_F_C*CHN_NUM+1];
	amix_d->ops->setup(amix_d, NULL, INIT_VOL, sum);

	/* Set up S/PDIF-in recording mixer */
	amix_d = mixer->amixers[AMIXER_SPDIFI_C*CHN_NUM];
	sum = mixer->sums[SUM_IN_F_C*CHN_NUM];
	amix_d->ops->setup(amix_d, NULL, INIT_VOL, sum);
	amix_d = mixer->amixers[AMIXER_SPDIFI_C*CHN_NUM+1];
	sum = mixer->sums[SUM_IN_F_C*CHN_NUM+1];
	amix_d->ops->setup(amix_d, NULL, INIT_VOL, sum);

	return 0;
}

static int mixer_set_input_port(struct amixer *amixer, struct rsc *rsc)
{
	amixer->ops->set_input(amixer, rsc);
	amixer->ops->commit_write(amixer);

	return 0;
}

static enum CT_AMIXER_CTL port_to_amixer(enum MIXER_PORT_T type)
{
	switch (type) {
	case MIX_WAVE_FRONT:	return AMIXER_WAVE_F;
	case MIX_WAVE_SURROUND:	return AMIXER_WAVE_S;
	case MIX_WAVE_CENTLFE:	return AMIXER_WAVE_C;
	case MIX_WAVE_REAR:	return AMIXER_WAVE_R;
	case MIX_PCMO_FRONT:	return AMIXER_MASTER_F_C;
	case MIX_SPDIF_OUT:	return AMIXER_SPDIFO;
	case MIX_LINE_IN:	return AMIXER_LINEIN;
	case MIX_MIC_IN:	return AMIXER_MIC;
	case MIX_SPDIF_IN:	return AMIXER_SPDIFI;
	case MIX_PCMI_FRONT:	return AMIXER_PCM_F;
	case MIX_PCMI_SURROUND:	return AMIXER_PCM_S;
	case MIX_PCMI_CENTLFE:	return AMIXER_PCM_C;
	case MIX_PCMI_REAR:	return AMIXER_PCM_R;
	default: 		return 0;
	}
}

static int mixer_get_output_ports(struct ct_mixer *mixer,
				  enum MIXER_PORT_T type,
				  struct rsc **rleft, struct rsc **rright)
{
	enum CT_AMIXER_CTL amix = port_to_amixer(type);

	if (NULL != rleft)
		*rleft = &((struct amixer *)mixer->amixers[amix*CHN_NUM])->rsc;

	if (NULL != rright)
		*rright =
			&((struct amixer *)mixer->amixers[amix*CHN_NUM+1])->rsc;

	return 0;
}

static int mixer_set_input_left(struct ct_mixer *mixer,
				enum MIXER_PORT_T type, struct rsc *rsc)
{
	enum CT_AMIXER_CTL amix = port_to_amixer(type);

	mixer_set_input_port(mixer->amixers[amix*CHN_NUM], rsc);
	amix = get_recording_amixer(amix);
	if (amix < NUM_CT_AMIXERS)
		mixer_set_input_port(mixer->amixers[amix*CHN_NUM], rsc);

	return 0;
}

static int
mixer_set_input_right(struct ct_mixer *mixer,
		      enum MIXER_PORT_T type, struct rsc *rsc)
{
	enum CT_AMIXER_CTL amix = port_to_amixer(type);

	mixer_set_input_port(mixer->amixers[amix*CHN_NUM+1], rsc);
	amix = get_recording_amixer(amix);
	if (amix < NUM_CT_AMIXERS)
		mixer_set_input_port(mixer->amixers[amix*CHN_NUM+1], rsc);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int mixer_resume(struct ct_mixer *mixer)
{
	int i, state;
	struct amixer *amixer;

	/* resume topology and volume gain. */
	for (i = 0; i < NUM_CT_AMIXERS*CHN_NUM; i++) {
		amixer = mixer->amixers[i];
		amixer->ops->commit_write(amixer);
	}

	/* resume switch state. */
	for (i = SWH_MIXER_START; i <= SWH_MIXER_END; i++) {
		state = get_switch_state(mixer, i);
		do_switch(mixer->atc, i, state);
	}

	return 0;
}
#endif

int ct_mixer_destroy(struct ct_mixer *mixer)
{
	struct sum_mgr *sum_mgr = (struct sum_mgr *)mixer->atc->rsc_mgrs[SUM];
	struct amixer_mgr *amixer_mgr =
			(struct amixer_mgr *)mixer->atc->rsc_mgrs[AMIXER];
	struct amixer *amixer;
	int i = 0;

	/* Release amixer resources */
	for (i = 0; i < (NUM_CT_AMIXERS * CHN_NUM); i++) {
		if (NULL != mixer->amixers[i]) {
			amixer = mixer->amixers[i];
			amixer_mgr->put_amixer(amixer_mgr, amixer);
		}
	}

	/* Release sum resources */
	for (i = 0; i < (NUM_CT_SUMS * CHN_NUM); i++) {
		if (NULL != mixer->sums[i])
			sum_mgr->put_sum(sum_mgr, (struct sum *)mixer->sums[i]);
	}

	/* Release mem assigned to mixer object */
	kfree(mixer->sums);
	kfree(mixer->amixers);
	kfree(mixer);

	return 0;
}

int ct_mixer_create(struct ct_atc *atc, struct ct_mixer **rmixer)
{
	struct ct_mixer *mixer;
	int err;

	*rmixer = NULL;

	/* Allocate mem for mixer obj */
	err = ct_mixer_get_mem(&mixer);
	if (err)
		return err;

	mixer->switch_state = 0;
	mixer->atc = atc;
	/* Set operations */
	mixer->get_output_ports = mixer_get_output_ports;
	mixer->set_input_left = mixer_set_input_left;
	mixer->set_input_right = mixer_set_input_right;
#ifdef CONFIG_PM_SLEEP
	mixer->resume = mixer_resume;
#endif

	/* Allocate chip resources for mixer obj */
	err = ct_mixer_get_resources(mixer);
	if (err)
		goto error;

	/* Build internal mixer topology */
	ct_mixer_topology_build(mixer);

	*rmixer = mixer;

	return 0;

error:
	ct_mixer_destroy(mixer);
	return err;
}

int ct_alsa_mix_create(struct ct_atc *atc,
		       enum CTALSADEVS device,
		       const char *device_name)
{
	int err;

	/* Create snd kcontrol instances on demand */
	/* vol_ctl.device = swh_ctl.device = device; */ /* better w/ device 0 */
	err = ct_mixer_kcontrols_create((struct ct_mixer *)atc->mixer);
	if (err)
		return err;

	strcpy(atc->card->mixername, device_name);

	return 0;
}
