/*
 *  Support for Digigram Lola PCI-e boards
 *
 *  Copyright (c) 2011 Takashi Iwai <tiwai@suse.de>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the Free
 *  Software Foundation; either version 2 of the License, or (at your option)
 *  any later version.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 *  more details.
 *
 *  You should have received a copy of the GNU General Public License along with
 *  this program; if not, write to the Free Software Foundation, Inc., 59
 *  Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/vmalloc.h>
#include <linux/io.h>
#include <sound/core.h>
#include <sound/control.h>
#include <sound/pcm.h>
#include <sound/tlv.h>
#include "lola.h"

static int lola_init_pin(struct lola *chip, struct lola_pin *pin,
			 int dir, int nid)
{
	unsigned int val;
	int err;

	pin->nid = nid;
	err = lola_read_param(chip, nid, LOLA_PAR_AUDIO_WIDGET_CAP, &val);
	if (err < 0) {
		dev_err(chip->card->dev, "Can't read wcaps for 0x%x\n", nid);
		return err;
	}
	val &= 0x00f00fff; /* test TYPE and bits 0..11 */
	if (val == 0x00400200)    /* Type = 4, Digital = 1 */
		pin->is_analog = false;
	else if (val == 0x0040000a && dir == CAPT) /* Dig=0, InAmp/ovrd */
		pin->is_analog = true;
	else if (val == 0x0040000c && dir == PLAY) /* Dig=0, OutAmp/ovrd */
		pin->is_analog = true;
	else {
		dev_err(chip->card->dev, "Invalid wcaps 0x%x for 0x%x\n", val, nid);
		return -EINVAL;
	}

	/* analog parameters only following, so continue in case of Digital pin
	 */
	if (!pin->is_analog)
		return 0;

	if (dir == PLAY)
		err = lola_read_param(chip, nid, LOLA_PAR_AMP_OUT_CAP, &val);
	else
		err = lola_read_param(chip, nid, LOLA_PAR_AMP_IN_CAP, &val);
	if (err < 0) {
		dev_err(chip->card->dev, "Can't read AMP-caps for 0x%x\n", nid);
		return err;
	}

	pin->amp_mute = LOLA_AMP_MUTE_CAPABLE(val);
	pin->amp_step_size = LOLA_AMP_STEP_SIZE(val);
	pin->amp_num_steps = LOLA_AMP_NUM_STEPS(val);
	if (pin->amp_num_steps) {
		/* zero as mute state */
		pin->amp_num_steps++;
		pin->amp_step_size++;
	}
	pin->amp_offset = LOLA_AMP_OFFSET(val);

	err = lola_codec_read(chip, nid, LOLA_VERB_GET_MAX_LEVEL, 0, 0, &val,
			      NULL);
	if (err < 0) {
		dev_err(chip->card->dev, "Can't get MAX_LEVEL 0x%x\n", nid);
		return err;
	}
	pin->max_level = val & 0x3ff;   /* 10 bits */

	pin->config_default_reg = 0;
	pin->fixed_gain_list_len = 0;
	pin->cur_gain_step = 0;

	return 0;
}

int lola_init_pins(struct lola *chip, int dir, int *nidp)
{
	int i, err, nid;
	nid = *nidp;
	for (i = 0; i < chip->pin[dir].num_pins; i++, nid++) {
		err = lola_init_pin(chip, &chip->pin[dir].pins[i], dir, nid);
		if (err < 0)
			return err;
		if (chip->pin[dir].pins[i].is_analog)
			chip->pin[dir].num_analog_pins++;
	}
	*nidp = nid;
	return 0;
}

void lola_free_mixer(struct lola *chip)
{
	vfree(chip->mixer.array_saved);
}

