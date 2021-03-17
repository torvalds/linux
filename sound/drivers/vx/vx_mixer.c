// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Driver for Digigram VX soundcards
 *
 * Common mixer part
 *
 * Copyright (c) 2002 by Takashi Iwai <tiwai@suse.de>
 */

#include <sound/core.h>
#include <sound/control.h>
#include <sound/tlv.h>
#include <sound/vx_core.h>
#include "vx_cmd.h"


/*
 * write a codec data (24bit)
 */
static void vx_write_codec_reg(struct vx_core *chip, int codec, unsigned int data)
{
	if (snd_BUG_ON(!chip->ops->write_codec))
		return;

	if (chip->chip_status & VX_STAT_IS_STALE)
		return;

	mutex_lock(&chip->lock);
	chip->ops->write_codec(chip, codec, data);
	mutex_unlock(&chip->lock);
}

/*
 * Data type used to access the Codec
 */
union vx_codec_data {
	u32 l;
#ifdef SNDRV_BIG_ENDIAN
	struct w {
		u16 h;
		u16 l;
	} w;
	struct b {
		u8 hh;
		u8 mh;
		u8 ml;
		u8 ll;
	} b;
#else /* LITTLE_ENDIAN */
	struct w {
		u16 l;
		u16 h;
	} w;
	struct b {
		u8 ll;
		u8 ml;
		u8 mh;
		u8 hh;
	} b;
#endif
};

#define SET_CDC_DATA_SEL(di,s)          ((di).b.mh = (u8) (s))
#define SET_CDC_DATA_REG(di,r)          ((di).b.ml = (u8) (r))
#define SET_CDC_DATA_VAL(di,d)          ((di).b.ll = (u8) (d))
#define SET_CDC_DATA_INIT(di)           ((di).l = 0L, SET_CDC_DATA_SEL(di,XX_CODEC_SELECTOR))

/*
 * set up codec register and write the value
 * @codec: the codec id, 0 or 1
 * @reg: register index
 * @val: data value
 */
static void vx_set_codec_reg(struct vx_core *chip, int codec, int reg, int val)
{
	union vx_codec_data data;
	/* DAC control register */
	SET_CDC_DATA_INIT(data);
	SET_CDC_DATA_REG(data, reg);
	SET_CDC_DATA_VAL(data, val);
	vx_write_codec_reg(chip, codec, data.l);
}


/*
 * vx_set_analog_output_level - set the output attenuation level
 * @codec: the output codec, 0 or 1.  (1 for VXP440 only)
 * @left: left output level, 0 = mute
 * @right: right output level
 */
static void vx_set_analog_output_level(struct vx_core *chip, int codec, int left, int right)
{
	left  = chip->hw->output_level_max - left;
	right = chip->hw->output_level_max - right;

	if (chip->ops->akm_write) {
		chip->ops->akm_write(chip, XX_CODEC_LEVEL_LEFT_REGISTER, left);
		chip->ops->akm_write(chip, XX_CODEC_LEVEL_RIGHT_REGISTER, right);
	} else {
		/* convert to attenuation level: 0 = 0dB (max), 0xe3 = -113.5 dB (min) */
		vx_set_codec_reg(chip, codec, XX_CODEC_LEVEL_LEFT_REGISTER, left);
		vx_set_codec_reg(chip, codec, XX_CODEC_LEVEL_RIGHT_REGISTER, right);
	}
}


/*
 * vx_toggle_dac_mute -  mute/unmute DAC
 * @mute: 0 = unmute, 1 = mute
 */

#define DAC_ATTEN_MIN	0x08
#define DAC_ATTEN_MAX	0x38

void vx_toggle_dac_mute(struct vx_core *chip, int mute)
{
	unsigned int i;
	for (i = 0; i < chip->hw->num_codecs; i++) {
		if (chip->ops->akm_write)
			chip->ops->akm_write(chip, XX_CODEC_DAC_CONTROL_REGISTER, mute); /* XXX */
		else
			vx_set_codec_reg(chip, i, XX_CODEC_DAC_CONTROL_REGISTER,
					 mute ? DAC_ATTEN_MAX : DAC_ATTEN_MIN);
	}
}

/*
 * vx_reset_codec - reset and initialize the codecs
 */
