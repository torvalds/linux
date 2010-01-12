/*
 *  Copyright (c) by Jaroslav Kysela <perex@perex.cz>
 *  Routines for Sound Blaster mixer control
 *
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
 *
 */

#include <asm/io.h>
#include <linux/delay.h>
#include <linux/time.h>
#include <sound/core.h>
#include <sound/sb.h>
#include <sound/control.h>

#undef IO_DEBUG

void snd_sbmixer_write(struct snd_sb *chip, unsigned char reg, unsigned char data)
{
	outb(reg, SBP(chip, MIXER_ADDR));
	udelay(10);
	outb(data, SBP(chip, MIXER_DATA));
	udelay(10);
#ifdef IO_DEBUG
	snd_printk(KERN_DEBUG "mixer_write 0x%x 0x%x\n", reg, data);
#endif
}

unsigned char snd_sbmixer_read(struct snd_sb *chip, unsigned char reg)
{
	unsigned char result;

	outb(reg, SBP(chip, MIXER_ADDR));
	udelay(10);
	result = inb(SBP(chip, MIXER_DATA));
	udelay(10);
#ifdef IO_DEBUG
	snd_printk(KERN_DEBUG "mixer_read 0x%x 0x%x\n", reg, result);
#endif
	return result;
}

/*
 * Single channel mixer element
 */

static int snd_sbmixer_info_single(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	int mask = (kcontrol->private_value >> 24) & 0xff;

	uinfo->type = mask == 1 ? SNDRV_CTL_ELEM_TYPE_BOOLEAN : SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = mask;
	return 0;
}

static int snd_sbmixer_get_single(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_sb *sb = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	int reg = kcontrol->private_value & 0xff;
	int shift = (kcontrol->private_value >> 16) & 0xff;
	int mask = (kcontrol->private_value >> 24) & 0xff;
	unsigned char val;

	spin_lock_irqsave(&sb->mixer_lock, flags);
	val = (snd_sbmixer_read(sb, reg) >> shift) & mask;
	spin_unlock_irqrestore(&sb->mixer_lock, flags);
	ucontrol->value.integer.value[0] = val;
	return 0;
}

static int snd_sbmixer_put_single(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_sb *sb = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	int reg = kcontrol->private_value & 0xff;
	int shift = (kcontrol->private_value >> 16) & 0x07;
	int mask = (kcontrol->private_value >> 24) & 0xff;
	int change;
	unsigned char val, oval;

	val = (ucontrol->value.integer.value[0] & mask) << shift;
	spin_lock_irqsave(&sb->mixer_lock, flags);
	oval = snd_sbmixer_read(sb, reg);
	val = (oval & ~(mask << shift)) | val;
	change = val != oval;
	if (change)
		snd_sbmixer_write(sb, reg, val);
	spin_unlock_irqrestore(&sb->mixer_lock, flags);
	return change;
}

/*
 * Double channel mixer element
 */

static int snd_sbmixer_info_double(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	int mask = (kcontrol->private_value >> 24) & 0xff;

	uinfo->type = mask == 1 ? SNDRV_CTL_ELEM_TYPE_BOOLEAN : SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = mask;
	return 0;
}

static int snd_sbmixer_get_double(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_sb *sb = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	int left_reg = kcontrol->private_value & 0xff;
	int right_reg = (kcontrol->private_value >> 8) & 0xff;
	int left_shift = (kcontrol->private_value >> 16) & 0x07;
	int right_shift = (kcontrol->private_value >> 19) & 0x07;
	int mask = (kcontrol->private_value >> 24) & 0xff;
	unsigned char left, right;

	spin_lock_irqsave(&sb->mixer_lock, flags);
	left = (snd_sbmixer_read(sb, left_reg) >> left_shift) & mask;
	right = (snd_sbmixer_read(sb, right_reg) >> right_shift) & mask;
	spin_unlock_irqrestore(&sb->mixer_lock, flags);
	ucontrol->value.integer.value[0] = left;
	ucontrol->value.integer.value[1] = right;
	return 0;
}

static int snd_sbmixer_put_double(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_sb *sb = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	int left_reg = kcontrol->private_value & 0xff;
	int right_reg = (kcontrol->private_value >> 8) & 0xff;
	int left_shift = (kcontrol->private_value >> 16) & 0x07;
	int right_shift = (kcontrol->private_value >> 19) & 0x07;
	int mask = (kcontrol->private_value >> 24) & 0xff;
	int change;
	unsigned char left, right, oleft, oright;

	left = (ucontrol->value.integer.value[0] & mask) << left_shift;
	right = (ucontrol->value.integer.value[1] & mask) << right_shift;
	spin_lock_irqsave(&sb->mixer_lock, flags);
	if (left_reg == right_reg) {
		oleft = snd_sbmixer_read(sb, left_reg);
		left = (oleft & ~((mask << left_shift) | (mask << right_shift))) | left | right;
		change = left != oleft;
		if (change)
			snd_sbmixer_write(sb, left_reg, left);
	} else {
		oleft = snd_sbmixer_read(sb, left_reg);
		oright = snd_sbmixer_read(sb, right_reg);
		left = (oleft & ~(mask << left_shift)) | left;
		right = (oright & ~(mask << right_shift)) | right;
		change = left != oleft || right != oright;
		if (change) {
			snd_sbmixer_write(sb, left_reg, left);
			snd_sbmixer_write(sb, right_reg, right);
		}
	}
	spin_unlock_irqrestore(&sb->mixer_lock, flags);
	return change;
}

/*
 * DT-019x / ALS-007 capture/input switch
 */

static int snd_dt019x_input_sw_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	static const char *texts[5] = {
		"CD", "Mic", "Line", "Synth", "Master"
	};

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 5;
	if (uinfo->value.enumerated.item > 4)
		uinfo->value.enumerated.item = 4;
	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}