int lola_init_mixer_widget(struct lola *chip, int nid)
{
	unsigned int val;
	int err;

	err = lola_read_param(chip, nid, LOLA_PAR_AUDIO_WIDGET_CAP, &val);
	if (err < 0) {
		dev_err(chip->card->dev, "Can't read wcaps for 0x%x\n", nid);
		return err;
	}

	if ((val & 0xfff00000) != 0x02f00000) { /* test SubType and Type */
		dev_dbg(chip->card->dev, "No valid mixer widget\n");
		return 0;
	}

	chip->mixer.nid = nid;
	chip->mixer.caps = val;
	chip->mixer.array = (struct lola_mixer_array __iomem *)
		(chip->bar[BAR1].remap_addr + LOLA_BAR1_SOURCE_GAIN_ENABLE);

	/* reserve memory to copy mixer data for sleep mode transitions */
	chip->mixer.array_saved = vmalloc(sizeof(struct lola_mixer_array));

	/* mixer matrix sources are physical input data and play streams */
	chip->mixer.src_stream_outs = chip->pcm[PLAY].num_streams;
	chip->mixer.src_phys_ins = chip->pin[CAPT].num_pins;

	/* mixer matrix destinations are record streams and physical output */
	chip->mixer.dest_stream_ins = chip->pcm[CAPT].num_streams;
	chip->mixer.dest_phys_outs = chip->pin[PLAY].num_pins;

	/* mixer matrix may have unused areas between PhysIn and
	 * Play or Record and PhysOut zones
	 */
	chip->mixer.src_stream_out_ofs = chip->mixer.src_phys_ins +
		LOLA_MIXER_SRC_INPUT_PLAY_SEPARATION(val);
	chip->mixer.dest_phys_out_ofs = chip->mixer.dest_stream_ins +
		LOLA_MIXER_DEST_REC_OUTPUT_SEPARATION(val);

	/* example : MixerMatrix of LoLa881 (LoLa16161 uses unused zones)
	 * +-+  0-------8------16-------8------16
	 * | |  |       |       |       |       |
	 * |s|  | INPUT |       | INPUT |       |
	 * | |->|  ->   |unused |  ->   |unused |
	 * |r|  |CAPTURE|       | OUTPUT|       |
	 * | |  |  MIX  |       |  MIX  |       |
	 * |c|  8--------------------------------
	 * | |  |       |       |       |       |
	 * | |  |       |       |       |       |
	 * |g|  |unused |unused |unused |unused |
	 * | |  |       |       |       |       |
	 * |a|  |       |       |       |       |
	 * | |  16-------------------------------
	 * |i|  |       |       |       |       |
	 * | |  | PLAYBK|       | PLAYBK|       |
	 * |n|->|  ->   |unused |  ->   |unused |
	 * | |  |CAPTURE|       | OUTPUT|       |
	 * | |  |  MIX  |       |  MIX  |       |
	 * |a|  8--------------------------------
	 * |r|  |       |       |       |       |
	 * |r|  |       |       |       |       |
	 * |a|  |unused |unused |unused |unused |
	 * |y|  |       |       |       |       |
	 * | |  |       |       |       |       |
	 * +++  16--|---------------|------------
	 *      +---V---------------V-----------+
	 *      |  dest_mix_gain_enable array   |
	 *      +-------------------------------+
	 */
	/* example : MixerMatrix of LoLa280
	 * +-+  0-------8-2
	 * | |  |       | |
	 * |s|  | INPUT | |     INPUT
	 * |r|->|  ->   | |      ->
	 * |c|  |CAPTURE| | <-  OUTPUT
	 * | |  |  MIX  | |      MIX
	 * |g|  8----------
	 * |a|  |       | |
	 * |i|  | PLAYBK| |     PLAYBACK
	 * |n|->|  ->   | |      ->
	 * | |  |CAPTURE| | <-  OUTPUT
	 * |a|  |  MIX  | |      MIX
	 * |r|  8---|----|-
	 * |r|  +---V----V-------------------+
	 * |a|  | dest_mix_gain_enable array |
	 * |y|  +----------------------------+
	 */
	if (chip->mixer.src_stream_out_ofs > MAX_AUDIO_INOUT_COUNT ||
	    chip->mixer.dest_phys_out_ofs > MAX_STREAM_IN_COUNT) {
		dev_err(chip->card->dev, "Invalid mixer widget size\n");
		return -EINVAL;
	}

	chip->mixer.src_mask = ((1U << chip->mixer.src_phys_ins) - 1) |
		(((1U << chip->mixer.src_stream_outs) - 1)
		 << chip->mixer.src_stream_out_ofs);
	chip->mixer.dest_mask = ((1U << chip->mixer.dest_stream_ins) - 1) |
		(((1U << chip->mixer.dest_phys_outs) - 1)
		 << chip->mixer.dest_phys_out_ofs);

	dev_dbg(chip->card->dev, "Mixer src_mask=%x, dest_mask=%x\n",
		    chip->mixer.src_mask, chip->mixer.dest_mask);

	return 0;
}