void vx_reset_codec(struct vx_core *chip, int cold_reset)
{
	unsigned int i;
	int port = chip->type >= VX_TYPE_VXPOCKET ? 0x75 : 0x65;

	chip->ops->reset_codec(chip);

	/* AKM codecs should be initialized in reset_codec callback */
	if (! chip->ops->akm_write) {
		/* initialize old codecs */
		for (i = 0; i < chip->hw->num_codecs; i++) {
			/* DAC control register (change level when zero crossing + mute) */
			vx_set_codec_reg(chip, i, XX_CODEC_DAC_CONTROL_REGISTER, DAC_ATTEN_MAX);
			/* ADC control register */
			vx_set_codec_reg(chip, i, XX_CODEC_ADC_CONTROL_REGISTER, 0x00);
			/* Port mode register */
			vx_set_codec_reg(chip, i, XX_CODEC_PORT_MODE_REGISTER, port);
			/* Clock control register */
			vx_set_codec_reg(chip, i, XX_CODEC_CLOCK_CONTROL_REGISTER, 0x00);
		}
	}

	/* mute analog output */
	for (i = 0; i < chip->hw->num_codecs; i++) {
		chip->output_level[i][0] = 0;
		chip->output_level[i][1] = 0;
		vx_set_analog_output_level(chip, i, 0, 0);
	}
}

/*
 * change the audio input source
 * @src: the target source (VX_AUDIO_SRC_XXX)
 */
static void vx_change_audio_source(struct vx_core *chip, int src)
{
	if (chip->chip_status & VX_STAT_IS_STALE)
		return;

	mutex_lock(&chip->lock);
	chip->ops->change_audio_source(chip, src);
	mutex_unlock(&chip->lock);
}


/*
 * change the audio source if necessary and possible
 * returns 1 if the source is actually changed.
 */
int vx_sync_audio_source(struct vx_core *chip)
{
	if (chip->audio_source_target == chip->audio_source ||
	    chip->pcm_running)
		return 0;
	vx_change_audio_source(chip, chip->audio_source_target);
	chip->audio_source = chip->audio_source_target;
	return 1;
}


/*
 * audio level, mute, monitoring
 */
struct vx_audio_level {
	unsigned int has_level: 1;
	unsigned int has_monitor_level: 1;
	unsigned int has_mute: 1;
	unsigned int has_monitor_mute: 1;
	unsigned int mute;
	unsigned int monitor_mute;
	short level;
	short monitor_level;
};

static int vx_adjust_audio_level(struct vx_core *chip, int audio, int capture,
				 struct vx_audio_level *info)
{
	struct vx_rmh rmh;

	if (chip->chip_status & VX_STAT_IS_STALE)
		return -EBUSY;

        vx_init_rmh(&rmh, CMD_AUDIO_LEVEL_ADJUST);
	if (capture)
		rmh.Cmd[0] |= COMMAND_RECORD_MASK;
	/* Add Audio IO mask */
	rmh.Cmd[1] = 1 << audio;
	rmh.Cmd[2] = 0;
	if (info->has_level) {
		rmh.Cmd[0] |=  VALID_AUDIO_IO_DIGITAL_LEVEL;
		rmh.Cmd[2] |= info->level;
        }
	if (info->has_monitor_level) {
		rmh.Cmd[0] |=  VALID_AUDIO_IO_MONITORING_LEVEL;
		rmh.Cmd[2] |= ((unsigned int)info->monitor_level << 10);
        }
	if (info->has_mute) { 
		rmh.Cmd[0] |= VALID_AUDIO_IO_MUTE_LEVEL;
		if (info->mute)
			rmh.Cmd[2] |= AUDIO_IO_HAS_MUTE_LEVEL;
	}
	if (info->has_monitor_mute) {
		/* validate flag for M2 at least to unmute it */ 
		rmh.Cmd[0] |=  VALID_AUDIO_IO_MUTE_MONITORING_1 | VALID_AUDIO_IO_MUTE_MONITORING_2;
		if (info->monitor_mute)
			rmh.Cmd[2] |= AUDIO_IO_HAS_MUTE_MONITORING_1;
	}

	return vx_send_msg(chip, &rmh);
}

    
#if 0 // not used
static int vx_read_audio_level(struct vx_core *chip, int audio, int capture,
			       struct vx_audio_level *info)
{
	int err;
	struct vx_rmh rmh;