static int snd_dt019x_input_sw_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_sb *sb = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	unsigned char oval;
	
	spin_lock_irqsave(&sb->mixer_lock, flags);
	oval = snd_sbmixer_read(sb, SB_DT019X_CAPTURE_SW);
	spin_unlock_irqrestore(&sb->mixer_lock, flags);
	switch (oval & 0x07) {
	case SB_DT019X_CAP_CD:
		ucontrol->value.enumerated.item[0] = 0;
		break;
	case SB_DT019X_CAP_MIC:
		ucontrol->value.enumerated.item[0] = 1;
		break;
	case SB_DT019X_CAP_LINE:
		ucontrol->value.enumerated.item[0] = 2;
		break;
	case SB_DT019X_CAP_MAIN:
		ucontrol->value.enumerated.item[0] = 4;
		break;
	/* To record the synth on these cards you must record the main.   */
	/* Thus SB_DT019X_CAP_SYNTH == SB_DT019X_CAP_MAIN and would cause */
	/* duplicate case labels if left uncommented. */
	/* case SB_DT019X_CAP_SYNTH:
	 *	ucontrol->value.enumerated.item[0] = 3;
	 *	break;
	 */
	default:
		ucontrol->value.enumerated.item[0] = 4;
		break;
	}
	return 0;
}

static int snd_dt019x_input_sw_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_sb *sb = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	int change;
	unsigned char nval, oval;
	
	if (ucontrol->value.enumerated.item[0] > 4)
		return -EINVAL;
	switch (ucontrol->value.enumerated.item[0]) {
	case 0:
		nval = SB_DT019X_CAP_CD;
		break;
	case 1:
		nval = SB_DT019X_CAP_MIC;
		break;
	case 2:
		nval = SB_DT019X_CAP_LINE;
		break;
	case 3:
		nval = SB_DT019X_CAP_SYNTH;
		break;
	case 4:
		nval = SB_DT019X_CAP_MAIN;
		break;
	default:
		nval = SB_DT019X_CAP_MAIN;
	}
	spin_lock_irqsave(&sb->mixer_lock, flags);
	oval = snd_sbmixer_read(sb, SB_DT019X_CAPTURE_SW);
	change = nval != oval;
	if (change)
		snd_sbmixer_write(sb, SB_DT019X_CAPTURE_SW, nval);
	spin_unlock_irqrestore(&sb->mixer_lock, flags);
	return change;
}

/*
 * ALS4000 mono recording control switch
 */

static int snd_als4k_mono_capture_route_info(struct snd_kcontrol *kcontrol,
					     struct snd_ctl_elem_info *uinfo)
{
	static const char *texts[3] = {
		"L chan only", "R chan only", "L ch/2 + R ch/2"
	};

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 3;
	if (uinfo->value.enumerated.item > 2)
		uinfo->value.enumerated.item = 2;
	strcpy(uinfo->value.enumerated.name,
	       texts[uinfo->value.enumerated.item]);
	return 0;
}

static int snd_als4k_mono_capture_route_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_sb *sb = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	unsigned char oval;

	spin_lock_irqsave(&sb->mixer_lock, flags);
	oval = snd_sbmixer_read(sb, SB_ALS4000_MONO_IO_CTRL);
	spin_unlock_irqrestore(&sb->mixer_lock, flags);
	oval >>= 6;
	if (oval > 2)
		oval = 2;

	ucontrol->value.enumerated.item[0] = oval;
	return 0;
}

static int snd_als4k_mono_capture_route_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_sb *sb = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	int change;
	unsigned char nval, oval;

	if (ucontrol->value.enumerated.item[0] > 2)
		return -EINVAL;
	spin_lock_irqsave(&sb->mixer_lock, flags);
	oval = snd_sbmixer_read(sb, SB_ALS4000_MONO_IO_CTRL);

	nval = (oval & ~(3 << 6))
	     | (ucontrol->value.enumerated.item[0] << 6);
	change = nval != oval;
	if (change)
		snd_sbmixer_write(sb, SB_ALS4000_MONO_IO_CTRL, nval);
	spin_unlock_irqrestore(&sb->mixer_lock, flags);
	return change;
}

/*
 * SBPRO input multiplexer
 */

static int snd_sb8mixer_info_mux(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	static const char *texts[3] = {
		"Mic", "CD", "Line"
	};

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 3;
	if (uinfo->value.enumerated.item > 2)
		uinfo->value.enumerated.item = 2;
	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}


static int snd_sb8mixer_get_mux(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_sb *sb = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	unsigned char oval;
	
	spin_lock_irqsave(&sb->mixer_lock, flags);
	oval = snd_sbmixer_read(sb, SB_DSP_CAPTURE_SOURCE);
	spin_unlock_irqrestore(&sb->mixer_lock, flags);
	switch ((oval >> 0x01) & 0x03) {
	case SB_DSP_MIXS_CD:
		ucontrol->value.enumerated.item[0] = 1;
		break;
	case SB_DSP_MIXS_LINE:
		ucontrol->value.enumerated.item[0] = 2;
		break;
	default:
		ucontrol->value.enumerated.item[0] = 0;
		break;
	}
	return 0;
}

static int snd_sb8mixer_put_mux(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_sb *sb = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	int change;
	unsigned char nval, oval;
	
	if (ucontrol->value.enumerated.item[0] > 2)
		return -EINVAL;
	switch (ucontrol->value.enumerated.item[0]) {
	case 1:
		nval = SB_DSP_MIXS_CD;
		break;
	case 2:
		nval = SB_DSP_MIXS_LINE;
		break;
	default:
		nval = SB_DSP_MIXS_MIC;
	}
	nval <<= 1;
	spin_lock_irqsave(&sb->mixer_lock, flags);
	oval = snd_sbmixer_read(sb, SB_DSP_CAPTURE_SOURCE);
	nval |= oval & ~0x06;
	change = nval != oval;
	if (change)
		snd_sbmixer_write(sb, SB_DSP_CAPTURE_SOURCE, nval);
	spin_unlock_irqrestore(&sb->mixer_lock, flags);
	return change;
}

/*
 * SB16 input switch
 */

