// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * PMac AWACS lowlevel functions
 *
 * Copyright (c) by Takashi Iwai <tiwai@suse.de>
 * code based on dmasound.c.
 */


#include <linux/io.h>
#include <asm/nvram.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <sound/core.h>
#include "pmac.h"


#ifdef CONFIG_ADB_CUDA
#define PMAC_AMP_AVAIL
#endif

#ifdef PMAC_AMP_AVAIL
struct awacs_amp {
	unsigned char amp_master;
	unsigned char amp_vol[2][2];
	unsigned char amp_tone[2];
};

#define CHECK_CUDA_AMP() (sys_ctrler == SYS_CTRLER_CUDA)

#endif /* PMAC_AMP_AVAIL */


static void snd_pmac_screamer_wait(struct snd_pmac *chip)
{
	long timeout = 2000;
	while (!(in_le32(&chip->awacs->codec_stat) & MASK_VALID)) {
		mdelay(1);
		if (! --timeout) {
			dev_dbg(chip->card->dev, "%s timeout\n", __func__);
			break;
		}
	}
}

/*
 * write AWACS register
 */
static void
snd_pmac_awacs_write(struct snd_pmac *chip, int val)
{
	long timeout = 5000000;

	if (chip->model == PMAC_SCREAMER)
		snd_pmac_screamer_wait(chip);
	out_le32(&chip->awacs->codec_ctrl, val | (chip->subframe << 22));
	while (in_le32(&chip->awacs->codec_ctrl) & MASK_NEWECMD) {
		if (! --timeout) {
			dev_dbg(chip->card->dev, "%s timeout\n", __func__);
			break;
		}
	}
}

static void
snd_pmac_awacs_write_reg(struct snd_pmac *chip, int reg, int val)
{
	snd_pmac_awacs_write(chip, val | (reg << 12));
	chip->awacs_reg[reg] = val;
}

static void
snd_pmac_awacs_write_noreg(struct snd_pmac *chip, int reg, int val)
{
	snd_pmac_awacs_write(chip, val | (reg << 12));
}

#ifdef CONFIG_PM
/* Recalibrate chip */
static void screamer_recalibrate(struct snd_pmac *chip)
{
	if (chip->model != PMAC_SCREAMER)
		return;

	/* Sorry for the horrible delays... I hope to get that improved
	 * by making the whole PM process asynchronous in a future version
	 */
	snd_pmac_awacs_write_noreg(chip, 1, chip->awacs_reg[1]);
	if (chip->manufacturer == 0x1)
		/* delay for broken crystal part */
		msleep(750);
	snd_pmac_awacs_write_noreg(chip, 1,
				   chip->awacs_reg[1] | MASK_RECALIBRATE |
				   MASK_CMUTE | MASK_AMUTE);
	snd_pmac_awacs_write_noreg(chip, 1, chip->awacs_reg[1]);
	snd_pmac_awacs_write_noreg(chip, 6, chip->awacs_reg[6]);
}

#else
#define screamer_recalibrate(chip) /* NOP */
#endif


/*
 * additional callback to set the pcm format
 */
static void snd_pmac_awacs_set_format(struct snd_pmac *chip)
{
	chip->awacs_reg[1] &= ~MASK_SAMPLERATE;
	chip->awacs_reg[1] |= chip->rate_index << 3;
	snd_pmac_awacs_write_reg(chip, 1, chip->awacs_reg[1]);
}


/*
 * AWACS volume callbacks
 */
/*
 * volumes: 0-15 stereo
 */
static int snd_pmac_awacs_info_volume(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 15;
	return 0;
}

static int snd_pmac_awacs_get_volume(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_pmac *chip = snd_kcontrol_chip(kcontrol);
	int reg = kcontrol->private_value & 0xff;
	int lshift = (kcontrol->private_value >> 8) & 0xff;
	int inverted = (kcontrol->private_value >> 16) & 1;
	unsigned long flags;
	int vol[2];

	spin_lock_irqsave(&chip->reg_lock, flags);
	vol[0] = (chip->awacs_reg[reg] >> lshift) & 0xf;
	vol[1] = chip->awacs_reg[reg] & 0xf;
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	if (inverted) {
		vol[0] = 0x0f - vol[0];
		vol[1] = 0x0f - vol[1];
	}
	ucontrol->value.integer.value[0] = vol[0];
	ucontrol->value.integer.value[1] = vol[1];
	return 0;
}

static int snd_pmac_awacs_put_volume(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_pmac *chip = snd_kcontrol_chip(kcontrol);
	int reg = kcontrol->private_value & 0xff;
	int lshift = (kcontrol->private_value >> 8) & 0xff;
	int inverted = (kcontrol->private_value >> 16) & 1;
	int val, oldval;
	unsigned long flags;
	unsigned int vol[2];

	vol[0] = ucontrol->value.integer.value[0];
	vol[1] = ucontrol->value.integer.value[1];
	if (vol[0] > 0x0f || vol[1] > 0x0f)
		return -EINVAL;
	if (inverted) {
		vol[0] = 0x0f - vol[0];
		vol[1] = 0x0f - vol[1];
	}
	vol[0] &= 0x0f;
	vol[1] &= 0x0f;
	spin_lock_irqsave(&chip->reg_lock, flags);
	oldval = chip->awacs_reg[reg];
	val = oldval & ~(0xf | (0xf << lshift));
	val |= vol[0] << lshift;
	val |= vol[1];
	if (oldval != val)
		snd_pmac_awacs_write_reg(chip, reg, val);
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	return oldval != reg;
}