static int lola_mixer_set_src_gain(struct lola *chip, unsigned int id,
				   unsigned short gain, bool on)
{
	unsigned int oldval, val;

	if (!(chip->mixer.src_mask & (1 << id)))
		return -EINVAL;
	oldval = val = readl(&chip->mixer.array->src_gain_enable);
	if (on)
		val |= (1 << id);
	else
		val &= ~(1 << id);
	/* test if values unchanged */
	if ((val == oldval) &&
	    (gain == readw(&chip->mixer.array->src_gain[id])))
		return 0;

	dev_dbg(chip->card->dev,
		"lola_mixer_set_src_gain (id=%d, gain=%d) enable=%x\n",
			id, gain, val);
	writew(gain, &chip->mixer.array->src_gain[id]);
	writel(val, &chip->mixer.array->src_gain_enable);
	lola_codec_flush(chip);
	/* inform micro-controller about the new source gain */
	return lola_codec_write(chip, chip->mixer.nid,
				LOLA_VERB_SET_SOURCE_GAIN, id, 0);
}

#if 0 /* not used */
static int lola_mixer_set_src_gains(struct lola *chip, unsigned int mask,
				    unsigned short *gains)
{
	int i;

	if ((chip->mixer.src_mask & mask) != mask)
		return -EINVAL;
	for (i = 0; i < LOLA_MIXER_DIM; i++) {
		if (mask & (1 << i)) {
			writew(*gains, &chip->mixer.array->src_gain[i]);
			gains++;
		}
	}
	writel(mask, &chip->mixer.array->src_gain_enable);
	lola_codec_flush(chip);
	if (chip->mixer.caps & LOLA_PEAK_METER_CAN_AGC_MASK) {
		/* update for all srcs at once */
		return lola_codec_write(chip, chip->mixer.nid,
					LOLA_VERB_SET_SOURCE_GAIN, 0x80, 0);
	}
	/* update manually */
	for (i = 0; i < LOLA_MIXER_DIM; i++) {
		if (mask & (1 << i)) {
			lola_codec_write(chip, chip->mixer.nid,
					 LOLA_VERB_SET_SOURCE_GAIN, i, 0);
		}
	}
	return 0;
}
#endif /* not used */

static int lola_mixer_set_mapping_gain(struct lola *chip,
				       unsigned int src, unsigned int dest,
				       unsigned short gain, bool on)
{
	unsigned int val;

	if (!(chip->mixer.src_mask & (1 << src)) ||
	    !(chip->mixer.dest_mask & (1 << dest)))
		return -EINVAL;
	if (on)
		writew(gain, &chip->mixer.array->dest_mix_gain[dest][src]);
	val = readl(&chip->mixer.array->dest_mix_gain_enable[dest]);
	if (on)
		val |= (1 << src);
	else
		val &= ~(1 << src);
	writel(val, &chip->mixer.array->dest_mix_gain_enable[dest]);
	lola_codec_flush(chip);
	return lola_codec_write(chip, chip->mixer.nid, LOLA_VERB_SET_MIX_GAIN,
				src, dest);
}