static int snd_sb16mixer_info_input_sw(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = 4;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;
	return 0;
}

static int snd_sb16mixer_get_input_sw(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_sb *sb = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	int reg1 = kcontrol->private_value & 0xff;
	int reg2 = (kcontrol->private_value >> 8) & 0xff;
	int left_shift = (kcontrol->private_value >> 16) & 0x0f;
	int right_shift = (kcontrol->private_value >> 24) & 0x0f;
	unsigned char val1, val2;

	spin_lock_irqsave(&sb->mixer_lock, flags);
	val1 = snd_sbmixer_read(sb, reg1);
	val2 = snd_sbmixer_read(sb, reg2);
	spin_unlock_irqrestore(&sb->mixer_lock, flags);
	ucontrol->value.integer.value[0] = (val1 >> left_shift) & 0x01;
	ucontrol->value.integer.value[1] = (val2 >> left_shift) & 0x01;
	ucontrol->value.integer.value[2] = (val1 >> right_shift) & 0x01;
	ucontrol->value.integer.value[3] = (val2 >> right_shift) & 0x01;
	return 0;
}                                                                                                                   

static int snd_sb16mixer_put_input_sw(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_sb *sb = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	int reg1 = kcontrol->private_value & 0xff;
	int reg2 = (kcontrol->private_value >> 8) & 0xff;
	int left_shift = (kcontrol->private_value >> 16) & 0x0f;
	int right_shift = (kcontrol->private_value >> 24) & 0x0f;
	int change;
	unsigned char val1, val2, oval1, oval2;

	spin_lock_irqsave(&sb->mixer_lock, flags);
	oval1 = snd_sbmixer_read(sb, reg1);
	oval2 = snd_sbmixer_read(sb, reg2);
	val1 = oval1 & ~((1 << left_shift) | (1 << right_shift));
	val2 = oval2 & ~((1 << left_shift) | (1 << right_shift));
	val1 |= (ucontrol->value.integer.value[0] & 1) << left_shift;
	val2 |= (ucontrol->value.integer.value[1] & 1) << left_shift;
	val1 |= (ucontrol->value.integer.value[2] & 1) << right_shift;
	val2 |= (ucontrol->value.integer.value[3] & 1) << right_shift;
	change = val1 != oval1 || val2 != oval2;
	if (change) {
		snd_sbmixer_write(sb, reg1, val1);
		snd_sbmixer_write(sb, reg2, val2);
	}
	spin_unlock_irqrestore(&sb->mixer_lock, flags);
	return change;
}


/*
 */
/*
 */
int snd_sbmixer_add_ctl(struct snd_sb *chip, const char *name, int index, int type, unsigned long value)
{
	static struct snd_kcontrol_new newctls[] = {
		[SB_MIX_SINGLE] = {
			.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
			.info = snd_sbmixer_info_single,
			.get = snd_sbmixer_get_single,
			.put = snd_sbmixer_put_single,
		},
		[SB_MIX_DOUBLE] = {
			.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
			.info = snd_sbmixer_info_double,
			.get = snd_sbmixer_get_double,
			.put = snd_sbmixer_put_double,
		},
		[SB_MIX_INPUT_SW] = {
			.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
			.info = snd_sb16mixer_info_input_sw,
			.get = snd_sb16mixer_get_input_sw,
			.put = snd_sb16mixer_put_input_sw,
		},
		[SB_MIX_CAPTURE_PRO] = {
			.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
			.info = snd_sb8mixer_info_mux,
			.get = snd_sb8mixer_get_mux,
			.put = snd_sb8mixer_put_mux,
		},
		[SB_MIX_CAPTURE_DT019X] = {
			.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
			.info = snd_dt019x_input_sw_info,
			.get = snd_dt019x_input_sw_get,
			.put = snd_dt019x_input_sw_put,
		},
		[SB_MIX_MONO_CAPTURE_ALS4K] = {
			.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
			.info = snd_als4k_mono_capture_route_info,
			.get = snd_als4k_mono_capture_route_get,
			.put = snd_als4k_mono_capture_route_put,
		},
	};
	struct snd_kcontrol *ctl;
	int err;

	ctl = snd_ctl_new1(&newctls[type], chip);
	if (! ctl)
		return -ENOMEM;
	strlcpy(ctl->id.name, name, sizeof(ctl->id.name));
	ctl->id.index = index;
	ctl->private_value = value;
	if ((err = snd_ctl_add(chip->card, ctl)) < 0)
		return err;
	return 0;
}

/*
 * SB 2.0 specific mixer elements
 */

static struct sbmix_elem snd_sb20_ctl_master_play_vol =
	SB_SINGLE("Master Playback Volume", SB_DSP20_MASTER_DEV, 1, 7);
static struct sbmix_elem snd_sb20_ctl_pcm_play_vol =
	SB_SINGLE("PCM Playback Volume", SB_DSP20_PCM_DEV, 1, 3);
static struct sbmix_elem snd_sb20_ctl_synth_play_vol =
	SB_SINGLE("Synth Playback Volume", SB_DSP20_FM_DEV, 1, 7);
static struct sbmix_elem snd_sb20_ctl_cd_play_vol =
	SB_SINGLE("CD Playback Volume", SB_DSP20_CD_DEV, 1, 7);

static struct sbmix_elem *snd_sb20_controls[] = {
	&snd_sb20_ctl_master_play_vol,
	&snd_sb20_ctl_pcm_play_vol,
	&snd_sb20_ctl_synth_play_vol,
	&snd_sb20_ctl_cd_play_vol
};

static unsigned char snd_sb20_init_values[][2] = {
	{ SB_DSP20_MASTER_DEV, 0 },
	{ SB_DSP20_FM_DEV, 0 },
};

/*
 * SB Pro specific mixer elements
 */
static struct sbmix_elem snd_sbpro_ctl_master_play_vol =
	SB_DOUBLE("Master Playback Volume", SB_DSP_MASTER_DEV, SB_DSP_MASTER_DEV, 5, 1, 7);