	memset(info, 0, sizeof(*info));
        vx_init_rmh(&rmh, CMD_GET_AUDIO_LEVELS);
	if (capture)
		rmh.Cmd[0] |= COMMAND_RECORD_MASK;
	/* Add Audio IO mask */
	rmh.Cmd[1] = 1 << audio;
	err = vx_send_msg(chip, &rmh);
	if (err < 0)
		return err;
	info.level = rmh.Stat[0] & MASK_DSP_WORD_LEVEL;
	info.monitor_level = (rmh.Stat[0] >> 10) & MASK_DSP_WORD_LEVEL;
	info.mute = (rmh.Stat[i] & AUDIO_IO_HAS_MUTE_LEVEL) ? 1 : 0;
	info.monitor_mute = (rmh.Stat[i] & AUDIO_IO_HAS_MUTE_MONITORING_1) ? 1 : 0;
	return 0;
}
#endif // not used

/*
 * set the monitoring level and mute state of the given audio
 * no more static, because must be called from vx_pcm to demute monitoring
 */
int vx_set_monitor_level(struct vx_core *chip, int audio, int level, int active)
{
	struct vx_audio_level info;

	memset(&info, 0, sizeof(info));
	info.has_monitor_level = 1;
	info.monitor_level = level;
	info.has_monitor_mute = 1;
	info.monitor_mute = !active;
	chip->audio_monitor[audio] = level;
	chip->audio_monitor_active[audio] = active;
	return vx_adjust_audio_level(chip, audio, 0, &info); /* playback only */
}


/*
 * set the mute status of the given audio
 */
static int vx_set_audio_switch(struct vx_core *chip, int audio, int active)
{
	struct vx_audio_level info;

	memset(&info, 0, sizeof(info));
	info.has_mute = 1;
	info.mute = !active;
	chip->audio_active[audio] = active;
	return vx_adjust_audio_level(chip, audio, 0, &info); /* playback only */
}

/*
 * set the mute status of the given audio
 */
static int vx_set_audio_gain(struct vx_core *chip, int audio, int capture, int level)
{
	struct vx_audio_level info;

	memset(&info, 0, sizeof(info));
	info.has_level = 1;
	info.level = level;
	chip->audio_gain[capture][audio] = level;
	return vx_adjust_audio_level(chip, audio, capture, &info);
}

/*
 * reset all audio levels
 */
static void vx_reset_audio_levels(struct vx_core *chip)
{
	unsigned int i, c;
	struct vx_audio_level info;

	memset(chip->audio_gain, 0, sizeof(chip->audio_gain));
	memset(chip->audio_active, 0, sizeof(chip->audio_active));
	memset(chip->audio_monitor, 0, sizeof(chip->audio_monitor));
	memset(chip->audio_monitor_active, 0, sizeof(chip->audio_monitor_active));

	for (c = 0; c < 2; c++) {
		for (i = 0; i < chip->hw->num_ins * 2; i++) {
			memset(&info, 0, sizeof(info));
			if (c == 0) {
				info.has_monitor_level = 1;
				info.has_mute = 1;
				info.has_monitor_mute = 1;
			}
			info.has_level = 1;
			info.level = CVAL_0DB; /* default: 0dB */
			vx_adjust_audio_level(chip, i, c, &info);
			chip->audio_gain[c][i] = CVAL_0DB;
			chip->audio_monitor[i] = CVAL_0DB;
		}
	}
}


/*
 * VU, peak meter record
 */

#define VU_METER_CHANNELS	2

struct vx_vu_meter {
	int saturated;
	int vu_level;
	int peak_level;
};

/*
 * get the VU and peak meter values
 * @audio: the audio index
 * @capture: 0 = playback, 1 = capture operation
 * @info: the array of vx_vu_meter records (size = 2).
 */
static int vx_get_audio_vu_meter(struct vx_core *chip, int audio, int capture, struct vx_vu_meter *info)
{
	struct vx_rmh rmh;
	int i, err;

	if (chip->chip_status & VX_STAT_IS_STALE)
		return -EBUSY;

	vx_init_rmh(&rmh, CMD_AUDIO_VU_PIC_METER);
	rmh.LgStat += 2 * VU_METER_CHANNELS;
	if (capture)
		rmh.Cmd[0] |= COMMAND_RECORD_MASK;
    
        /* Add Audio IO mask */
	rmh.Cmd[1] = 0;
	for (i = 0; i < VU_METER_CHANNELS; i++)
		rmh.Cmd[1] |= 1 << (audio + i);
	err = vx_send_msg(chip, &rmh);
	if (err < 0)
		return err;
	/* Read response */
	for (i = 0; i < 2 * VU_METER_CHANNELS; i +=2) {
		info->saturated = (rmh.Stat[0] & (1 << (audio + i))) ? 1 : 0;
		info->vu_level = rmh.Stat[i + 1];
		info->peak_level = rmh.Stat[i + 2];
		info++;
	}
	return 0;
}
   