#if 0 /* not used */
static int lola_mixer_set_dest_gains(struct lola *chip, unsigned int id,
				     unsigned int mask, unsigned short *gains)
{
	int i;

	if (!(chip->mixer.dest_mask & (1 << id)) ||
	    (chip->mixer.src_mask & mask) != mask)
		return -EINVAL;
	for (i = 0; i < LOLA_MIXER_DIM; i++) {
		if (mask & (1 << i)) {
			writew(*gains, &chip->mixer.array->dest_mix_gain[id][i]);
			gains++;
		}
	}
	writel(mask, &chip->mixer.array->dest_mix_gain_enable[id]);
	lola_codec_flush(chip);
	/* update for all dests at once */
	return lola_codec_write(chip, chip->mixer.nid,
				LOLA_VERB_SET_DESTINATION_GAIN, id, 0);
}
#endif /* not used */

/*
 */

static int set_analog_volume(struct lola *chip, int dir,
			     unsigned int idx, unsigned int val,
			     bool external_call);

int lola_setup_all_analog_gains(struct lola *chip, int dir, bool mute)
{
	struct lola_pin *pin;
	int idx, max_idx;

	pin = chip->pin[dir].pins;
	max_idx = chip->pin[dir].num_pins;
	for (idx = 0; idx < max_idx; idx++) {
		if (pin[idx].is_analog) {
			unsigned int val = mute ? 0 : pin[idx].cur_gain_step;
			/* set volume and do not save the value */
			set_analog_volume(chip, dir, idx, val, false);
		}
	}
	return lola_codec_flush(chip);
}

void lola_save_mixer(struct lola *chip)
{
	/* mute analog output */
	if (chip->mixer.array_saved) {
		/* store contents of mixer array */
		memcpy_fromio(chip->mixer.array_saved, chip->mixer.array,
			      sizeof(*chip->mixer.array));
	}
	lola_setup_all_analog_gains(chip, PLAY, true); /* output mute */
}

void lola_restore_mixer(struct lola *chip)
{
	int i;

	/*lola_reset_setups(chip);*/
	if (chip->mixer.array_saved) {
		/* restore contents of mixer array */
		memcpy_toio(chip->mixer.array, chip->mixer.array_saved,
			    sizeof(*chip->mixer.array));
		/* inform micro-controller about all restored values
		 * and ignore return values
		 */
		for (i = 0; i < chip->mixer.src_phys_ins; i++)
			lola_codec_write(chip, chip->mixer.nid,
					 LOLA_VERB_SET_SOURCE_GAIN,
					 i, 0);
		for (i = 0; i < chip->mixer.src_stream_outs; i++)
			lola_codec_write(chip, chip->mixer.nid,
					 LOLA_VERB_SET_SOURCE_GAIN,
					 chip->mixer.src_stream_out_ofs + i, 0);
		for (i = 0; i < chip->mixer.dest_stream_ins; i++)
			lola_codec_write(chip, chip->mixer.nid,
					 LOLA_VERB_SET_DESTINATION_GAIN,
					 i, 0);
		for (i = 0; i < chip->mixer.dest_phys_outs; i++)
			lola_codec_write(chip, chip->mixer.nid,
					 LOLA_VERB_SET_DESTINATION_GAIN,
					 chip->mixer.dest_phys_out_ofs + i, 0);
		lola_codec_flush(chip);
	}
}

/*
 */

static int set_analog_volume(struct lola *chip, int dir,
			     unsigned int idx, unsigned int val,
			     bool external_call)
{
	struct lola_pin *pin;
	int err;

	if (idx >= chip->pin[dir].num_pins)
		return -EINVAL;
	pin = &chip->pin[dir].pins[idx];
	if (!pin->is_analog || pin->amp_num_steps <= val)
		return -EINVAL;
	if (external_call && pin->cur_gain_step == val)
		return 0;
	if (external_call)
		lola_codec_flush(chip);
	dev_dbg(chip->card->dev,
		"set_analog_volume (dir=%d idx=%d, volume=%d)\n",
			dir, idx, val);
	err = lola_codec_write(chip, pin->nid,
			       LOLA_VERB_SET_AMP_GAIN_MUTE, val, 0);
	if (err < 0)
		return err;
	if (external_call)
		pin->cur_gain_step = val;
	return 0;
}