#define AWACS_VOLUME(xname, xreg, xshift, xinverted) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, .index = 0, \
  .info = snd_pmac_awacs_info_volume, \
  .get = snd_pmac_awacs_get_volume, \
  .put = snd_pmac_awacs_put_volume, \
  .private_value = (xreg) | ((xshift) << 8) | ((xinverted) << 16) }

/*
 * mute master/ogain for AWACS: mono
 */
static int snd_pmac_awacs_get_switch(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_pmac *chip = snd_kcontrol_chip(kcontrol);
	int reg = kcontrol->private_value & 0xff;
	int shift = (kcontrol->private_value >> 8) & 0xff;
	int invert = (kcontrol->private_value >> 16) & 1;
	int val;
	unsigned long flags;

	spin_lock_irqsave(&chip->reg_lock, flags);
	val = (chip->awacs_reg[reg] >> shift) & 1;
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	if (invert)
		val = 1 - val;
	ucontrol->value.integer.value[0] = val;
	return 0;
}

static int snd_pmac_awacs_put_switch(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_pmac *chip = snd_kcontrol_chip(kcontrol);
	int reg = kcontrol->private_value & 0xff;
	int shift = (kcontrol->private_value >> 8) & 0xff;
	int invert = (kcontrol->private_value >> 16) & 1;
	int mask = 1 << shift;
	int val, changed;
	unsigned long flags;

	spin_lock_irqsave(&chip->reg_lock, flags);
	val = chip->awacs_reg[reg] & ~mask;
	if (ucontrol->value.integer.value[0] != invert)
		val |= mask;
	changed = chip->awacs_reg[reg] != val;
	if (changed)
		snd_pmac_awacs_write_reg(chip, reg, val);
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	return changed;
}

#define AWACS_SWITCH(xname, xreg, xshift, xinvert) \
{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER, .name = xname, .index = 0, \
  .info = snd_pmac_boolean_mono_info, \
  .get = snd_pmac_awacs_get_switch, \
  .put = snd_pmac_awacs_put_switch, \
  .private_value = (xreg) | ((xshift) << 8) | ((xinvert) << 16) }


#ifdef PMAC_AMP_AVAIL
/*
 * controls for perch/whisper extension cards, e.g. G3 desktop
 *
 * TDA7433 connected via i2c address 0x45 (= 0x8a),
 * accessed through cuda
 */
static void awacs_set_cuda(int reg, int val)
{
	struct adb_request req;
	cuda_request(&req, NULL, 5, CUDA_PACKET, CUDA_GET_SET_IIC, 0x8a,
			reg, val);
	while (! req.complete)
		cuda_poll();
}

/*
 * level = 0 - 14, 7 = 0 dB
 */
static void awacs_amp_set_tone(struct awacs_amp *amp, int bass, int treble)
{
	amp->amp_tone[0] = bass;
	amp->amp_tone[1] = treble;
	if (bass > 7)
		bass = (14 - bass) + 8;
	if (treble > 7)
		treble = (14 - treble) + 8;
	awacs_set_cuda(2, (bass << 4) | treble);
}

/*
 * vol = 0 - 31 (attenuation), 32 = mute bit, stereo
 */
static int awacs_amp_set_vol(struct awacs_amp *amp, int index,
			     int lvol, int rvol, int do_check)
{
	if (do_check && amp->amp_vol[index][0] == lvol &&
			amp->amp_vol[index][1] == rvol)
		return 0;
	awacs_set_cuda(3 + index, lvol);
	awacs_set_cuda(5 + index, rvol);
	amp->amp_vol[index][0] = lvol;
	amp->amp_vol[index][1] = rvol;
	return 1;
}

/*
 * 0 = -79 dB, 79 = 0 dB, 99 = +20 dB
 */
static void awacs_amp_set_master(struct awacs_amp *amp, int vol)
{
	amp->amp_master = vol;
	if (vol <= 79)
		vol = 32 + (79 - vol);
	else
		vol = 32 - (vol - 79);
	awacs_set_cuda(1, vol);
}

static void awacs_amp_free(struct snd_pmac *chip)
{
	struct awacs_amp *amp = chip->mixer_data;
	if (!amp)
		return;
	kfree(amp);
	chip->mixer_data = NULL;
	chip->mixer_free = NULL;
}


/*
 * mixer controls
 */
static int snd_pmac_awacs_info_volume_amp(struct snd_kcontrol *kcontrol,
					  struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 31;
	return 0;
}

static int snd_pmac_awacs_get_volume_amp(struct snd_kcontrol *kcontrol,
					 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_pmac *chip = snd_kcontrol_chip(kcontrol);
	int index = kcontrol->private_value;
	struct awacs_amp *amp = chip->mixer_data;

	ucontrol->value.integer.value[0] = 31 - (amp->amp_vol[index][0] & 31);
	ucontrol->value.integer.value[1] = 31 - (amp->amp_vol[index][1] & 31);
	return 0;
}