/*
 * control API entries
 */

/*
 * output level control
 */
static int vx_output_level_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	struct vx_core *chip = snd_kcontrol_chip(kcontrol);
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = chip->hw->output_level_max;
	return 0;
}

static int vx_output_level_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct vx_core *chip = snd_kcontrol_chip(kcontrol);
	int codec = kcontrol->id.index;
	mutex_lock(&chip->mixer_mutex);
	ucontrol->value.integer.value[0] = chip->output_level[codec][0];
	ucontrol->value.integer.value[1] = chip->output_level[codec][1];
	mutex_unlock(&chip->mixer_mutex);
	return 0;
}

static int vx_output_level_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct vx_core *chip = snd_kcontrol_chip(kcontrol);
	int codec = kcontrol->id.index;
	unsigned int val[2], vmax;

	vmax = chip->hw->output_level_max;
	val[0] = ucontrol->value.integer.value[0];
	val[1] = ucontrol->value.integer.value[1];
	if (val[0] > vmax || val[1] > vmax)
		return -EINVAL;
	mutex_lock(&chip->mixer_mutex);
	if (val[0] != chip->output_level[codec][0] ||
	    val[1] != chip->output_level[codec][1]) {
		vx_set_analog_output_level(chip, codec, val[0], val[1]);
		chip->output_level[codec][0] = val[0];
		chip->output_level[codec][1] = val[1];
		mutex_unlock(&chip->mixer_mutex);
		return 1;
	}
	mutex_unlock(&chip->mixer_mutex);
	return 0;
}

static const struct snd_kcontrol_new vx_control_output_level = {
	.iface =	SNDRV_CTL_ELEM_IFACE_MIXER,
	.access =	(SNDRV_CTL_ELEM_ACCESS_READWRITE |
			 SNDRV_CTL_ELEM_ACCESS_TLV_READ),
	.name =		"Master Playback Volume",
	.info =		vx_output_level_info,
	.get =		vx_output_level_get,
	.put =		vx_output_level_put,
	/* tlv will be filled later */
};

/*
 * audio source select
 */
static int vx_audio_src_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	static const char * const texts_mic[3] = {
		"Digital", "Line", "Mic"
	};
	static const char * const texts_vx2[2] = {
		"Digital", "Analog"
	};
	struct vx_core *chip = snd_kcontrol_chip(kcontrol);

	if (chip->type >= VX_TYPE_VXPOCKET)
		return snd_ctl_enum_info(uinfo, 1, 3, texts_mic);
	else
		return snd_ctl_enum_info(uinfo, 1, 2, texts_vx2);
}

static int vx_audio_src_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct vx_core *chip = snd_kcontrol_chip(kcontrol);
	ucontrol->value.enumerated.item[0] = chip->audio_source_target;
	return 0;
}

static int vx_audio_src_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct vx_core *chip = snd_kcontrol_chip(kcontrol);

	if (chip->type >= VX_TYPE_VXPOCKET) {
		if (ucontrol->value.enumerated.item[0] > 2)
			return -EINVAL;
	} else {
		if (ucontrol->value.enumerated.item[0] > 1)
			return -EINVAL;
	}
	mutex_lock(&chip->mixer_mutex);
	if (chip->audio_source_target != ucontrol->value.enumerated.item[0]) {
		chip->audio_source_target = ucontrol->value.enumerated.item[0];
		vx_sync_audio_source(chip);
		mutex_unlock(&chip->mixer_mutex);
		return 1;
	}
	mutex_unlock(&chip->mixer_mutex);
	return 0;
}

static const struct snd_kcontrol_new vx_control_audio_src = {
	.iface =	SNDRV_CTL_ELEM_IFACE_MIXER,
	.name =		"Capture Source",
	.info =		vx_audio_src_info,
	.get =		vx_audio_src_get,
	.put =		vx_audio_src_put,
};

/*
 * clock mode selection
 */
static int vx_clock_mode_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	static const char * const texts[3] = {
		"Auto", "Internal", "External"
	};

	return snd_ctl_enum_info(uinfo, 1, 3, texts);
}