static struct sbmix_elem snd_sbpro_ctl_pcm_play_vol =
	SB_DOUBLE("PCM Playback Volume", SB_DSP_PCM_DEV, SB_DSP_PCM_DEV, 5, 1, 7);
static struct sbmix_elem snd_sbpro_ctl_pcm_play_filter =
	SB_SINGLE("PCM Playback Filter", SB_DSP_PLAYBACK_FILT, 5, 1);
static struct sbmix_elem snd_sbpro_ctl_synth_play_vol =
	SB_DOUBLE("Synth Playback Volume", SB_DSP_FM_DEV, SB_DSP_FM_DEV, 5, 1, 7);
static struct sbmix_elem snd_sbpro_ctl_cd_play_vol =
	SB_DOUBLE("CD Playback Volume", SB_DSP_CD_DEV, SB_DSP_CD_DEV, 5, 1, 7);
static struct sbmix_elem snd_sbpro_ctl_line_play_vol =
	SB_DOUBLE("Line Playback Volume", SB_DSP_LINE_DEV, SB_DSP_LINE_DEV, 5, 1, 7);
static struct sbmix_elem snd_sbpro_ctl_mic_play_vol =
	SB_SINGLE("Mic Playback Volume", SB_DSP_MIC_DEV, 1, 3);
static struct sbmix_elem snd_sbpro_ctl_capture_source =
	{
		.name = "Capture Source",
		.type = SB_MIX_CAPTURE_PRO
	};
static struct sbmix_elem snd_sbpro_ctl_capture_filter =
	SB_SINGLE("Capture Filter", SB_DSP_CAPTURE_FILT, 5, 1);
static struct sbmix_elem snd_sbpro_ctl_capture_low_filter =
	SB_SINGLE("Capture Low-Pass Filter", SB_DSP_CAPTURE_FILT, 3, 1);

static struct sbmix_elem *snd_sbpro_controls[] = {
	&snd_sbpro_ctl_master_play_vol,
	&snd_sbpro_ctl_pcm_play_vol,
	&snd_sbpro_ctl_pcm_play_filter,
	&snd_sbpro_ctl_synth_play_vol,
	&snd_sbpro_ctl_cd_play_vol,
	&snd_sbpro_ctl_line_play_vol,
	&snd_sbpro_ctl_mic_play_vol,
	&snd_sbpro_ctl_capture_source,
	&snd_sbpro_ctl_capture_filter,
	&snd_sbpro_ctl_capture_low_filter
};

static unsigned char snd_sbpro_init_values[][2] = {
	{ SB_DSP_MASTER_DEV, 0 },
	{ SB_DSP_PCM_DEV, 0 },
	{ SB_DSP_FM_DEV, 0 },
};

/*
 * SB16 specific mixer elements
 */
static struct sbmix_elem snd_sb16_ctl_master_play_vol =
	SB_DOUBLE("Master Playback Volume", SB_DSP4_MASTER_DEV, (SB_DSP4_MASTER_DEV + 1), 3, 3, 31);
static struct sbmix_elem snd_sb16_ctl_3d_enhance_switch =
	SB_SINGLE("3D Enhancement Switch", SB_DSP4_3DSE, 0, 1);
static struct sbmix_elem snd_sb16_ctl_tone_bass =
	SB_DOUBLE("Tone Control - Bass", SB_DSP4_BASS_DEV, (SB_DSP4_BASS_DEV + 1), 4, 4, 15);
static struct sbmix_elem snd_sb16_ctl_tone_treble =
	SB_DOUBLE("Tone Control - Treble", SB_DSP4_TREBLE_DEV, (SB_DSP4_TREBLE_DEV + 1), 4, 4, 15);
static struct sbmix_elem snd_sb16_ctl_pcm_play_vol =
	SB_DOUBLE("PCM Playback Volume", SB_DSP4_PCM_DEV, (SB_DSP4_PCM_DEV + 1), 3, 3, 31);
static struct sbmix_elem snd_sb16_ctl_synth_capture_route =
	SB16_INPUT_SW("Synth Capture Route", SB_DSP4_INPUT_LEFT, SB_DSP4_INPUT_RIGHT, 6, 5);
static struct sbmix_elem snd_sb16_ctl_synth_play_vol =
	SB_DOUBLE("Synth Playback Volume", SB_DSP4_SYNTH_DEV, (SB_DSP4_SYNTH_DEV + 1), 3, 3, 31);
static struct sbmix_elem snd_sb16_ctl_cd_capture_route =
	SB16_INPUT_SW("CD Capture Route", SB_DSP4_INPUT_LEFT, SB_DSP4_INPUT_RIGHT, 2, 1);
static struct sbmix_elem snd_sb16_ctl_cd_play_switch =
	SB_DOUBLE("CD Playback Switch", SB_DSP4_OUTPUT_SW, SB_DSP4_OUTPUT_SW, 2, 1, 1);
static struct sbmix_elem snd_sb16_ctl_cd_play_vol =
	SB_DOUBLE("CD Playback Volume", SB_DSP4_CD_DEV, (SB_DSP4_CD_DEV + 1), 3, 3, 31);
static struct sbmix_elem snd_sb16_ctl_line_capture_route =
	SB16_INPUT_SW("Line Capture Route", SB_DSP4_INPUT_LEFT, SB_DSP4_INPUT_RIGHT, 4, 3);
static struct sbmix_elem snd_sb16_ctl_line_play_switch =
	SB_DOUBLE("Line Playback Switch", SB_DSP4_OUTPUT_SW, SB_DSP4_OUTPUT_SW, 4, 3, 1);
static struct sbmix_elem snd_sb16_ctl_line_play_vol =
	SB_DOUBLE("Line Playback Volume", SB_DSP4_LINE_DEV, (SB_DSP4_LINE_DEV + 1), 3, 3, 31);
static struct sbmix_elem snd_sb16_ctl_mic_capture_route =
	SB16_INPUT_SW("Mic Capture Route", SB_DSP4_INPUT_LEFT, SB_DSP4_INPUT_RIGHT, 0, 0);