static int snd_pmac_awacs_put_volume_amp(struct snd_kcontrol *kcontrol,
					 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_pmac *chip = snd_kcontrol_chip(kcontrol);
	int index = kcontrol->private_value;
	int vol[2];
	struct awacs_amp *amp = chip->mixer_data;

	vol[0] = (31 - (ucontrol->value.integer.value[0] & 31))
		| (amp->amp_vol[index][0] & 32);
	vol[1] = (31 - (ucontrol->value.integer.value[1] & 31))
		| (amp->amp_vol[index][1] & 32);
	return awacs_amp_set_vol(amp, index, vol[0], vol[1], 1);
}

static int snd_pmac_awacs_get_switch_amp(struct snd_kcontrol *kcontrol,
					 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_pmac *chip = snd_kcontrol_chip(kcontrol);
	int index = kcontrol->private_value;
	struct awacs_amp *amp = chip->mixer_data;

	ucontrol->value.integer.value[0] = (amp->amp_vol[index][0] & 32)
					? 0 : 1;
	ucontrol->value.integer.value[1] = (amp->amp_vol[index][1] & 32)
					? 0 : 1;
	return 0;
}

static int snd_pmac_awacs_put_switch_amp(struct snd_kcontrol *kcontrol,
					 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_pmac *chip = snd_kcontrol_chip(kcontrol);
	int index = kcontrol->private_value;
	int vol[2];
	struct awacs_amp *amp = chip->mixer_data;

	vol[0] = (ucontrol->value.integer.value[0] ? 0 : 32)
		| (amp->amp_vol[index][0] & 31);
	vol[1] = (ucontrol->value.integer.value[1] ? 0 : 32)
		| (amp->amp_vol[index][1] & 31);
	return awacs_amp_set_vol(amp, index, vol[0], vol[1], 1);
}

static int snd_pmac_awacs_info_tone_amp(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 14;
	return 0;
}

static int snd_pmac_awacs_get_tone_amp(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_pmac *chip = snd_kcontrol_chip(kcontrol);
	int index = kcontrol->private_value;
	struct awacs_amp *amp = chip->mixer_data;

	ucontrol->value.integer.value[0] = amp->amp_tone[index];
	return 0;
}

static int snd_pmac_awacs_put_tone_amp(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_pmac *chip = snd_kcontrol_chip(kcontrol);
	int index = kcontrol->private_value;
	struct awacs_amp *amp = chip->mixer_data;
	unsigned int val;

	val = ucontrol->value.integer.value[0];
	if (val > 14)
		return -EINVAL;
	if (val != amp->amp_tone[index]) {
		amp->amp_tone[index] = val;
		awacs_amp_set_tone(amp, amp->amp_tone[0], amp->amp_tone[1]);
		return 1;
	}
	return 0;
}

static int snd_pmac_awacs_info_master_amp(struct snd_kcontrol *kcontrol,
					  struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 99;
	return 0;
}

static int snd_pmac_awacs_get_master_amp(struct snd_kcontrol *kcontrol,
					 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_pmac *chip = snd_kcontrol_chip(kcontrol);
	struct awacs_amp *amp = chip->mixer_data;

	ucontrol->value.integer.value[0] = amp->amp_master;
	return 0;
}

static int snd_pmac_awacs_put_master_amp(struct snd_kcontrol *kcontrol,
					 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_pmac *chip = snd_kcontrol_chip(kcontrol);
	struct awacs_amp *amp = chip->mixer_data;
	unsigned int val;

	val = ucontrol->value.integer.value[0];
	if (val > 99)
		return -EINVAL;
	if (val != amp->amp_master) {
		amp->amp_master = val;
		awacs_amp_set_master(amp, amp->amp_master);
		return 1;
	}
	return 0;
}

#define AMP_CH_SPK	0
#define AMP_CH_HD	1

static const struct snd_kcontrol_new snd_pmac_awacs_amp_vol[] = {
	{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	  .name = "Speaker Playback Volume",
	  .info = snd_pmac_awacs_info_volume_amp,
	  .get = snd_pmac_awacs_get_volume_amp,
	  .put = snd_pmac_awacs_put_volume_amp,
	  .private_value = AMP_CH_SPK,
	},
	{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	  .name = "Headphone Playback Volume",
	  .info = snd_pmac_awacs_info_volume_amp,
	  .get = snd_pmac_awacs_get_volume_amp,
	  .put = snd_pmac_awacs_put_volume_amp,
	  .private_value = AMP_CH_HD,
	},
	{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	  .name = "Tone Control - Bass",
	  .info = snd_pmac_awacs_info_tone_amp,
	  .get = snd_pmac_awacs_get_tone_amp,
	  .put = snd_pmac_awacs_put_tone_amp,
	  .private_value = 0,
	},
	{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	  .name = "Tone Control - Treble",
	  .info = snd_pmac_awacs_info_tone_amp,
	  .get = snd_pmac_awacs_get_tone_amp,
	  .put = snd_pmac_awacs_put_tone_amp,
	  .private_value = 1,
	},
	{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	  .name = "Amp Master Playback Volume",
	  .info = snd_pmac_awacs_info_master_amp,
	  .get = snd_pmac_awacs_get_master_amp,
	  .put = snd_pmac_awacs_put_master_amp,
	},
};

static const struct snd_kcontrol_new snd_pmac_awacs_amp_hp_sw = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Headphone Playback Switch",
	.info = snd_pmac_boolean_stereo_info,
	.get = snd_pmac_awacs_get_switch_amp,
	.put = snd_pmac_awacs_put_switch_amp,
	.private_value = AMP_CH_HD,
};