int lola_set_src_config(struct lola *chip, unsigned int src_mask, bool update)
{
	int ret = 0;
	int success = 0;
	int n, err;

	/* SRC can be activated and the dwInputSRCMask is valid? */
	if ((chip->input_src_caps_mask & src_mask) != src_mask)
		return -EINVAL;
	/* handle all even Inputs - SRC is a stereo setting !!! */
	for (n = 0; n < chip->pin[CAPT].num_pins; n += 2) {
		unsigned int mask = 3U << n; /* handle the stereo case */
		unsigned int new_src, src_state;
		if (!(chip->input_src_caps_mask & mask))
			continue;
		/* if one IO needs SRC, both stereo IO will get SRC */
		new_src = (src_mask & mask) != 0;
		if (update) {
			src_state = (chip->input_src_mask & mask) != 0;
			if (src_state == new_src)
				continue;   /* nothing to change for this IO */
		}
		err = lola_codec_write(chip, chip->pcm[CAPT].streams[n].nid,
				       LOLA_VERB_SET_SRC, new_src, 0);
		if (!err)
			success++;
		else
			ret = err;
	}
	if (success)
		ret = lola_codec_flush(chip);
	if (!ret)
		chip->input_src_mask = src_mask;
	return ret;
}

/*
 */
static int init_mixer_values(struct lola *chip)
{
	int i;

	/* all sample rate converters on */
	lola_set_src_config(chip, (1 << chip->pin[CAPT].num_pins) - 1, false);

	/* clear all mixer matrix settings */
	memset_io(chip->mixer.array, 0, sizeof(*chip->mixer.array));
	/* inform firmware about all updated matrix columns - capture part */
	for (i = 0; i < chip->mixer.dest_stream_ins; i++)
		lola_codec_write(chip, chip->mixer.nid,
				 LOLA_VERB_SET_DESTINATION_GAIN,
				 i, 0);
	/* inform firmware about all updated matrix columns - output part */
	for (i = 0; i < chip->mixer.dest_phys_outs; i++)
		lola_codec_write(chip, chip->mixer.nid,
				 LOLA_VERB_SET_DESTINATION_GAIN,
				 chip->mixer.dest_phys_out_ofs + i, 0);

	/* set all digital input source (master) gains to 0dB */
	for (i = 0; i < chip->mixer.src_phys_ins; i++)
		lola_mixer_set_src_gain(chip, i, 336, true); /* 0dB */

	/* set all digital playback source (master) gains to 0dB */
	for (i = 0; i < chip->mixer.src_stream_outs; i++)
		lola_mixer_set_src_gain(chip,
					i + chip->mixer.src_stream_out_ofs,
					336, true); /* 0dB */
	/* set gain value 0dB diagonally in matrix - part INPUT -> CAPTURE */
	for (i = 0; i < chip->mixer.dest_stream_ins; i++) {
		int src = i % chip->mixer.src_phys_ins;
		lola_mixer_set_mapping_gain(chip, src, i, 336, true);
	}
	/* set gain value 0dB diagonally in matrix , part PLAYBACK -> OUTPUT
	 * (LoLa280 : playback channel 0,2,4,6 linked to output channel 0)
	 * (LoLa280 : playback channel 1,3,5,7 linked to output channel 1)
	 */
	for (i = 0; i < chip->mixer.src_stream_outs; i++) {
		int src = chip->mixer.src_stream_out_ofs + i;
		int dst = chip->mixer.dest_phys_out_ofs +
			i % chip->mixer.dest_phys_outs;
		lola_mixer_set_mapping_gain(chip, src, dst, 336, true);
	}
	return 0;
}

/*
 * analog mixer control element
 */
static int lola_analog_vol_info(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_info *uinfo)
{
	struct lola *chip = snd_kcontrol_chip(kcontrol);
	int dir = kcontrol->private_value;

	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = chip->pin[dir].num_pins;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = chip->pin[dir].pins[0].amp_num_steps;
	return 0;
}