static struct sbmix_elem snd_sb16_ctl_mic_play_switch =
	SB_SINGLE("Mic Playback Switch", SB_DSP4_OUTPUT_SW, 0, 1);
static struct sbmix_elem snd_sb16_ctl_mic_play_vol =
	SB_SINGLE("Mic Playback Volume", SB_DSP4_MIC_DEV, 3, 31);
static struct sbmix_elem snd_sb16_ctl_pc_speaker_vol =
	SB_SINGLE("Beep Volume", SB_DSP4_SPEAKER_DEV, 6, 3);
static struct sbmix_elem snd_sb16_ctl_capture_vol =
	SB_DOUBLE("Capture Volume", SB_DSP4_IGAIN_DEV, (SB_DSP4_IGAIN_DEV + 1), 6, 6, 3);
static struct sbmix_elem snd_sb16_ctl_play_vol =
	SB_DOUBLE("Playback Volume", SB_DSP4_OGAIN_DEV, (SB_DSP4_OGAIN_DEV + 1), 6, 6, 3);
static struct sbmix_elem snd_sb16_ctl_auto_mic_gain =
	SB_SINGLE("Mic Auto Gain", SB_DSP4_MIC_AGC, 0, 1);

static struct sbmix_elem *snd_sb16_controls[] = {
	&snd_sb16_ctl_master_play_vol,
	&snd_sb16_ctl_3d_enhance_switch,
	&snd_sb16_ctl_tone_bass,
	&snd_sb16_ctl_tone_treble,
	&snd_sb16_ctl_pcm_play_vol,
	&snd_sb16_ctl_synth_capture_route,
	&snd_sb16_ctl_synth_play_vol,
	&snd_sb16_ctl_cd_capture_route,
	&snd_sb16_ctl_cd_play_switch,
	&snd_sb16_ctl_cd_play_vol,
	&snd_sb16_ctl_line_capture_route,
	&snd_sb16_ctl_line_play_switch,
	&snd_sb16_ctl_line_play_vol,
	&snd_sb16_ctl_mic_capture_route,
	&snd_sb16_ctl_mic_play_switch,
	&snd_sb16_ctl_mic_play_vol,
	&snd_sb16_ctl_pc_speaker_vol,
	&snd_sb16_ctl_capture_vol,
	&snd_sb16_ctl_play_vol,
	&snd_sb16_ctl_auto_mic_gain
};

static unsigned char snd_sb16_init_values[][2] = {
	{ SB_DSP4_MASTER_DEV + 0, 0 },
	{ SB_DSP4_MASTER_DEV + 1, 0 },
	{ SB_DSP4_PCM_DEV + 0, 0 },
	{ SB_DSP4_PCM_DEV + 1, 0 },
	{ SB_DSP4_SYNTH_DEV + 0, 0 },
	{ SB_DSP4_SYNTH_DEV + 1, 0 },
	{ SB_DSP4_INPUT_LEFT, 0 },
	{ SB_DSP4_INPUT_RIGHT, 0 },
	{ SB_DSP4_OUTPUT_SW, 0 },
	{ SB_DSP4_SPEAKER_DEV, 0 },
};

/*
 * DT019x specific mixer elements
 */
static struct sbmix_elem snd_dt019x_ctl_master_play_vol =
	SB_DOUBLE("Master Playback Volume", SB_DT019X_MASTER_DEV, SB_DT019X_MASTER_DEV, 4,0, 15);
static struct sbmix_elem snd_dt019x_ctl_pcm_play_vol =
	SB_DOUBLE("PCM Playback Volume", SB_DT019X_PCM_DEV, SB_DT019X_PCM_DEV, 4,0, 15);
static struct sbmix_elem snd_dt019x_ctl_synth_play_vol =
	SB_DOUBLE("Synth Playback Volume", SB_DT019X_SYNTH_DEV, SB_DT019X_SYNTH_DEV, 4,0, 15);
static struct sbmix_elem snd_dt019x_ctl_cd_play_vol =
	SB_DOUBLE("CD Playback Volume", SB_DT019X_CD_DEV, SB_DT019X_CD_DEV, 4,0, 15);
static struct sbmix_elem snd_dt019x_ctl_mic_play_vol =
	SB_SINGLE("Mic Playback Volume", SB_DT019X_MIC_DEV, 4, 7);
static struct sbmix_elem snd_dt019x_ctl_pc_speaker_vol =
	SB_SINGLE("Beep Volume", SB_DT019X_SPKR_DEV, 0,  7);
static struct sbmix_elem snd_dt019x_ctl_line_play_vol =
	SB_DOUBLE("Line Playback Volume", SB_DT019X_LINE_DEV, SB_DT019X_LINE_DEV, 4,0, 15);
static struct sbmix_elem snd_dt019x_ctl_pcm_play_switch =
	SB_DOUBLE("PCM Playback Switch", SB_DT019X_OUTPUT_SW2, SB_DT019X_OUTPUT_SW2, 2,1, 1);
static struct sbmix_elem snd_dt019x_ctl_synth_play_switch =
	SB_DOUBLE("Synth Playback Switch", SB_DT019X_OUTPUT_SW2, SB_DT019X_OUTPUT_SW2, 4,3, 1);
static struct sbmix_elem snd_dt019x_ctl_capture_source =
	{
		.name = "Capture Source",
		.type = SB_MIX_CAPTURE_DT019X
	};

static struct sbmix_elem *snd_dt019x_controls[] = {
	/* ALS4000 below has some parts which we might be lacking,
	 * e.g. snd_als4000_ctl_mono_playback_switch - check it! */
	&snd_dt019x_ctl_master_play_vol,
	&snd_dt019x_ctl_pcm_play_vol,
	&snd_dt019x_ctl_synth_play_vol,
	&snd_dt019x_ctl_cd_play_vol,
	&snd_dt019x_ctl_mic_play_vol,
	&snd_dt019x_ctl_pc_speaker_vol,
	&snd_dt019x_ctl_line_play_vol,
	&snd_sb16_ctl_mic_play_switch,
	&snd_sb16_ctl_cd_play_switch,
	&snd_sb16_ctl_line_play_switch,
	&snd_dt019x_ctl_pcm_play_switch,
	&snd_dt019x_ctl_synth_play_switch,
	&snd_dt019x_ctl_capture_source
};