static const struct snd_kcontrol_new snd_pmac_awacs_amp_spk_sw = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Speaker Playback Switch",
	.info = snd_pmac_boolean_stereo_info,
	.get = snd_pmac_awacs_get_switch_amp,
	.put = snd_pmac_awacs_put_switch_amp,
	.private_value = AMP_CH_SPK,
};

#endif /* PMAC_AMP_AVAIL */


/*
 * mic boost for screamer
 */
static int snd_pmac_screamer_mic_boost_info(struct snd_kcontrol *kcontrol,
					    struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 3;
	return 0;
}

static int snd_pmac_screamer_mic_boost_get(struct snd_kcontrol *kcontrol,
					   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_pmac *chip = snd_kcontrol_chip(kcontrol);
	int val = 0;
	unsigned long flags;

	spin_lock_irqsave(&chip->reg_lock, flags);
	if (chip->awacs_reg[6] & MASK_MIC_BOOST)
		val |= 2;
	if (chip->awacs_reg[0] & MASK_GAINLINE)
		val |= 1;
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	ucontrol->value.integer.value[0] = val;
	return 0;
}

static int snd_pmac_screamer_mic_boost_put(struct snd_kcontrol *kcontrol,
					   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_pmac *chip = snd_kcontrol_chip(kcontrol);
	int changed = 0;
	int val0, val6;
	unsigned long flags;

	spin_lock_irqsave(&chip->reg_lock, flags);
	val0 = chip->awacs_reg[0] & ~MASK_GAINLINE;
	val6 = chip->awacs_reg[6] & ~MASK_MIC_BOOST;
	if (ucontrol->value.integer.value[0] & 1)
		val0 |= MASK_GAINLINE;
	if (ucontrol->value.integer.value[0] & 2)
		val6 |= MASK_MIC_BOOST;
	if (val0 != chip->awacs_reg[0]) {
		snd_pmac_awacs_write_reg(chip, 0, val0);
		changed = 1;
	}
	if (val6 != chip->awacs_reg[6]) {
		snd_pmac_awacs_write_reg(chip, 6, val6);
		changed = 1;
	}
	spin_unlock_irqrestore(&chip->reg_lock, flags);
	return changed;
}

/*
 * lists of mixer elements
 */
static const struct snd_kcontrol_new snd_pmac_awacs_mixers[] = {
	AWACS_SWITCH("Master Capture Switch", 1, SHIFT_LOOPTHRU, 0),
	AWACS_VOLUME("Master Capture Volume", 0, 4, 0),
/*	AWACS_SWITCH("Unknown Playback Switch", 6, SHIFT_PAROUT0, 0), */
};

static const struct snd_kcontrol_new snd_pmac_screamer_mixers_beige[] = {
	AWACS_VOLUME("Master Playback Volume", 2, 6, 1),
	AWACS_VOLUME("Play-through Playback Volume", 5, 6, 1),
	AWACS_SWITCH("Line Capture Switch", 0, SHIFT_MUX_MIC, 0),
	AWACS_SWITCH("CD Capture Switch", 0, SHIFT_MUX_LINE, 0),
};

static const struct snd_kcontrol_new snd_pmac_screamer_mixers_lo[] = {
	AWACS_VOLUME("Line out Playback Volume", 2, 6, 1),
};

static const struct snd_kcontrol_new snd_pmac_screamer_mixers_imac[] = {
	AWACS_VOLUME("Play-through Playback Volume", 5, 6, 1),
	AWACS_SWITCH("CD Capture Switch", 0, SHIFT_MUX_CD, 0),
};

static const struct snd_kcontrol_new snd_pmac_screamer_mixers_g4agp[] = {
	AWACS_VOLUME("Line out Playback Volume", 2, 6, 1),
	AWACS_VOLUME("Master Playback Volume", 5, 6, 1),
	AWACS_SWITCH("CD Capture Switch", 0, SHIFT_MUX_CD, 0),
	AWACS_SWITCH("Line Capture Switch", 0, SHIFT_MUX_MIC, 0),
};

static const struct snd_kcontrol_new snd_pmac_awacs_mixers_pmac7500[] = {
	AWACS_VOLUME("Line out Playback Volume", 2, 6, 1),
	AWACS_SWITCH("CD Capture Switch", 0, SHIFT_MUX_CD, 0),
	AWACS_SWITCH("Line Capture Switch", 0, SHIFT_MUX_MIC, 0),
};

static const struct snd_kcontrol_new snd_pmac_awacs_mixers_pmac5500[] = {
	AWACS_VOLUME("Headphone Playback Volume", 2, 6, 1),
};

static const struct snd_kcontrol_new snd_pmac_awacs_mixers_pmac[] = {
	AWACS_VOLUME("Master Playback Volume", 2, 6, 1),
	AWACS_SWITCH("CD Capture Switch", 0, SHIFT_MUX_CD, 0),
};

/* FIXME: is this correct order?
 * screamer (powerbook G3 pismo) seems to have different bits...
 */
static const struct snd_kcontrol_new snd_pmac_awacs_mixers2[] = {
	AWACS_SWITCH("Line Capture Switch", 0, SHIFT_MUX_LINE, 0),
	AWACS_SWITCH("Mic Capture Switch", 0, SHIFT_MUX_MIC, 0),
};