static int lola_analog_vol_get(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct lola *chip = snd_kcontrol_chip(kcontrol);
	int dir = kcontrol->private_value;
	int i;

	for (i = 0; i < chip->pin[dir].num_pins; i++)
		ucontrol->value.integer.value[i] =
			chip->pin[dir].pins[i].cur_gain_step;
	return 0;
}

static int lola_analog_vol_put(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct lola *chip = snd_kcontrol_chip(kcontrol);
	int dir = kcontrol->private_value;
	int i, err;

	for (i = 0; i < chip->pin[dir].num_pins; i++) {
		err = set_analog_volume(chip, dir, i,
					ucontrol->value.integer.value[i],
					true);
		if (err < 0)
			return err;
	}
	return 0;
}

static int lola_analog_vol_tlv(struct snd_kcontrol *kcontrol, int op_flag,
			       unsigned int size, unsigned int __user *tlv)
{
	struct lola *chip = snd_kcontrol_chip(kcontrol);
	int dir = kcontrol->private_value;
	unsigned int val1, val2;
	struct lola_pin *pin;

	if (size < 4 * sizeof(unsigned int))
		return -ENOMEM;
	pin = &chip->pin[dir].pins[0];

	val2 = pin->amp_step_size * 25;
	val1 = -1 * (int)pin->amp_offset * (int)val2;
#ifdef TLV_DB_SCALE_MUTE
	val2 |= TLV_DB_SCALE_MUTE;
#endif
	if (put_user(SNDRV_CTL_TLVT_DB_SCALE, tlv))
		return -EFAULT;
	if (put_user(2 * sizeof(unsigned int), tlv + 1))
		return -EFAULT;
	if (put_user(val1, tlv + 2))
		return -EFAULT;
	if (put_user(val2, tlv + 3))
		return -EFAULT;
	return 0;
}

static struct snd_kcontrol_new lola_analog_mixer = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.access = (SNDRV_CTL_ELEM_ACCESS_READWRITE |
		   SNDRV_CTL_ELEM_ACCESS_TLV_READ |
		   SNDRV_CTL_ELEM_ACCESS_TLV_CALLBACK),
	.info = lola_analog_vol_info,
	.get = lola_analog_vol_get,
	.put = lola_analog_vol_put,
	.tlv.c = lola_analog_vol_tlv,
};

static int create_analog_mixer(struct lola *chip, int dir, char *name)
{
	if (!chip->pin[dir].num_pins)
		return 0;
	/* no analog volumes on digital only adapters */
	if (chip->pin[dir].num_pins != chip->pin[dir].num_analog_pins)
		return 0;
	lola_analog_mixer.name = name;
	lola_analog_mixer.private_value = dir;
	return snd_ctl_add(chip->card,
			   snd_ctl_new1(&lola_analog_mixer, chip));
}

/*
 * Hardware sample rate converter on digital input
 */
static int lola_input_src_info(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_info *uinfo)
{
	struct lola *chip = snd_kcontrol_chip(kcontrol);

	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = chip->pin[CAPT].num_pins;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;
	return 0;
}

static int lola_input_src_get(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct lola *chip = snd_kcontrol_chip(kcontrol);
	int i;

	for (i = 0; i < chip->pin[CAPT].num_pins; i++)
		ucontrol->value.integer.value[i] =
			!!(chip->input_src_mask & (1 << i));
	return 0;
}

static int lola_input_src_put(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct lola *chip = snd_kcontrol_chip(kcontrol);
	int i;
	unsigned int mask;

	mask = 0;
	for (i = 0; i < chip->pin[CAPT].num_pins; i++)
		if (ucontrol->value.integer.value[i])
			mask |= 1 << i;
	return lola_set_src_config(chip, mask, true);
}

static const struct snd_kcontrol_new lola_input_src_mixer = {
	.name = "Digital SRC Capture Switch",
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.info = lola_input_src_info,
	.get = lola_input_src_get,
	.put = lola_input_src_put,
};