static unsigned char snd_dt019x_init_values[][2] = {
        { SB_DT019X_MASTER_DEV, 0 },
        { SB_DT019X_PCM_DEV, 0 },
        { SB_DT019X_SYNTH_DEV, 0 },
        { SB_DT019X_CD_DEV, 0 },
        { SB_DT019X_MIC_DEV, 0 },	/* Includes PC-speaker in high nibble */
        { SB_DT019X_LINE_DEV, 0 },
        { SB_DSP4_OUTPUT_SW, 0 },
        { SB_DT019X_OUTPUT_SW2, 0 },
        { SB_DT019X_CAPTURE_SW, 0x06 },
};

/*
 * ALS4000 specific mixer elements
 */
static struct sbmix_elem snd_als4000_ctl_master_mono_playback_switch =
	SB_SINGLE("Master Mono Playback Switch", SB_ALS4000_MONO_IO_CTRL, 5, 1);
static struct sbmix_elem snd_als4k_ctl_master_mono_capture_route = {
		.name = "Master Mono Capture Route",
		.type = SB_MIX_MONO_CAPTURE_ALS4K
	};
static struct sbmix_elem snd_als4000_ctl_mono_playback_switch =
	SB_SINGLE("Mono Playback Switch", SB_DT019X_OUTPUT_SW2, 0, 1);
static struct sbmix_elem snd_als4000_ctl_mic_20db_boost =
	SB_SINGLE("Mic Boost (+20dB)", SB_ALS4000_MIC_IN_GAIN, 0, 0x03);
static struct sbmix_elem snd_als4000_ctl_mixer_analog_loopback =
	SB_SINGLE("Analog Loopback Switch", SB_ALS4000_MIC_IN_GAIN, 7, 0x01);
static struct sbmix_elem snd_als4000_ctl_mixer_digital_loopback =
	SB_SINGLE("Digital Loopback Switch",
		  SB_ALS4000_CR3_CONFIGURATION, 7, 0x01);
/* FIXME: functionality of 3D controls might be swapped, I didn't find
 * a description of how to identify what is supposed to be what */
static struct sbmix_elem snd_als4000_3d_control_switch =
	SB_SINGLE("3D Control - Switch", SB_ALS4000_3D_SND_FX, 6, 0x01);
static struct sbmix_elem snd_als4000_3d_control_ratio =
	SB_SINGLE("3D Control - Level", SB_ALS4000_3D_SND_FX, 0, 0x07);
static struct sbmix_elem snd_als4000_3d_control_freq =
	/* FIXME: maybe there's actually some standard 3D ctrl name for it?? */
	SB_SINGLE("3D Control - Freq", SB_ALS4000_3D_SND_FX, 4, 0x03);
static struct sbmix_elem snd_als4000_3d_control_delay =
	/* FIXME: ALS4000a.pdf mentions BBD (Bucket Brigade Device) time delay,
	 * but what ALSA 3D attribute is that actually? "Center", "Depth",
	 * "Wide" or "Space" or even "Level"? Assuming "Wide" for now... */
	SB_SINGLE("3D Control - Wide", SB_ALS4000_3D_TIME_DELAY, 0, 0x0f);
static struct sbmix_elem snd_als4000_3d_control_poweroff_switch =
	SB_SINGLE("3D PowerOff Switch", SB_ALS4000_3D_TIME_DELAY, 4, 0x01);
static struct sbmix_elem snd_als4000_ctl_3db_freq_control_switch =
	SB_SINGLE("Master Playback 8kHz / 20kHz LPF Switch",
		  SB_ALS4000_FMDAC, 5, 0x01);
#ifdef NOT_AVAILABLE
static struct sbmix_elem snd_als4000_ctl_fmdac =
	SB_SINGLE("FMDAC Switch (Option ?)", SB_ALS4000_FMDAC, 0, 0x01);
static struct sbmix_elem snd_als4000_ctl_qsound =
	SB_SINGLE("QSound Mode", SB_ALS4000_QSOUND, 1, 0x1f);
#endif

static struct sbmix_elem *snd_als4000_controls[] = {
						/* ALS4000a.PDF regs page */
	&snd_sb16_ctl_master_play_vol,		/* MX30/31 12 */
	&snd_dt019x_ctl_pcm_play_switch,	/* MX4C    16 */
	&snd_sb16_ctl_pcm_play_vol,		/* MX32/33 12 */
	&snd_sb16_ctl_synth_capture_route,	/* MX3D/3E 14 */
	&snd_dt019x_ctl_synth_play_switch,	/* MX4C    16 */
	&snd_sb16_ctl_synth_play_vol,		/* MX34/35 12/13 */
	&snd_sb16_ctl_cd_capture_route,		/* MX3D/3E 14 */
	&snd_sb16_ctl_cd_play_switch,		/* MX3C    14 */
	&snd_sb16_ctl_cd_play_vol,		/* MX36/37 13 */
	&snd_sb16_ctl_line_capture_route,	/* MX3D/3E 14 */
	&snd_sb16_ctl_line_play_switch,		/* MX3C    14 */
	&snd_sb16_ctl_line_play_vol,		/* MX38/39 13 */
	&snd_sb16_ctl_mic_capture_route,	/* MX3D/3E 14 */
	&snd_als4000_ctl_mic_20db_boost,	/* MX4D    16 */
	&snd_sb16_ctl_mic_play_switch,		/* MX3C    14 */
	&snd_sb16_ctl_mic_play_vol,		/* MX3A    13 */
	&snd_sb16_ctl_pc_speaker_vol,		/* MX3B    14 */
	&snd_sb16_ctl_capture_vol,		/* MX3F/40 15 */
	&snd_sb16_ctl_play_vol,			/* MX41/42 15 */
	&snd_als4000_ctl_master_mono_playback_switch, /* MX4C 16 */
	&snd_als4k_ctl_master_mono_capture_route, /* MX4B  16 */
	&snd_als4000_ctl_mono_playback_switch,	/* MX4C    16 */
	&snd_als4000_ctl_mixer_analog_loopback, /* MX4D    16 */
	&snd_als4000_ctl_mixer_digital_loopback, /* CR3    21 */
	&snd_als4000_3d_control_switch,		 /* MX50   17 */
	&snd_als4000_3d_control_ratio,		 /* MX50   17 */
	&snd_als4000_3d_control_freq,		 /* MX50   17 */
	&snd_als4000_3d_control_delay,		 /* MX51   18 */
	&snd_als4000_3d_control_poweroff_switch,	/* MX51    18 */
	&snd_als4000_ctl_3db_freq_control_switch,	/* MX4F    17 */
#ifdef NOT_AVAILABLE
	&snd_als4000_ctl_fmdac,
	&snd_als4000_ctl_qsound,
#endif
};