static const struct snd_kcontrol_new snd_pmac_screamer_mixers2[] = {
	AWACS_SWITCH("Line Capture Switch", 0, SHIFT_MUX_MIC, 0),
	AWACS_SWITCH("Mic Capture Switch", 0, SHIFT_MUX_LINE, 0),
};

static const struct snd_kcontrol_new snd_pmac_awacs_mixers2_pmac5500[] = {
	AWACS_SWITCH("CD Capture Switch", 0, SHIFT_MUX_CD, 0),
};

static const struct snd_kcontrol_new snd_pmac_awacs_master_sw =
AWACS_SWITCH("Master Playback Switch", 1, SHIFT_HDMUTE, 1);

static const struct snd_kcontrol_new snd_pmac_awacs_master_sw_imac =
AWACS_SWITCH("Line out Playback Switch", 1, SHIFT_HDMUTE, 1);

static const struct snd_kcontrol_new snd_pmac_awacs_master_sw_pmac5500 =
AWACS_SWITCH("Headphone Playback Switch", 1, SHIFT_HDMUTE, 1);

static const struct snd_kcontrol_new snd_pmac_awacs_mic_boost[] = {
	AWACS_SWITCH("Mic Boost Capture Switch", 0, SHIFT_GAINLINE, 0),
};

static const struct snd_kcontrol_new snd_pmac_screamer_mic_boost[] = {
	{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	  .name = "Mic Boost Capture Volume",
	  .info = snd_pmac_screamer_mic_boost_info,
	  .get = snd_pmac_screamer_mic_boost_get,
	  .put = snd_pmac_screamer_mic_boost_put,
	},
};

static const struct snd_kcontrol_new snd_pmac_awacs_mic_boost_pmac7500[] =
{
	AWACS_SWITCH("Line Boost Capture Switch", 0, SHIFT_GAINLINE, 0),
};

static const struct snd_kcontrol_new snd_pmac_screamer_mic_boost_beige[] =
{
	AWACS_SWITCH("Line Boost Capture Switch", 0, SHIFT_GAINLINE, 0),
	AWACS_SWITCH("CD Boost Capture Switch", 6, SHIFT_MIC_BOOST, 0),
};

static const struct snd_kcontrol_new snd_pmac_screamer_mic_boost_imac[] =
{
	AWACS_SWITCH("Line Boost Capture Switch", 0, SHIFT_GAINLINE, 0),
	AWACS_SWITCH("Mic Boost Capture Switch", 6, SHIFT_MIC_BOOST, 0),
};

static const struct snd_kcontrol_new snd_pmac_awacs_speaker_vol[] = {
	AWACS_VOLUME("Speaker Playback Volume", 4, 6, 1),
};

static const struct snd_kcontrol_new snd_pmac_awacs_speaker_sw =
AWACS_SWITCH("Speaker Playback Switch", 1, SHIFT_SPKMUTE, 1);

static const struct snd_kcontrol_new snd_pmac_awacs_speaker_sw_imac1 =
AWACS_SWITCH("Speaker Playback Switch", 1, SHIFT_PAROUT1, 1);

static const struct snd_kcontrol_new snd_pmac_awacs_speaker_sw_imac2 =
AWACS_SWITCH("Speaker Playback Switch", 1, SHIFT_PAROUT1, 0);


/*
 * add new mixer elements to the card
 */
static int build_mixers(struct snd_pmac *chip, int nums,
			const struct snd_kcontrol_new *mixers)
{
	int i, err;

	for (i = 0; i < nums; i++) {
		err = snd_ctl_add(chip->card, snd_ctl_new1(&mixers[i], chip));
		if (err < 0)
			return err;
	}
	return 0;
}


/*
 * restore all registers
 */
static void awacs_restore_all_regs(struct snd_pmac *chip)
{
	snd_pmac_awacs_write_noreg(chip, 0, chip->awacs_reg[0]);
	snd_pmac_awacs_write_noreg(chip, 1, chip->awacs_reg[1]);
	snd_pmac_awacs_write_noreg(chip, 2, chip->awacs_reg[2]);
	snd_pmac_awacs_write_noreg(chip, 4, chip->awacs_reg[4]);
	if (chip->model == PMAC_SCREAMER) {
		snd_pmac_awacs_write_noreg(chip, 5, chip->awacs_reg[5]);
		snd_pmac_awacs_write_noreg(chip, 6, chip->awacs_reg[6]);
		snd_pmac_awacs_write_noreg(chip, 7, chip->awacs_reg[7]);
	}
}

#ifdef CONFIG_PM
static void snd_pmac_awacs_suspend(struct snd_pmac *chip)
{
	snd_pmac_awacs_write_noreg(chip, 1, (chip->awacs_reg[1]
					     | MASK_AMUTE | MASK_CMUTE));
}