static int vx_clock_mode_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct vx_core *chip = snd_kcontrol_chip(kcontrol);
	ucontrol->value.enumerated.item[0] = chip->clock_mode;
	return 0;
}

static int vx_clock_mode_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct vx_core *chip = snd_kcontrol_chip(kcontrol);

	if (ucontrol->value.enumerated.item[0] > 2)
		return -EINVAL;
	mutex_lock(&chip->mixer_mutex);
	if (chip->clock_mode != ucontrol->value.enumerated.item[0]) {
		chip->clock_mode = ucontrol->value.enumerated.item[0];
		vx_set_clock(chip, chip->freq);
		mutex_unlock(&chip->mixer_mutex);
		return 1;
	}
	mutex_unlock(&chip->mixer_mutex);
	return 0;
}

static const struct snd_kcontrol_new vx_control_clock_mode = {
	.iface =	SNDRV_CTL_ELEM_IFACE_MIXER,
	.name =		"Clock Mode",
	.info =		vx_clock_mode_info,
	.get =		vx_clock_mode_get,
	.put =		vx_clock_mode_put,
};

/*
 * Audio Gain
 */
static int vx_audio_gain_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = CVAL_MAX;
	return 0;
}

static int vx_audio_gain_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct vx_core *chip = snd_kcontrol_chip(kcontrol);
	int audio = kcontrol->private_value & 0xff;
	int capture = (kcontrol->private_value >> 8) & 1;

	mutex_lock(&chip->mixer_mutex);
	ucontrol->value.integer.value[0] = chip->audio_gain[capture][audio];
	ucontrol->value.integer.value[1] = chip->audio_gain[capture][audio+1];
	mutex_unlock(&chip->mixer_mutex);
	return 0;
}

static int vx_audio_gain_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct vx_core *chip = snd_kcontrol_chip(kcontrol);
	int audio = kcontrol->private_value & 0xff;
	int capture = (kcontrol->private_value >> 8) & 1;
	unsigned int val[2];

	val[0] = ucontrol->value.integer.value[0];
	val[1] = ucontrol->value.integer.value[1];
	if (val[0] > CVAL_MAX || val[1] > CVAL_MAX)
		return -EINVAL;
	mutex_lock(&chip->mixer_mutex);
	if (val[0] != chip->audio_gain[capture][audio] ||
	    val[1] != chip->audio_gain[capture][audio+1]) {
		vx_set_audio_gain(chip, audio, capture, val[0]);
		vx_set_audio_gain(chip, audio+1, capture, val[1]);
		mutex_unlock(&chip->mixer_mutex);
		return 1;
	}
	mutex_unlock(&chip->mixer_mutex);
	return 0;
}

static int vx_audio_monitor_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct vx_core *chip = snd_kcontrol_chip(kcontrol);
	int audio = kcontrol->private_value & 0xff;

	mutex_lock(&chip->mixer_mutex);
	ucontrol->value.integer.value[0] = chip->audio_monitor[audio];
	ucontrol->value.integer.value[1] = chip->audio_monitor[audio+1];
	mutex_unlock(&chip->mixer_mutex);
	return 0;
}

static int vx_audio_monitor_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct vx_core *chip = snd_kcontrol_chip(kcontrol);
	int audio = kcontrol->private_value & 0xff;
	unsigned int val[2];

	val[0] = ucontrol->value.integer.value[0];
	val[1] = ucontrol->value.integer.value[1];
	if (val[0] > CVAL_MAX || val[1] > CVAL_MAX)
		return -EINVAL;

	mutex_lock(&chip->mixer_mutex);
	if (val[0] != chip->audio_monitor[audio] ||
	    val[1] != chip->audio_monitor[audio+1]) {
		vx_set_monitor_level(chip, audio, val[0],
				     chip->audio_monitor_active[audio]);
		vx_set_monitor_level(chip, audio+1, val[1],
				     chip->audio_monitor_active[audio+1]);
		mutex_unlock(&chip->mixer_mutex);
		return 1;
	}
	mutex_unlock(&chip->mixer_mutex);
	return 0;
}

#define vx_audio_sw_info	snd_ctl_boolean_stereo_info

static int vx_audio_sw_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct vx_core *chip = snd_kcontrol_chip(kcontrol);
	int audio = kcontrol->private_value & 0xff;

	mutex_lock(&chip->mixer_mutex);
	ucontrol->value.integer.value[0] = chip->audio_active[audio];
	ucontrol->value.integer.value[1] = chip->audio_active[audio+1];
	mutex_unlock(&chip->mixer_mutex);
	return 0;
}