static unsigned char snd_als4000_init_values[][2] = {
	{ SB_DSP4_MASTER_DEV + 0, 0 },
	{ SB_DSP4_MASTER_DEV + 1, 0 },
	{ SB_DSP4_PCM_DEV + 0, 0 },
	{ SB_DSP4_PCM_DEV + 1, 0 },
	{ SB_DSP4_SYNTH_DEV + 0, 0 },
	{ SB_DSP4_SYNTH_DEV + 1, 0 },
	{ SB_DSP4_SPEAKER_DEV, 0 },
	{ SB_DSP4_OUTPUT_SW, 0 },
	{ SB_DSP4_INPUT_LEFT, 0 },
	{ SB_DSP4_INPUT_RIGHT, 0 },
	{ SB_DT019X_OUTPUT_SW2, 0 },
	{ SB_ALS4000_MIC_IN_GAIN, 0 },
};


/*
 */
static int snd_sbmixer_init(struct snd_sb *chip,
			    struct sbmix_elem **controls,
			    int controls_count,
			    unsigned char map[][2],
			    int map_count,
			    char *name)
{
	unsigned long flags;
	struct snd_card *card = chip->card;
	int idx, err;

	/* mixer reset */
	spin_lock_irqsave(&chip->mixer_lock, flags);
	snd_sbmixer_write(chip, 0x00, 0x00);
	spin_unlock_irqrestore(&chip->mixer_lock, flags);

	/* mute and zero volume channels */
	for (idx = 0; idx < map_count; idx++) {
		spin_lock_irqsave(&chip->mixer_lock, flags);
		snd_sbmixer_write(chip, map[idx][0], map[idx][1]);
		spin_unlock_irqrestore(&chip->mixer_lock, flags);
	}

	for (idx = 0; idx < controls_count; idx++) {
		if ((err = snd_sbmixer_add_ctl_elem(chip, controls[idx])) < 0)
			return err;
	}
	snd_component_add(card, name);
	strcpy(card->mixername, name);
	return 0;
}

int snd_sbmixer_new(struct snd_sb *chip)
{
	struct snd_card *card;
	int err;

	if (snd_BUG_ON(!chip || !chip->card))
		return -EINVAL;

	card = chip->card;

	switch (chip->hardware) {
	case SB_HW_10:
		return 0; /* no mixer chip on SB1.x */
	case SB_HW_20:
	case SB_HW_201:
		if ((err = snd_sbmixer_init(chip,
					    snd_sb20_controls,
					    ARRAY_SIZE(snd_sb20_controls),
					    snd_sb20_init_values,
					    ARRAY_SIZE(snd_sb20_init_values),
					    "CTL1335")) < 0)
			return err;
		break;
	case SB_HW_PRO:
		if ((err = snd_sbmixer_init(chip,
					    snd_sbpro_controls,
					    ARRAY_SIZE(snd_sbpro_controls),
					    snd_sbpro_init_values,
					    ARRAY_SIZE(snd_sbpro_init_values),
					    "CTL1345")) < 0)
			return err;
		break;
	case SB_HW_16:
	case SB_HW_ALS100:
	case SB_HW_CS5530:
		if ((err = snd_sbmixer_init(chip,
					    snd_sb16_controls,
					    ARRAY_SIZE(snd_sb16_controls),
					    snd_sb16_init_values,
					    ARRAY_SIZE(snd_sb16_init_values),
					    "CTL1745")) < 0)
			return err;
		break;
	case SB_HW_ALS4000:
		if ((err = snd_sbmixer_init(chip,
					    snd_als4000_controls,
					    ARRAY_SIZE(snd_als4000_controls),
					    snd_als4000_init_values,
					    ARRAY_SIZE(snd_als4000_init_values),
					    "ALS4000")) < 0)
			return err;
		break;
	case SB_HW_DT019X:
		if ((err = snd_sbmixer_init(chip,
					    snd_dt019x_controls,
					    ARRAY_SIZE(snd_dt019x_controls),
					    snd_dt019x_init_values,
					    ARRAY_SIZE(snd_dt019x_init_values),
					    "DT019X")) < 0)
		break;
	default:
		strcpy(card->mixername, "???");
	}
	return 0;
}

#ifdef CONFIG_PM
static unsigned char sb20_saved_regs[] = {
	SB_DSP20_MASTER_DEV,
	SB_DSP20_PCM_DEV,
	SB_DSP20_FM_DEV,
	SB_DSP20_CD_DEV,
};

static unsigned char sbpro_saved_regs[] = {
	SB_DSP_MASTER_DEV,
	SB_DSP_PCM_DEV,
	SB_DSP_PLAYBACK_FILT,
	SB_DSP_FM_DEV,
	SB_DSP_CD_DEV,
	SB_DSP_LINE_DEV,
	SB_DSP_MIC_DEV,
	SB_DSP_CAPTURE_SOURCE,
	SB_DSP_CAPTURE_FILT,
};