static void snd_pmac_awacs_resume(struct snd_pmac *chip)
{
	if (of_machine_is_compatible("PowerBook3,1")
	    || of_machine_is_compatible("PowerBook3,2")) {
		msleep(100);
		snd_pmac_awacs_write_reg(chip, 1,
			chip->awacs_reg[1] & ~MASK_PAROUT);
		msleep(300);
	}

	awacs_restore_all_regs(chip);
	if (chip->model == PMAC_SCREAMER) {
		/* reset power bits in reg 6 */
		mdelay(5);
		snd_pmac_awacs_write_noreg(chip, 6, chip->awacs_reg[6]);
	}
	screamer_recalibrate(chip);
#ifdef PMAC_AMP_AVAIL
	if (chip->mixer_data) {
		struct awacs_amp *amp = chip->mixer_data;
		awacs_amp_set_vol(amp, 0,
				  amp->amp_vol[0][0], amp->amp_vol[0][1], 0);
		awacs_amp_set_vol(amp, 1,
				  amp->amp_vol[1][0], amp->amp_vol[1][1], 0);
		awacs_amp_set_tone(amp, amp->amp_tone[0], amp->amp_tone[1]);
		awacs_amp_set_master(amp, amp->amp_master);
	}
#endif
}
#endif /* CONFIG_PM */

#define IS_PM7500 (of_machine_is_compatible("AAPL,7500") \
		|| of_machine_is_compatible("AAPL,8500") \
		|| of_machine_is_compatible("AAPL,9500"))
#define IS_PM5500 (of_machine_is_compatible("AAPL,e411"))
#define IS_BEIGE (of_machine_is_compatible("AAPL,Gossamer"))
#define IS_IMAC1 (of_machine_is_compatible("PowerMac2,1"))
#define IS_IMAC2 (of_machine_is_compatible("PowerMac2,2") \
		|| of_machine_is_compatible("PowerMac4,1"))
#define IS_G4AGP (of_machine_is_compatible("PowerMac3,1"))
#define IS_LOMBARD (of_machine_is_compatible("PowerBook1,1"))

static int imac1, imac2;

#ifdef PMAC_SUPPORT_AUTOMUTE
/*
 * auto-mute stuffs
 */
static int snd_pmac_awacs_detect_headphone(struct snd_pmac *chip)
{
	return (in_le32(&chip->awacs->codec_stat) & chip->hp_stat_mask) ? 1 : 0;
}

#ifdef PMAC_AMP_AVAIL
static int toggle_amp_mute(struct awacs_amp *amp, int index, int mute)
{
	int vol[2];
	vol[0] = amp->amp_vol[index][0] & 31;
	vol[1] = amp->amp_vol[index][1] & 31;
	if (mute) {
		vol[0] |= 32;
		vol[1] |= 32;
	}
	return awacs_amp_set_vol(amp, index, vol[0], vol[1], 1);
}
#endif

static void snd_pmac_awacs_update_automute(struct snd_pmac *chip, int do_notify)
{
	if (chip->auto_mute) {
#ifdef PMAC_AMP_AVAIL
		if (chip->mixer_data) {
			struct awacs_amp *amp = chip->mixer_data;
			int changed;
			if (snd_pmac_awacs_detect_headphone(chip)) {
				changed = toggle_amp_mute(amp, AMP_CH_HD, 0);
				changed |= toggle_amp_mute(amp, AMP_CH_SPK, 1);
			} else {
				changed = toggle_amp_mute(amp, AMP_CH_HD, 1);
				changed |= toggle_amp_mute(amp, AMP_CH_SPK, 0);
			}
			if (do_notify && ! changed)
				return;
		} else
#endif
		{
			int reg = chip->awacs_reg[1]
				| (MASK_HDMUTE | MASK_SPKMUTE);
			if (imac1) {
				reg &= ~MASK_SPKMUTE;
				reg |= MASK_PAROUT1;
			} else if (imac2) {
				reg &= ~MASK_SPKMUTE;
				reg &= ~MASK_PAROUT1;
			}
			if (snd_pmac_awacs_detect_headphone(chip))
				reg &= ~MASK_HDMUTE;
			else if (imac1)
				reg &= ~MASK_PAROUT1;
			else if (imac2)
				reg |= MASK_PAROUT1;
			else
				reg &= ~MASK_SPKMUTE;
			if (do_notify && reg == chip->awacs_reg[1])
				return;
			snd_pmac_awacs_write_reg(chip, 1, reg);
		}
		if (do_notify) {
			snd_ctl_notify(chip->card, SNDRV_CTL_EVENT_MASK_VALUE,
				       &chip->master_sw_ctl->id);
			snd_ctl_notify(chip->card, SNDRV_CTL_EVENT_MASK_VALUE,
				       &chip->speaker_sw_ctl->id);
			snd_ctl_notify(chip->card, SNDRV_CTL_EVENT_MASK_VALUE,
				       &chip->hp_detect_ctl->id);
		}
	}
}
#endif /* PMAC_SUPPORT_AUTOMUTE */


/*
 * initialize chip
 */