static int vx_audio_sw_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct vx_core *chip = snd_kcontrol_chip(kcontrol);
	int audio = kcontrol->private_value & 0xff;

	mutex_lock(&chip->mixer_mutex);
	if (ucontrol->value.integer.value[0] != chip->audio_active[audio] ||
	    ucontrol->value.integer.value[1] != chip->audio_active[audio+1]) {
		vx_set_audio_switch(chip, audio,
				    !!ucontrol->value.integer.value[0]);
		vx_set_audio_switch(chip, audio+1,
				    !!ucontrol->value.integer.value[1]);
		mutex_unlock(&chip->mixer_mutex);
		return 1;
	}
	mutex_unlock(&chip->mixer_mutex);
	return 0;
}

static int vx_monitor_sw_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct vx_core *chip = snd_kcontrol_chip(kcontrol);
	int audio = kcontrol->private_value & 0xff;

	mutex_lock(&chip->mixer_mutex);
	ucontrol->value.integer.value[0] = chip->audio_monitor_active[audio];
	ucontrol->value.integer.value[1] = chip->audio_monitor_active[audio+1];
	mutex_unlock(&chip->mixer_mutex);
	return 0;
}

static int vx_monitor_sw_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct vx_core *chip = snd_kcontrol_chip(kcontrol);
	int audio = kcontrol->private_value & 0xff;

	mutex_lock(&chip->mixer_mutex);
	if (ucontrol->value.integer.value[0] != chip->audio_monitor_active[audio] ||
	    ucontrol->value.integer.value[1] != chip->audio_monitor_active[audio+1]) {
		vx_set_monitor_level(chip, audio, chip->audio_monitor[audio],
				     !!ucontrol->value.integer.value[0]);
		vx_set_monitor_level(chip, audio+1, chip->audio_monitor[audio+1],
				     !!ucontrol->value.integer.value[1]);
		mutex_unlock(&chip->mixer_mutex);
		return 1;
	}
	mutex_unlock(&chip->mixer_mutex);
	return 0;
}

static const DECLARE_TLV_DB_SCALE(db_scale_audio_gain, -10975, 25, 0);

static const struct snd_kcontrol_new vx_control_audio_gain = {
	.iface =	SNDRV_CTL_ELEM_IFACE_MIXER,
	.access =	(SNDRV_CTL_ELEM_ACCESS_READWRITE |
			 SNDRV_CTL_ELEM_ACCESS_TLV_READ),
	/* name will be filled later */
	.info =         vx_audio_gain_info,
	.get =          vx_audio_gain_get,
	.put =          vx_audio_gain_put,
	.tlv = { .p = db_scale_audio_gain },
};
static const struct snd_kcontrol_new vx_control_output_switch = {
	.iface =	SNDRV_CTL_ELEM_IFACE_MIXER,
	.name =         "PCM Playback Switch",
	.info =         vx_audio_sw_info,
	.get =          vx_audio_sw_get,
	.put =          vx_audio_sw_put
};
static const struct snd_kcontrol_new vx_control_monitor_gain = {
	.iface =	SNDRV_CTL_ELEM_IFACE_MIXER,
	.name =         "Monitoring Volume",
	.access =	(SNDRV_CTL_ELEM_ACCESS_READWRITE |
			 SNDRV_CTL_ELEM_ACCESS_TLV_READ),
	.info =         vx_audio_gain_info,	/* shared */
	.get =          vx_audio_monitor_get,
	.put =          vx_audio_monitor_put,
	.tlv = { .p = db_scale_audio_gain },
};
static const struct snd_kcontrol_new vx_control_monitor_switch = {
	.iface =	SNDRV_CTL_ELEM_IFACE_MIXER,
	.name =         "Monitoring Switch",
	.info =         vx_audio_sw_info,	/* shared */
	.get =          vx_monitor_sw_get,
	.put =          vx_monitor_sw_put
};


/*
 * IEC958 status bits
 */
static int vx_iec958_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_IEC958;
	uinfo->count = 1;
	return 0;
}