/*
 * Lola16161 or Lola881 can have Hardware sample rate converters
 * on its digital input pins
 */
static int create_input_src_mixer(struct lola *chip)
{
	if (!chip->input_src_caps_mask)
		return 0;

	return snd_ctl_add(chip->card,
			   snd_ctl_new1(&lola_input_src_mixer, chip));
}

/*
 * src gain mixer
 */
static int lola_src_gain_info(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_info *uinfo)
{
	unsigned int count = (kcontrol->private_value >> 8) & 0xff;

	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = count;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 409;
	return 0;
}

static int lola_src_gain_get(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct lola *chip = snd_kcontrol_chip(kcontrol);
	unsigned int ofs = kcontrol->private_value & 0xff;
	unsigned int count = (kcontrol->private_value >> 8) & 0xff;
	unsigned int mask, i;

	mask = readl(&chip->mixer.array->src_gain_enable);
	for (i = 0; i < count; i++) {
		unsigned int idx = ofs + i;
		unsigned short val;
		if (!(chip->mixer.src_mask & (1 << idx)))
			return -EINVAL;
		if (mask & (1 << idx))
			val = readw(&chip->mixer.array->src_gain[idx]) + 1;
		else
			val = 0;
		ucontrol->value.integer.value[i] = val;
	}
	return 0;
}

static int lola_src_gain_put(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct lola *chip = snd_kcontrol_chip(kcontrol);
	unsigned int ofs = kcontrol->private_value & 0xff;
	unsigned int count = (kcontrol->private_value >> 8) & 0xff;
	int i, err;

	for (i = 0; i < count; i++) {
		unsigned int idx = ofs + i;
		unsigned short val = ucontrol->value.integer.value[i];
		if (val)
			val--;
		err = lola_mixer_set_src_gain(chip, idx, val, !!val);
		if (err < 0)
			return err;
	}
	return 0;
}

/* raw value: 0 = -84dB, 336 = 0dB, 408=18dB, incremented 1 for mute */
static const DECLARE_TLV_DB_SCALE(lola_src_gain_tlv, -8425, 25, 1);

static struct snd_kcontrol_new lola_src_gain_mixer = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.access = (SNDRV_CTL_ELEM_ACCESS_READWRITE |
		   SNDRV_CTL_ELEM_ACCESS_TLV_READ),
	.info = lola_src_gain_info,
	.get = lola_src_gain_get,
	.put = lola_src_gain_put,
	.tlv.p = lola_src_gain_tlv,
};

static int create_src_gain_mixer(struct lola *chip,
				 int num, int ofs, char *name)
{
	lola_src_gain_mixer.name = name;
	lola_src_gain_mixer.private_value = ofs + (num << 8);
	return snd_ctl_add(chip->card,
			   snd_ctl_new1(&lola_src_gain_mixer, chip));
}

#if 0 /* not used */
/*
 * destination gain (matrix-like) mixer
 */
static int lola_dest_gain_info(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_info *uinfo)
{
	unsigned int src_num = (kcontrol->private_value >> 8) & 0xff;

	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = src_num;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 433;
	return 0;
}

static int lola_dest_gain_get(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct lola *chip = snd_kcontrol_chip(kcontrol);
	unsigned int src_ofs = kcontrol->private_value & 0xff;
	unsigned int src_num = (kcontrol->private_value >> 8) & 0xff;
	unsigned int dst_ofs = (kcontrol->private_value >> 16) & 0xff;
	unsigned int dst, mask, i;

	dst = snd_ctl_get_ioffidx(kcontrol, &ucontrol->id) + dst_ofs;
	mask = readl(&chip->mixer.array->dest_mix_gain_enable[dst]);
	for (i = 0; i < src_num; i++) {
		unsigned int src = src_ofs + i;
		unsigned short val;
		if (!(chip->mixer.src_mask & (1 << src)))
			return -EINVAL;
		if (mask & (1 << dst))
			val = readw(&chip->mixer.array->dest_mix_gain[dst][src]) + 1;
		else
			val = 0;
		ucontrol->value.integer.value[i] = val;
	}
	return 0;
}