int
snd_pmac_awacs_init(struct snd_pmac *chip)
{
	int pm7500 = IS_PM7500;
	int pm5500 = IS_PM5500;
	int beige = IS_BEIGE;
	int g4agp = IS_G4AGP;
	int lombard = IS_LOMBARD;
	int imac;
	int err, vol;
	struct snd_kcontrol *vmaster_sw, *vmaster_vol;
	struct snd_kcontrol *master_vol, *speaker_vol;

	imac1 = IS_IMAC1;
	imac2 = IS_IMAC2;
	imac = imac1 || imac2;
	/* looks like MASK_GAINLINE triggers something, so we set here
	 * as start-up
	 */
	chip->awacs_reg[0] = MASK_MUX_CD | 0xff | MASK_GAINLINE;
	chip->awacs_reg[1] = MASK_CMUTE | MASK_AMUTE;
	/* FIXME: Only machines with external SRS module need MASK_PAROUT */
	if (chip->has_iic || chip->device_id == 0x5 ||
	    /* chip->_device_id == 0x8 || */
	    chip->device_id == 0xb)
		chip->awacs_reg[1] |= MASK_PAROUT;
	/* get default volume from nvram */
	// vol = (~nvram_read_byte(0x1308) & 7) << 1;
	// vol = ((pmac_xpram_read( 8 ) & 7 ) << 1 );
	vol = 0x0f; /* no, on alsa, muted as default */
	vol = vol + (vol << 6);
	chip->awacs_reg[2] = vol;
	chip->awacs_reg[4] = vol;
	if (chip->model == PMAC_SCREAMER) {
		/* FIXME: screamer has loopthru vol control */
		chip->awacs_reg[5] = vol;
		/* FIXME: maybe should be vol << 3 for PCMCIA speaker */
		chip->awacs_reg[6] = MASK_MIC_BOOST;
		chip->awacs_reg[7] = 0;
	}

	awacs_restore_all_regs(chip);
	chip->manufacturer = (in_le32(&chip->awacs->codec_stat) >> 8) & 0xf;
	screamer_recalibrate(chip);

	chip->revision = (in_le32(&chip->awacs->codec_stat) >> 12) & 0xf;
#ifdef PMAC_AMP_AVAIL
	if (chip->revision == 3 && chip->has_iic && CHECK_CUDA_AMP()) {
		struct awacs_amp *amp = kzalloc(sizeof(*amp), GFP_KERNEL);
		if (! amp)
			return -ENOMEM;
		chip->mixer_data = amp;
		chip->mixer_free = awacs_amp_free;
		/* mute and zero vol */
		awacs_amp_set_vol(amp, 0, 63, 63, 0);
		awacs_amp_set_vol(amp, 1, 63, 63, 0);
		awacs_amp_set_tone(amp, 7, 7); /* 0 dB */
		awacs_amp_set_master(amp, 79); /* 0 dB */
	}
#endif /* PMAC_AMP_AVAIL */

	if (chip->hp_stat_mask == 0) {
		/* set headphone-jack detection bit */
		switch (chip->model) {
		case PMAC_AWACS:
			chip->hp_stat_mask = pm7500 || pm5500 ? MASK_HDPCONN
				: MASK_LOCONN;
			break;
		case PMAC_SCREAMER:
			switch (chip->device_id) {
			case 0x08:
			case 0x0B:
				chip->hp_stat_mask = imac
					? MASK_LOCONN_IMAC |
					MASK_HDPLCONN_IMAC |
					MASK_HDPRCONN_IMAC
					: MASK_HDPCONN;
				break;
			case 0x00:
			case 0x05:
				chip->hp_stat_mask = MASK_LOCONN;
				break;
			default:
				chip->hp_stat_mask = MASK_HDPCONN;
				break;
			}
			break;
		default:
			snd_BUG();
			break;
		}
	}

	/*
	 * build mixers
	 */
	strcpy(chip->card->mixername, "PowerMac AWACS");

	err = build_mixers(chip, ARRAY_SIZE(snd_pmac_awacs_mixers),
				snd_pmac_awacs_mixers);
	if (err < 0)
		return err;
	if (beige || g4agp)
		;
	else if (chip->model == PMAC_SCREAMER || pm5500)
		err = build_mixers(chip, ARRAY_SIZE(snd_pmac_screamer_mixers2),
				   snd_pmac_screamer_mixers2);
	else if (!pm7500)
		err = build_mixers(chip, ARRAY_SIZE(snd_pmac_awacs_mixers2),
				   snd_pmac_awacs_mixers2);
	if (err < 0)
		return err;
	if (pm5500) {
		err = build_mixers(chip,
				   ARRAY_SIZE(snd_pmac_awacs_mixers2_pmac5500),
				   snd_pmac_awacs_mixers2_pmac5500);
		if (err < 0)
			return err;
	}
	master_vol = NULL;
	if (pm7500)
		err = build_mixers(chip,
				   ARRAY_SIZE(snd_pmac_awacs_mixers_pmac7500),
				   snd_pmac_awacs_mixers_pmac7500);
	else if (pm5500)
		err = snd_ctl_add(chip->card,
		    (master_vol = snd_ctl_new1(snd_pmac_awacs_mixers_pmac5500,
						chip)));
	else if (beige)
		err = build_mixers(chip,
				   ARRAY_SIZE(snd_pmac_screamer_mixers_beige),
				   snd_pmac_screamer_mixers_beige);
	else if (imac || lombard) {
		err = snd_ctl_add(chip->card,
		    (master_vol = snd_ctl_new1(snd_pmac_screamer_mixers_lo,
						chip)));
		if (err < 0)
			return err;
		err = build_mixers(chip,
				   ARRAY_SIZE(snd_pmac_screamer_mixers_imac),
				   snd_pmac_screamer_mixers_imac);
	} else if (g4agp)
		err = build_mixers(chip,
				   ARRAY_SIZE(snd_pmac_screamer_mixers_g4agp),
				   snd_pmac_screamer_mixers_g4agp);
	else
		err = build_mixers(chip,
				   ARRAY_SIZE(snd_pmac_awacs_mixers_pmac),
				   snd_pmac_awacs_mixers_pmac);
	if (err < 0)
		return err;
	chip->master_sw_ctl = snd_ctl_new1((pm7500 || imac || g4agp || lombard)
			? &snd_pmac_awacs_master_sw_imac
			: pm5500
			? &snd_pmac_awacs_master_sw_pmac5500
			: &snd_pmac_awacs_master_sw, chip);
	err = snd_ctl_add(chip->card, chip->master_sw_ctl);
	if (err < 0)
		return err;
#ifdef PMAC_AMP_AVAIL
	if (chip->mixer_data) {
		/* use amplifier.  the signal is connected from route A
		 * to the amp.  the amp has its headphone and speaker
		 * volumes and mute switches, so we use them instead of
		 * screamer registers.
		 * in this case, it seems the route C is not used.
		 */
		err = build_mixers(chip, ARRAY_SIZE(snd_pmac_awacs_amp_vol),
					snd_pmac_awacs_amp_vol);
		if (err < 0)
			return err;
		/* overwrite */
		chip->master_sw_ctl = snd_ctl_new1(&snd_pmac_awacs_amp_hp_sw,
							chip);
		err = snd_ctl_add(chip->card, chip->master_sw_ctl);
		if (err < 0)
			return err;
		chip->speaker_sw_ctl = snd_ctl_new1(&snd_pmac_awacs_amp_spk_sw,
							chip);
		err = snd_ctl_add(chip->card, chip->speaker_sw_ctl);
		if (err < 0)
			return err;
	} else
#endif /* PMAC_AMP_AVAIL */
	{
		/* route A = headphone, route C = speaker */
		err = snd_ctl_add(chip->card,
		    (speaker_vol = snd_ctl_new1(snd_pmac_awacs_speaker_vol,
						chip)));
		if (err < 0)
			return err;
		chip->speaker_sw_ctl = snd_ctl_new1(imac1
				? &snd_pmac_awacs_speaker_sw_imac1
				: imac2
				? &snd_pmac_awacs_speaker_sw_imac2
				: &snd_pmac_awacs_speaker_sw, chip);
		err = snd_ctl_add(chip->card, chip->speaker_sw_ctl);
		if (err < 0)
			return err;
	}

	if (pm5500 || imac || lombard) {
		vmaster_sw = snd_ctl_make_virtual_master(
			"Master Playback Switch", (unsigned int *) NULL);
		err = snd_ctl_add_follower_uncached(vmaster_sw,
						    chip->master_sw_ctl);
		if (err < 0)
			return err;
		err = snd_ctl_add_follower_uncached(vmaster_sw,
						    chip->speaker_sw_ctl);
		if (err < 0)
			return err;
		err = snd_ctl_add(chip->card, vmaster_sw);
		if (err < 0)
			return err;
		vmaster_vol = snd_ctl_make_virtual_master(
			"Master Playback Volume", (unsigned int *) NULL);
		err = snd_ctl_add_follower(vmaster_vol, master_vol);
		if (err < 0)
			return err;
		err = snd_ctl_add_follower(vmaster_vol, speaker_vol);
		if (err < 0)
			return err;
		err = snd_ctl_add(chip->card, vmaster_vol);
		if (err < 0)
			return err;
	}

	if (beige || g4agp)
		err = build_mixers(chip,
				ARRAY_SIZE(snd_pmac_screamer_mic_boost_beige),
				snd_pmac_screamer_mic_boost_beige);
	else if (imac)
		err = build_mixers(chip,
				ARRAY_SIZE(snd_pmac_screamer_mic_boost_imac),
				snd_pmac_screamer_mic_boost_imac);
	else if (chip->model == PMAC_SCREAMER)
		err = build_mixers(chip,
				ARRAY_SIZE(snd_pmac_screamer_mic_boost),
				snd_pmac_screamer_mic_boost);
	else if (pm7500)
		err = build_mixers(chip,
				ARRAY_SIZE(snd_pmac_awacs_mic_boost_pmac7500),
				snd_pmac_awacs_mic_boost_pmac7500);
	else
		err = build_mixers(chip, ARRAY_SIZE(snd_pmac_awacs_mic_boost),
				snd_pmac_awacs_mic_boost);
	if (err < 0)
		return err;

	/*
	 * set lowlevel callbacks
	 */
	chip->set_format = snd_pmac_awacs_set_format;
#ifdef CONFIG_PM
	chip->suspend = snd_pmac_awacs_suspend;
	chip->resume = snd_pmac_awacs_resume;
#endif
#ifdef PMAC_SUPPORT_AUTOMUTE
	err = snd_pmac_add_automute(chip);
	if (err < 0)
		return err;
	chip->detect_headphone = snd_pmac_awacs_detect_headphone;
	chip->update_automute = snd_pmac_awacs_update_automute;
	snd_pmac_awacs_update_automute(chip, 0); /* update the status only */
#endif
	if (chip->model == PMAC_SCREAMER) {
		snd_pmac_awacs_write_noreg(chip, 6, chip->awacs_reg[6]);
		snd_pmac_awacs_write_noreg(chip, 0, chip->awacs_reg[0]);
	}

	return 0;
}