static unsigned char sb16_saved_regs[] = {
	SB_DSP4_MASTER_DEV, SB_DSP4_MASTER_DEV + 1,
	SB_DSP4_3DSE,
	SB_DSP4_BASS_DEV, SB_DSP4_BASS_DEV + 1,
	SB_DSP4_TREBLE_DEV, SB_DSP4_TREBLE_DEV + 1,
	SB_DSP4_PCM_DEV, SB_DSP4_PCM_DEV + 1,
	SB_DSP4_INPUT_LEFT, SB_DSP4_INPUT_RIGHT,
	SB_DSP4_SYNTH_DEV, SB_DSP4_SYNTH_DEV + 1,
	SB_DSP4_OUTPUT_SW,
	SB_DSP4_CD_DEV, SB_DSP4_CD_DEV + 1,
	SB_DSP4_LINE_DEV, SB_DSP4_LINE_DEV + 1,
	SB_DSP4_MIC_DEV,
	SB_DSP4_SPEAKER_DEV,
	SB_DSP4_IGAIN_DEV, SB_DSP4_IGAIN_DEV + 1,
	SB_DSP4_OGAIN_DEV, SB_DSP4_OGAIN_DEV + 1,
	SB_DSP4_MIC_AGC
};

static unsigned char dt019x_saved_regs[] = {
	SB_DT019X_MASTER_DEV,
	SB_DT019X_PCM_DEV,
	SB_DT019X_SYNTH_DEV,
	SB_DT019X_CD_DEV,
	SB_DT019X_MIC_DEV,
	SB_DT019X_SPKR_DEV,
	SB_DT019X_LINE_DEV,
	SB_DSP4_OUTPUT_SW,
	SB_DT019X_OUTPUT_SW2,
	SB_DT019X_CAPTURE_SW,
};

static unsigned char als4000_saved_regs[] = {
	/* please verify in dsheet whether regs to be added
	   are actually real H/W or just dummy */
	SB_DSP4_MASTER_DEV, SB_DSP4_MASTER_DEV + 1,
	SB_DSP4_OUTPUT_SW,
	SB_DSP4_PCM_DEV, SB_DSP4_PCM_DEV + 1,
	SB_DSP4_INPUT_LEFT, SB_DSP4_INPUT_RIGHT,
	SB_DSP4_SYNTH_DEV, SB_DSP4_SYNTH_DEV + 1,
	SB_DSP4_CD_DEV, SB_DSP4_CD_DEV + 1,
	SB_DSP4_MIC_DEV,
	SB_DSP4_SPEAKER_DEV,
	SB_DSP4_IGAIN_DEV, SB_DSP4_IGAIN_DEV + 1,
	SB_DSP4_OGAIN_DEV, SB_DSP4_OGAIN_DEV + 1,
	SB_DT019X_OUTPUT_SW2,
	SB_ALS4000_MONO_IO_CTRL,
	SB_ALS4000_MIC_IN_GAIN,
	SB_ALS4000_FMDAC,
	SB_ALS4000_3D_SND_FX,
	SB_ALS4000_3D_TIME_DELAY,
	SB_ALS4000_CR3_CONFIGURATION,
};

static void save_mixer(struct snd_sb *chip, unsigned char *regs, int num_regs)
{
	unsigned char *val = chip->saved_regs;
	if (snd_BUG_ON(num_regs > ARRAY_SIZE(chip->saved_regs)))
		return;
	for (; num_regs; num_regs--)
		*val++ = snd_sbmixer_read(chip, *regs++);
}

static void restore_mixer(struct snd_sb *chip, unsigned char *regs, int num_regs)
{
	unsigned char *val = chip->saved_regs;
	if (snd_BUG_ON(num_regs > ARRAY_SIZE(chip->saved_regs)))
		return;
	for (; num_regs; num_regs--)
		snd_sbmixer_write(chip, *regs++, *val++);
}

void snd_sbmixer_suspend(struct snd_sb *chip)
{
	switch (chip->hardware) {
	case SB_HW_20:
	case SB_HW_201:
		save_mixer(chip, sb20_saved_regs, ARRAY_SIZE(sb20_saved_regs));
		break;
	case SB_HW_PRO:
		save_mixer(chip, sbpro_saved_regs, ARRAY_SIZE(sbpro_saved_regs));
		break;
	case SB_HW_16:
	case SB_HW_ALS100:
	case SB_HW_CS5530:
		save_mixer(chip, sb16_saved_regs, ARRAY_SIZE(sb16_saved_regs));
		break;
	case SB_HW_ALS4000:
		save_mixer(chip, als4000_saved_regs, ARRAY_SIZE(als4000_saved_regs));
		break;
	case SB_HW_DT019X:
		save_mixer(chip, dt019x_saved_regs, ARRAY_SIZE(dt019x_saved_regs));
		break;
	default:
		break;
	}
}

void snd_sbmixer_resume(struct snd_sb *chip)
{
	switch (chip->hardware) {
	case SB_HW_20:
	case SB_HW_201:
		restore_mixer(chip, sb20_saved_regs, ARRAY_SIZE(sb20_saved_regs));
		break;
	case SB_HW_PRO:
		restore_mixer(chip, sbpro_saved_regs, ARRAY_SIZE(sbpro_saved_regs));
		break;
	case SB_HW_16:
	case SB_HW_ALS100:
	case SB_HW_CS5530:
		restore_mixer(chip, sb16_saved_regs, ARRAY_SIZE(sb16_saved_regs));
		break;
	case SB_HW_ALS4000:
		restore_mixer(chip, als4000_saved_regs, ARRAY_SIZE(als4000_saved_regs));
		break;
	case SB_HW_DT019X:
		restore_mixer(chip, dt019x_saved_regs, ARRAY_SIZE(dt019x_saved_regs));
		break;
	default:
		break;
	}
}
#endif