static int vx_iec958_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct vx_core *chip = snd_kcontrol_chip(kcontrol);

	mutex_lock(&chip->mixer_mutex);
	ucontrol->value.iec958.status[0] = (chip->uer_bits >> 0) & 0xff;
	ucontrol->value.iec958.status[1] = (chip->uer_bits >> 8) & 0xff;
	ucontrol->value.iec958.status[2] = (chip->uer_bits >> 16) & 0xff;
	ucontrol->value.iec958.status[3] = (chip->uer_bits >> 24) & 0xff;
	mutex_unlock(&chip->mixer_mutex);
        return 0;
}

static int vx_iec958_mask_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.iec958.status[0] = 0xff;
	ucontrol->value.iec958.status[1] = 0xff;
	ucontrol->value.iec958.status[2] = 0xff;
	ucontrol->value.iec958.status[3] = 0xff;
        return 0;
}

static int vx_iec958_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct vx_core *chip = snd_kcontrol_chip(kcontrol);
	unsigned int val;

	val = (ucontrol->value.iec958.status[0] << 0) |
	      (ucontrol->value.iec958.status[1] << 8) |
	      (ucontrol->value.iec958.status[2] << 16) |
	      (ucontrol->value.iec958.status[3] << 24);
	mutex_lock(&chip->mixer_mutex);
	if (chip->uer_bits != val) {
		chip->uer_bits = val;
		vx_set_iec958_status(chip, val);
		mutex_unlock(&chip->mixer_mutex);
		return 1;
	}
	mutex_unlock(&chip->mixer_mutex);
	return 0;
}

static const struct snd_kcontrol_new vx_control_iec958_mask = {
	.access =	SNDRV_CTL_ELEM_ACCESS_READ,
	.iface =	SNDRV_CTL_ELEM_IFACE_PCM,
	.name =		SNDRV_CTL_NAME_IEC958("",PLAYBACK,MASK),
	.info =		vx_iec958_info,	/* shared */
	.get =		vx_iec958_mask_get,
};

static const struct snd_kcontrol_new vx_control_iec958 = {
	.iface =	SNDRV_CTL_ELEM_IFACE_PCM,
	.name =         SNDRV_CTL_NAME_IEC958("",PLAYBACK,DEFAULT),
	.info =         vx_iec958_info,
	.get =          vx_iec958_get,
	.put =          vx_iec958_put
};


/*
 * VU meter
 */

#define METER_MAX	0xff
#define METER_SHIFT	16

static int vx_vu_meter_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = METER_MAX;
	return 0;
}

static int vx_vu_meter_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct vx_core *chip = snd_kcontrol_chip(kcontrol);
	struct vx_vu_meter meter[2];
	int audio = kcontrol->private_value & 0xff;
	int capture = (kcontrol->private_value >> 8) & 1;

	vx_get_audio_vu_meter(chip, audio, capture, meter);
	ucontrol->value.integer.value[0] = meter[0].vu_level >> METER_SHIFT;
	ucontrol->value.integer.value[1] = meter[1].vu_level >> METER_SHIFT;
	return 0;
}

static int vx_peak_meter_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct vx_core *chip = snd_kcontrol_chip(kcontrol);
	struct vx_vu_meter meter[2];
	int audio = kcontrol->private_value & 0xff;
	int capture = (kcontrol->private_value >> 8) & 1;

	vx_get_audio_vu_meter(chip, audio, capture, meter);
	ucontrol->value.integer.value[0] = meter[0].peak_level >> METER_SHIFT;
	ucontrol->value.integer.value[1] = meter[1].peak_level >> METER_SHIFT;
	return 0;
}

#define vx_saturation_info	snd_ctl_boolean_stereo_info

static int vx_saturation_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct vx_core *chip = snd_kcontrol_chip(kcontrol);
	struct vx_vu_meter meter[2];
	int audio = kcontrol->private_value & 0xff;

	vx_get_audio_vu_meter(chip, audio, 1, meter); /* capture only */
	ucontrol->value.integer.value[0] = meter[0].saturated;
	ucontrol->value.integer.value[1] = meter[1].saturated;
	return 0;
}

static const struct snd_kcontrol_new vx_control_vu_meter = {
	.iface =	SNDRV_CTL_ELEM_IFACE_MIXER,
	.access =	SNDRV_CTL_ELEM_ACCESS_READ | SNDRV_CTL_ELEM_ACCESS_VOLATILE,
	/* name will be filled later */
	.info =		vx_vu_meter_info,
	.get =		vx_vu_meter_get,
};