static int lola_dest_gain_put(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct lola *chip = snd_kcontrol_chip(kcontrol);
	unsigned int src_ofs = kcontrol->private_value & 0xff;
	unsigned int src_num = (kcontrol->private_value >> 8) & 0xff;
	unsigned int dst_ofs = (kcontrol->private_value >> 16) & 0xff;
	unsigned int dst, mask;
	unsigned short gains[MAX_STREAM_COUNT];
	int i, num;

	mask = 0;
	num = 0;
	for (i = 0; i < src_num; i++) {
		unsigned short val = ucontrol->value.integer.value[i];
		if (val) {
			gains[num++] = val - 1;
			mask |= 1 << i;
		}
	}
	mask <<= src_ofs;
	dst = snd_ctl_get_ioffidx(kcontrol, &ucontrol->id) + dst_ofs;
	return lola_mixer_set_dest_gains(chip, dst, mask, gains);
}

static const DECLARE_TLV_DB_SCALE(lola_dest_gain_tlv, -8425, 25, 1);

static struct snd_kcontrol_new lola_dest_gain_mixer = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.access = (SNDRV_CTL_ELEM_ACCESS_READWRITE |
		   SNDRV_CTL_ELEM_ACCESS_TLV_READ),
	.info = lola_dest_gain_info,
	.get = lola_dest_gain_get,
	.put = lola_dest_gain_put,
	.tlv.p = lola_dest_gain_tlv,
};

static int create_dest_gain_mixer(struct lola *chip,
				  int src_num, int src_ofs,
				  int num, int ofs, char *name)
{
	lola_dest_gain_mixer.count = num;
	lola_dest_gain_mixer.name = name;
	lola_dest_gain_mixer.private_value =
		src_ofs + (src_num << 8) + (ofs << 16) + (num << 24);
	return snd_ctl_add(chip->card,
			  snd_ctl_new1(&lola_dest_gain_mixer, chip));
}
#endif /* not used */

/*
 */
int lola_create_mixer(struct lola *chip)
{
	int err;

	err = create_analog_mixer(chip, PLAY, "Analog Playback Volume");
	if (err < 0)
		return err;
	err = create_analog_mixer(chip, CAPT, "Analog Capture Volume");
	if (err < 0)
		return err;
	err = create_input_src_mixer(chip);
	if (err < 0)
		return err;
	err = create_src_gain_mixer(chip, chip->mixer.src_phys_ins, 0,
				    "Digital Capture Volume");
	if (err < 0)
		return err;
	err = create_src_gain_mixer(chip, chip->mixer.src_stream_outs,
				    chip->mixer.src_stream_out_ofs,
				    "Digital Playback Volume");
	if (err < 0)
		return err;
#if 0
/* FIXME: buggy mixer matrix handling */
	err = create_dest_gain_mixer(chip,
				     chip->mixer.src_phys_ins, 0,
				     chip->mixer.dest_stream_ins, 0,
				     "Line Capture Volume");
	if (err < 0)
		return err;
	err = create_dest_gain_mixer(chip,
				     chip->mixer.src_stream_outs,
				     chip->mixer.src_stream_out_ofs,
				     chip->mixer.dest_stream_ins, 0,
				     "Stream-Loopback Capture Volume");
	if (err < 0)
		return err;
	err = create_dest_gain_mixer(chip,
				     chip->mixer.src_phys_ins, 0,
				     chip->mixer.dest_phys_outs,
				     chip->mixer.dest_phys_out_ofs,
				     "Line-Loopback Playback Volume");
	if (err < 0)
		return err;
	err = create_dest_gain_mixer(chip,
				     chip->mixer.src_stream_outs,
				     chip->mixer.src_stream_out_ofs,
				     chip->mixer.dest_phys_outs,
				     chip->mixer.dest_phys_out_ofs,
				     "Stream Playback Volume");
	if (err < 0)
		return err;
#endif /* FIXME */
	return init_mixer_values(chip);
}