static const struct snd_kcontrol_new vx_control_peak_meter = {
	.iface =	SNDRV_CTL_ELEM_IFACE_MIXER,
	.access =	SNDRV_CTL_ELEM_ACCESS_READ | SNDRV_CTL_ELEM_ACCESS_VOLATILE,
	/* name will be filled later */
	.info =		vx_vu_meter_info,	/* shared */
	.get =		vx_peak_meter_get,
};

static const struct snd_kcontrol_new vx_control_saturation = {
	.iface =	SNDRV_CTL_ELEM_IFACE_MIXER,
	.name =		"Input Saturation",
	.access =	SNDRV_CTL_ELEM_ACCESS_READ | SNDRV_CTL_ELEM_ACCESS_VOLATILE,
	.info =		vx_saturation_info,
	.get =		vx_saturation_get,
};



/*
 *
 */

int snd_vx_mixer_new(struct vx_core *chip)
{
	unsigned int i, c;
	int err;
	struct snd_kcontrol_new temp;
	struct snd_card *card = chip->card;
	char name[32];

	strcpy(card->mixername, card->driver);

	/* output level controls */
	for (i = 0; i < chip->hw->num_outs; i++) {
		temp = vx_control_output_level;
		temp.index = i;
		temp.tlv.p = chip->hw->output_level_db_scale;
		if ((err = snd_ctl_add(card, snd_ctl_new1(&temp, chip))) < 0)
			return err;
	}

	/* PCM volumes, switches, monitoring */
	for (i = 0; i < chip->hw->num_outs; i++) {
		int val = i * 2;
		temp = vx_control_audio_gain;
		temp.index = i;
		temp.name = "PCM Playback Volume";
		temp.private_value = val;
		if ((err = snd_ctl_add(card, snd_ctl_new1(&temp, chip))) < 0)
			return err;
		temp = vx_control_output_switch;
		temp.index = i;
		temp.private_value = val;
		if ((err = snd_ctl_add(card, snd_ctl_new1(&temp, chip))) < 0)
			return err;
		temp = vx_control_monitor_gain;
		temp.index = i;
		temp.private_value = val;
		if ((err = snd_ctl_add(card, snd_ctl_new1(&temp, chip))) < 0)
			return err;
		temp = vx_control_monitor_switch;
		temp.index = i;
		temp.private_value = val;
		if ((err = snd_ctl_add(card, snd_ctl_new1(&temp, chip))) < 0)
			return err;
	}
	for (i = 0; i < chip->hw->num_outs; i++) {
		temp = vx_control_audio_gain;
		temp.index = i;
		temp.name = "PCM Capture Volume";
		temp.private_value = (i * 2) | (1 << 8);
		if ((err = snd_ctl_add(card, snd_ctl_new1(&temp, chip))) < 0)
			return err;
	}

	/* Audio source */
	if ((err = snd_ctl_add(card, snd_ctl_new1(&vx_control_audio_src, chip))) < 0)
		return err;
	/* clock mode */
	if ((err = snd_ctl_add(card, snd_ctl_new1(&vx_control_clock_mode, chip))) < 0)
		return err;
	/* IEC958 controls */
	if ((err = snd_ctl_add(card, snd_ctl_new1(&vx_control_iec958_mask, chip))) < 0)
		return err;
	if ((err = snd_ctl_add(card, snd_ctl_new1(&vx_control_iec958, chip))) < 0)
		return err;
	/* VU, peak, saturation meters */
	for (c = 0; c < 2; c++) {
		static const char * const dir[2] = { "Output", "Input" };
		for (i = 0; i < chip->hw->num_ins; i++) {
			int val = (i * 2) | (c << 8);
			if (c == 1) {
				temp = vx_control_saturation;
				temp.index = i;
				temp.private_value = val;
				if ((err = snd_ctl_add(card, snd_ctl_new1(&temp, chip))) < 0)
					return err;
			}
			sprintf(name, "%s VU Meter", dir[c]);
			temp = vx_control_vu_meter;
			temp.index = i;
			temp.name = name;
			temp.private_value = val;
			if ((err = snd_ctl_add(card, snd_ctl_new1(&temp, chip))) < 0)
				return err;
			sprintf(name, "%s Peak Meter", dir[c]);
			temp = vx_control_peak_meter;
			temp.index = i;
			temp.name = name;
			temp.private_value = val;
			if ((err = snd_ctl_add(card, snd_ctl_new1(&temp, chip))) < 0)
				return err;
		}
	}
	vx_reset_audio_levels(chip);
	return 0;
}
