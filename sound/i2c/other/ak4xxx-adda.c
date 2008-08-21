/*
 *   ALSA driver for AK4524 / AK4528 / AK4529 / AK4355 / AK4358 / AK4381
 *   AD and DA converters
 *
 *	Copyright (c) 2000-2004 Jaroslav Kysela <perex@perex.cz>,
 *				Takashi Iwai <tiwai@suse.de>
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
#include <linux/interrupt.h>
#include <linux/init.h>
#include <sound/core.h>
#include <sound/control.h>
#include <sound/tlv.h>
#include <sound/ak4xxx-adda.h>

MODULE_AUTHOR("Jaroslav Kysela <perex@perex.cz>, Takashi Iwai <tiwai@suse.de>");
MODULE_DESCRIPTION("Routines for control of AK452x / AK43xx  AD/DA converters");
MODULE_LICENSE("GPL");

/* write the given register and save the data to the cache */
void snd_akm4xxx_write(struct snd_akm4xxx *ak, int chip, unsigned char reg,
		       unsigned char val)
{
	ak->ops.lock(ak, chip);
	ak->ops.write(ak, chip, reg, val);

	/* save the data */
	snd_akm4xxx_set(ak, chip, reg, val);
	ak->ops.unlock(ak, chip);
}

EXPORT_SYMBOL(snd_akm4xxx_write);

/* reset procedure for AK4524 and AK4528 */
static void ak4524_reset(struct snd_akm4xxx *ak, int state)
{
	unsigned int chip;
	unsigned char reg, maxreg;

	if (ak->type == SND_AK4528)
		maxreg = 0x06;
	else
		maxreg = 0x08;
	for (chip = 0; chip < ak->num_dacs/2; chip++) {
		snd_akm4xxx_write(ak, chip, 0x01, state ? 0x00 : 0x03);
		if (state)
			continue;
		/* DAC volumes */
		for (reg = 0x04; reg < maxreg; reg++)
			snd_akm4xxx_write(ak, chip, reg,
					  snd_akm4xxx_get(ak, chip, reg));
	}
}

/* reset procedure for AK4355 and AK4358 */
static void ak435X_reset(struct snd_akm4xxx *ak, int state,
		unsigned char total_regs)
{
	unsigned char reg;

	if (state) {
		snd_akm4xxx_write(ak, 0, 0x01, 0x02); /* reset and soft-mute */
		return;
	}
	for (reg = 0x00; reg < total_regs; reg++)
		if (reg != 0x01)
			snd_akm4xxx_write(ak, 0, reg,
					  snd_akm4xxx_get(ak, 0, reg));
	snd_akm4xxx_write(ak, 0, 0x01, 0x01); /* un-reset, unmute */
}

/* reset procedure for AK4381 */
static void ak4381_reset(struct snd_akm4xxx *ak, int state)
{
	unsigned int chip;
	unsigned char reg;

	for (chip = 0; chip < ak->num_dacs/2; chip++) {
		snd_akm4xxx_write(ak, chip, 0x00, state ? 0x0c : 0x0f);
		if (state)
			continue;
		for (reg = 0x01; reg < 0x05; reg++)
			snd_akm4xxx_write(ak, chip, reg,
					  snd_akm4xxx_get(ak, chip, reg));
	}
}

/*
 * reset the AKM codecs
 * @state: 1 = reset codec, 0 = restore the registers
 *
 * assert the reset operation and restores the register values to the chips.
 */
void snd_akm4xxx_reset(struct snd_akm4xxx *ak, int state)
{
	switch (ak->type) {
	case SND_AK4524:
	case SND_AK4528:
		ak4524_reset(ak, state);
		break;
	case SND_AK4529:
		/* FIXME: needed for ak4529? */
		break;
	case SND_AK4355:
		ak435X_reset(ak, state, 0x0b);
		break;
	case SND_AK4358:
		ak435X_reset(ak, state, 0x10);
		break;
	case SND_AK4381:
		ak4381_reset(ak, state);
		break;
	default:
		break;
	}
}

EXPORT_SYMBOL(snd_akm4xxx_reset);


/*
 * Volume conversion table for non-linear volumes
 * from -63.5dB (mute) to 0dB step 0.5dB
 *
 * Used for AK4524 input/ouput attenuation, AK4528, and
 * AK5365 input attenuation
 */
static const unsigned char vol_cvt_datt[128] = {
	0x00, 0x01, 0x01, 0x02, 0x02, 0x03, 0x03, 0x04,
	0x04, 0x04, 0x04, 0x05, 0x05, 0x05, 0x06, 0x06,
	0x06, 0x07, 0x07, 0x08, 0x08, 0x08, 0x09, 0x0a,
	0x0a, 0x0b, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x0f,
	0x10, 0x10, 0x11, 0x12, 0x12, 0x13, 0x13, 0x14,
	0x15, 0x16, 0x17, 0x17, 0x18, 0x19, 0x1a, 0x1c,
	0x1d, 0x1e, 0x1f, 0x20, 0x21, 0x22, 0x23, 0x23,
	0x24, 0x25, 0x26, 0x28, 0x29, 0x2a, 0x2b, 0x2d,
	0x2e, 0x30, 0x30, 0x31, 0x32, 0x33, 0x34, 0x35,
	0x37, 0x38, 0x39, 0x3b, 0x3c, 0x3e, 0x3f, 0x40,
	0x41, 0x42, 0x43, 0x44, 0x46, 0x47, 0x48, 0x4a,
	0x4b, 0x4d, 0x4e, 0x50, 0x51, 0x52, 0x53, 0x54,
	0x55, 0x56, 0x58, 0x59, 0x5b, 0x5c, 0x5e, 0x5f,
	0x60, 0x61, 0x62, 0x64, 0x65, 0x66, 0x67, 0x69,
	0x6a, 0x6c, 0x6d, 0x6f, 0x70, 0x71, 0x72, 0x73,
	0x75, 0x76, 0x77, 0x79, 0x7a, 0x7c, 0x7d, 0x7f,
};

/*
 * dB tables
 */
static const DECLARE_TLV_DB_SCALE(db_scale_vol_datt, -6350, 50, 1);
static const DECLARE_TLV_DB_SCALE(db_scale_8bit, -12750, 50, 1);
static const DECLARE_TLV_DB_SCALE(db_scale_7bit, -6350, 50, 1);
static const DECLARE_TLV_DB_LINEAR(db_scale_linear, TLV_DB_GAIN_MUTE, 0);

/*
 * initialize all the ak4xxx chips
 */
void snd_akm4xxx_init(struct snd_akm4xxx *ak)
{
	static const unsigned char inits_ak4524[] = {
		0x00, 0x07, /* 0: all power up */
		0x01, 0x00, /* 1: ADC/DAC reset */
		0x02, 0x60, /* 2: 24bit I2S */
		0x03, 0x19, /* 3: deemphasis off */
		0x01, 0x03, /* 1: ADC/DAC enable */
		0x04, 0x00, /* 4: ADC left muted */
		0x05, 0x00, /* 5: ADC right muted */
		0x06, 0x00, /* 6: DAC left muted */
		0x07, 0x00, /* 7: DAC right muted */
		0xff, 0xff
	};
	static const unsigned char inits_ak4528[] = {
		0x00, 0x07, /* 0: all power up */
		0x01, 0x00, /* 1: ADC/DAC reset */
		0x02, 0x60, /* 2: 24bit I2S */
		0x03, 0x0d, /* 3: deemphasis off, turn LR highpass filters on */
		0x01, 0x03, /* 1: ADC/DAC enable */
		0x04, 0x00, /* 4: ADC left muted */
		0x05, 0x00, /* 5: ADC right muted */
		0xff, 0xff
	};
	static const unsigned char inits_ak4529[] = {
		0x09, 0x01, /* 9: ATS=0, RSTN=1 */
		0x0a, 0x3f, /* A: all power up, no zero/overflow detection */
		0x00, 0x0c, /* 0: TDM=0, 24bit I2S, SMUTE=0 */
		0x01, 0x00, /* 1: ACKS=0, ADC, loop off */
		0x02, 0xff, /* 2: LOUT1 muted */
		0x03, 0xff, /* 3: ROUT1 muted */
		0x04, 0xff, /* 4: LOUT2 muted */
		0x05, 0xff, /* 5: ROUT2 muted */
		0x06, 0xff, /* 6: LOUT3 muted */
		0x07, 0xff, /* 7: ROUT3 muted */
		0x0b, 0xff, /* B: LOUT4 muted */
		0x0c, 0xff, /* C: ROUT4 muted */
		0x08, 0x55, /* 8: deemphasis all off */
		0xff, 0xff
	};
	static const unsigned char inits_ak4355[] = {
		0x01, 0x02, /* 1: reset and soft-mute */
		0x00, 0x06, /* 0: mode3(i2s), disable auto-clock detect,
			     * disable DZF, sharp roll-off, RSTN#=0 */
		0x02, 0x0e, /* 2: DA's power up, normal speed, RSTN#=0 */
		// 0x02, 0x2e, /* quad speed */
		0x03, 0x01, /* 3: de-emphasis off */
		0x04, 0x00, /* 4: LOUT1 volume muted */
		0x05, 0x00, /* 5: ROUT1 volume muted */
		0x06, 0x00, /* 6: LOUT2 volume muted */
		0x07, 0x00, /* 7: ROUT2 volume muted */
		0x08, 0x00, /* 8: LOUT3 volume muted */
		0x09, 0x00, /* 9: ROUT3 volume muted */
		0x0a, 0x00, /* a: DATT speed=0, ignore DZF */
		0x01, 0x01, /* 1: un-reset, unmute */
		0xff, 0xff
	};
	static const unsigned char inits_ak4358[] = {
		0x01, 0x02, /* 1: reset and soft-mute */
		0x00, 0x06, /* 0: mode3(i2s), disable auto-clock detect,
			     * disable DZF, sharp roll-off, RSTN#=0 */
		0x02, 0x4e, /* 2: DA's power up, normal speed, RSTN#=0 */
		/* 0x02, 0x6e,*/ /* quad speed */
		0x03, 0x01, /* 3: de-emphasis off */
		0x04, 0x00, /* 4: LOUT1 volume muted */
		0x05, 0x00, /* 5: ROUT1 volume muted */
		0x06, 0x00, /* 6: LOUT2 volume muted */
		0x07, 0x00, /* 7: ROUT2 volume muted */
		0x08, 0x00, /* 8: LOUT3 volume muted */
		0x09, 0x00, /* 9: ROUT3 volume muted */
		0x0b, 0x00, /* b: LOUT4 volume muted */
		0x0c, 0x00, /* c: ROUT4 volume muted */
		0x0a, 0x00, /* a: DATT speed=0, ignore DZF */
		0x01, 0x01, /* 1: un-reset, unmute */
		0xff, 0xff
	};
	static const unsigned char inits_ak4381[] = {
		0x00, 0x0c, /* 0: mode3(i2s), disable auto-clock detect */
		0x01, 0x02, /* 1: de-emphasis off, normal speed,
			     * sharp roll-off, DZF off */
		// 0x01, 0x12, /* quad speed */
		0x02, 0x00, /* 2: DZF disabled */
		0x03, 0x00, /* 3: LATT 0 */
		0x04, 0x00, /* 4: RATT 0 */
		0x00, 0x0f, /* 0: power-up, un-reset */
		0xff, 0xff
	};

	int chip, num_chips;
	const unsigned char *ptr, *inits;
	unsigned char reg, data;

	memset(ak->images, 0, sizeof(ak->images));
	memset(ak->volumes, 0, sizeof(ak->volumes));

	switch (ak->type) {
	case SND_AK4524:
		inits = inits_ak4524;
		num_chips = ak->num_dacs / 2;
		break;
	case SND_AK4528:
		inits = inits_ak4528;
		num_chips = ak->num_dacs / 2;
		break;
	case SND_AK4529:
		inits = inits_ak4529;
		num_chips = 1;
		break;
	case SND_AK4355:
		inits = inits_ak4355;
		num_chips = 1;
		break;
	case SND_AK4358:
		inits = inits_ak4358;
		num_chips = 1;
		break;
	case SND_AK4381:
		inits = inits_ak4381;
		num_chips = ak->num_dacs / 2;
		break;
	case SND_AK5365:
		/* FIXME: any init sequence? */
		return;
	default:
		snd_BUG();
		return;
	}

	for (chip = 0; chip < num_chips; chip++) {
		ptr = inits;
		while (*ptr != 0xff) {
			reg = *ptr++;
			data = *ptr++;
			snd_akm4xxx_write(ak, chip, reg, data);
		}
	}
}

EXPORT_SYMBOL(snd_akm4xxx_init);

/*
 * Mixer callbacks
 */
#define AK_IPGA 			(1<<20)	/* including IPGA */
#define AK_VOL_CVT 			(1<<21)	/* need dB conversion */
#define AK_NEEDSMSB 			(1<<22)	/* need MSB update bit */
#define AK_INVERT 			(1<<23)	/* data is inverted */
#define AK_GET_CHIP(val)		(((val) >> 8) & 0xff)
#define AK_GET_ADDR(val)		((val) & 0xff)
#define AK_GET_SHIFT(val)		(((val) >> 16) & 0x0f)
#define AK_GET_VOL_CVT(val)		(((val) >> 21) & 1)
#define AK_GET_IPGA(val)		(((val) >> 20) & 1)
#define AK_GET_NEEDSMSB(val)		(((val) >> 22) & 1)
#define AK_GET_INVERT(val)		(((val) >> 23) & 1)
#define AK_GET_MASK(val)		(((val) >> 24) & 0xff)
#define AK_COMPOSE(chip,addr,shift,mask) \
	(((chip) << 8) | (addr) | ((shift) << 16) | ((mask) << 24))

static int snd_akm4xxx_volume_info(struct snd_kcontrol *kcontrol,
				   struct snd_ctl_elem_info *uinfo)
{
	unsigned int mask = AK_GET_MASK(kcontrol->private_value);

	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = mask;
	return 0;
}

static int snd_akm4xxx_volume_get(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_akm4xxx *ak = snd_kcontrol_chip(kcontrol);
	int chip = AK_GET_CHIP(kcontrol->private_value);
	int addr = AK_GET_ADDR(kcontrol->private_value);

	ucontrol->value.integer.value[0] = snd_akm4xxx_get_vol(ak, chip, addr);
	return 0;
}

static int put_ak_reg(struct snd_kcontrol *kcontrol, int addr,
		      unsigned char nval)
{
	struct snd_akm4xxx *ak = snd_kcontrol_chip(kcontrol);
	unsigned int mask = AK_GET_MASK(kcontrol->private_value);
	int chip = AK_GET_CHIP(kcontrol->private_value);

	if (snd_akm4xxx_get_vol(ak, chip, addr) == nval)
		return 0;

	snd_akm4xxx_set_vol(ak, chip, addr, nval);
	if (AK_GET_VOL_CVT(kcontrol->private_value) && nval < 128)
		nval = vol_cvt_datt[nval];
	if (AK_GET_IPGA(kcontrol->private_value) && nval >= 128)
		nval++; /* need to correct + 1 since both 127 and 128 are 0dB */
	if (AK_GET_INVERT(kcontrol->private_value))
		nval = mask - nval;
	if (AK_GET_NEEDSMSB(kcontrol->private_value))
		nval |= 0x80;
	/* printk(KERN_DEBUG "DEBUG - AK writing reg: chip %x addr %x,
	   nval %x\n", chip, addr, nval); */
	snd_akm4xxx_write(ak, chip, addr, nval);
	return 1;
}

static int snd_akm4xxx_volume_put(struct snd_kcontrol *kcontrol,
				  struct snd_ctl_elem_value *ucontrol)
{
	unsigned int mask = AK_GET_MASK(kcontrol->private_value);
	unsigned int val = ucontrol->value.integer.value[0];
	if (val > mask)
		return -EINVAL;
	return put_ak_reg(kcontrol, AK_GET_ADDR(kcontrol->private_value), val);
}

static int snd_akm4xxx_stereo_volume_info(struct snd_kcontrol *kcontrol,
					  struct snd_ctl_elem_info *uinfo)
{
	unsigned int mask = AK_GET_MASK(kcontrol->private_value);

	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = mask;
	return 0;
}

static int snd_akm4xxx_stereo_volume_get(struct snd_kcontrol *kcontrol,
					 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_akm4xxx *ak = snd_kcontrol_chip(kcontrol);
	int chip = AK_GET_CHIP(kcontrol->private_value);
	int addr = AK_GET_ADDR(kcontrol->private_value);

	ucontrol->value.integer.value[0] = snd_akm4xxx_get_vol(ak, chip, addr);
	ucontrol->value.integer.value[1] = snd_akm4xxx_get_vol(ak, chip, addr+1);
	return 0;
}

static int snd_akm4xxx_stereo_volume_put(struct snd_kcontrol *kcontrol,
					 struct snd_ctl_elem_value *ucontrol)
{
	int addr = AK_GET_ADDR(kcontrol->private_value);
	unsigned int mask = AK_GET_MASK(kcontrol->private_value);
	unsigned int val[2];
	int change;

	val[0] = ucontrol->value.integer.value[0];
	val[1] = ucontrol->value.integer.value[1];
	if (val[0] > mask || val[1] > mask)
		return -EINVAL;
	change = put_ak_reg(kcontrol, addr, val[0]);
	change |= put_ak_reg(kcontrol, addr + 1, val[1]);
	return change;
}

static int snd_akm4xxx_deemphasis_info(struct snd_kcontrol *kcontrol,
				       struct snd_ctl_elem_info *uinfo)
{
	static char *texts[4] = {
		"44.1kHz", "Off", "48kHz", "32kHz",
	};
	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 4;
	if (uinfo->value.enumerated.item >= 4)
		uinfo->value.enumerated.item = 3;
	strcpy(uinfo->value.enumerated.name,
	       texts[uinfo->value.enumerated.item]);
	return 0;
}

static int snd_akm4xxx_deemphasis_get(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_akm4xxx *ak = snd_kcontrol_chip(kcontrol);
	int chip = AK_GET_CHIP(kcontrol->private_value);
	int addr = AK_GET_ADDR(kcontrol->private_value);
	int shift = AK_GET_SHIFT(kcontrol->private_value);
	ucontrol->value.enumerated.item[0] =
		(snd_akm4xxx_get(ak, chip, addr) >> shift) & 3;
	return 0;
}

static int snd_akm4xxx_deemphasis_put(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_akm4xxx *ak = snd_kcontrol_chip(kcontrol);
	int chip = AK_GET_CHIP(kcontrol->private_value);
	int addr = AK_GET_ADDR(kcontrol->private_value);
	int shift = AK_GET_SHIFT(kcontrol->private_value);
	unsigned char nval = ucontrol->value.enumerated.item[0] & 3;
	int change;
	
	nval = (nval << shift) |
		(snd_akm4xxx_get(ak, chip, addr) & ~(3 << shift));
	change = snd_akm4xxx_get(ak, chip, addr) != nval;
	if (change)
		snd_akm4xxx_write(ak, chip, addr, nval);
	return change;
}

#define ak4xxx_switch_info	snd_ctl_boolean_mono_info

static int ak4xxx_switch_get(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_akm4xxx *ak = snd_kcontrol_chip(kcontrol);
	int chip = AK_GET_CHIP(kcontrol->private_value);
	int addr = AK_GET_ADDR(kcontrol->private_value);
	int shift = AK_GET_SHIFT(kcontrol->private_value);
	int invert = AK_GET_INVERT(kcontrol->private_value);
	/* we observe the (1<<shift) bit only */
	unsigned char val = snd_akm4xxx_get(ak, chip, addr) & (1<<shift);
	if (invert)
		val = ! val;
	ucontrol->value.integer.value[0] = (val & (1<<shift)) != 0;
	return 0;
}

static int ak4xxx_switch_put(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_akm4xxx *ak = snd_kcontrol_chip(kcontrol);
	int chip = AK_GET_CHIP(kcontrol->private_value);
	int addr = AK_GET_ADDR(kcontrol->private_value);
	int shift = AK_GET_SHIFT(kcontrol->private_value);
	int invert = AK_GET_INVERT(kcontrol->private_value);
	long flag = ucontrol->value.integer.value[0];
	unsigned char val, oval;
	int change;

	if (invert)
		flag = ! flag;
	oval = snd_akm4xxx_get(ak, chip, addr);
	if (flag)
		val = oval | (1<<shift);
	else
		val = oval & ~(1<<shift);
	change = (oval != val);
	if (change)
		snd_akm4xxx_write(ak, chip, addr, val);
	return change;
}

#define AK5365_NUM_INPUTS 5

static int ak4xxx_capture_num_inputs(struct snd_akm4xxx *ak, int mixer_ch)
{
	int num_names;
	const char **input_names;

	input_names = ak->adc_info[mixer_ch].input_names;
	num_names = 0;
	while (num_names < AK5365_NUM_INPUTS && input_names[num_names])
		++num_names;
	return num_names;
}

static int ak4xxx_capture_source_info(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_info *uinfo)
{
	struct snd_akm4xxx *ak = snd_kcontrol_chip(kcontrol);
	int mixer_ch = AK_GET_SHIFT(kcontrol->private_value);
	const char **input_names;
	int  num_names, idx;

	num_names = ak4xxx_capture_num_inputs(ak, mixer_ch);
	if (!num_names)
		return -EINVAL;
	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = num_names;
	idx = uinfo->value.enumerated.item;
	if (idx >= num_names)
		return -EINVAL;
	input_names = ak->adc_info[mixer_ch].input_names;
	strncpy(uinfo->value.enumerated.name, input_names[idx],
		sizeof(uinfo->value.enumerated.name));
	return 0;
}

static int ak4xxx_capture_source_get(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_akm4xxx *ak = snd_kcontrol_chip(kcontrol);
	int chip = AK_GET_CHIP(kcontrol->private_value);
	int addr = AK_GET_ADDR(kcontrol->private_value);
	int mask = AK_GET_MASK(kcontrol->private_value);
	unsigned char val;

	val = snd_akm4xxx_get(ak, chip, addr) & mask;
	ucontrol->value.enumerated.item[0] = val;
	return 0;
}

static int ak4xxx_capture_source_put(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_akm4xxx *ak = snd_kcontrol_chip(kcontrol);
	int mixer_ch = AK_GET_SHIFT(kcontrol->private_value);
	int chip = AK_GET_CHIP(kcontrol->private_value);
	int addr = AK_GET_ADDR(kcontrol->private_value);
	int mask = AK_GET_MASK(kcontrol->private_value);
	unsigned char oval, val;
	int num_names = ak4xxx_capture_num_inputs(ak, mixer_ch);

	if (ucontrol->value.enumerated.item[0] >= num_names)
		return -EINVAL;

	oval = snd_akm4xxx_get(ak, chip, addr);
	val = oval & ~mask;
	val |= ucontrol->value.enumerated.item[0] & mask;
	if (val != oval) {
		snd_akm4xxx_write(ak, chip, addr, val);
		return 1;
	}
	return 0;
}

/*
 * build AK4xxx controls
 */

static int build_dac_controls(struct snd_akm4xxx *ak)
{
	int idx, err, mixer_ch, num_stereo;
	struct snd_kcontrol_new knew;

	mixer_ch = 0;
	for (idx = 0; idx < ak->num_dacs; ) {
		/* mute control for Revolution 7.1 - AK4381 */
		if (ak->type == SND_AK4381 
				&&  ak->dac_info[mixer_ch].switch_name) {
			memset(&knew, 0, sizeof(knew));
			knew.iface = SNDRV_CTL_ELEM_IFACE_MIXER;
			knew.count = 1;
			knew.access = SNDRV_CTL_ELEM_ACCESS_READWRITE;
			knew.name = ak->dac_info[mixer_ch].switch_name;
			knew.info = ak4xxx_switch_info;
			knew.get = ak4xxx_switch_get;
			knew.put = ak4xxx_switch_put;
			knew.access = 0;
			/* register 1, bit 0 (SMUTE): 0 = normal operation,
			   1 = mute */
			knew.private_value =
				AK_COMPOSE(idx/2, 1, 0, 0) | AK_INVERT;
			err = snd_ctl_add(ak->card, snd_ctl_new1(&knew, ak));
			if (err < 0)
				return err;
		}
		memset(&knew, 0, sizeof(knew));
		if (! ak->dac_info || ! ak->dac_info[mixer_ch].name) {
			knew.name = "DAC Volume";
			knew.index = mixer_ch + ak->idx_offset * 2;
			num_stereo = 1;
		} else {
			knew.name = ak->dac_info[mixer_ch].name;
			num_stereo = ak->dac_info[mixer_ch].num_channels;
		}
		knew.iface = SNDRV_CTL_ELEM_IFACE_MIXER;
		knew.count = 1;
		knew.access = SNDRV_CTL_ELEM_ACCESS_READWRITE |
			SNDRV_CTL_ELEM_ACCESS_TLV_READ;
		if (num_stereo == 2) {
			knew.info = snd_akm4xxx_stereo_volume_info;
			knew.get = snd_akm4xxx_stereo_volume_get;
			knew.put = snd_akm4xxx_stereo_volume_put;
		} else {
			knew.info = snd_akm4xxx_volume_info;
			knew.get = snd_akm4xxx_volume_get;
			knew.put = snd_akm4xxx_volume_put;
		}
		switch (ak->type) {
		case SND_AK4524:
			/* register 6 & 7 */
			knew.private_value =
				AK_COMPOSE(idx/2, (idx%2) + 6, 0, 127) |
				AK_VOL_CVT;
			knew.tlv.p = db_scale_vol_datt;
			break;
		case SND_AK4528:
			/* register 4 & 5 */
			knew.private_value =
				AK_COMPOSE(idx/2, (idx%2) + 4, 0, 127) |
				AK_VOL_CVT;
			knew.tlv.p = db_scale_vol_datt;
			break;
		case SND_AK4529: {
			/* registers 2-7 and b,c */
			int val = idx < 6 ? idx + 2 : (idx - 6) + 0xb;
			knew.private_value =
				AK_COMPOSE(0, val, 0, 255) | AK_INVERT;
			knew.tlv.p = db_scale_8bit;
			break;
		}
		case SND_AK4355:
			/* register 4-9, chip #0 only */
			knew.private_value = AK_COMPOSE(0, idx + 4, 0, 255);
			knew.tlv.p = db_scale_8bit;
			break;
		case SND_AK4358: {
			/* register 4-9 and 11-12, chip #0 only */
			int  addr = idx < 6 ? idx + 4 : idx + 5;
			knew.private_value =
				AK_COMPOSE(0, addr, 0, 127) | AK_NEEDSMSB;
			knew.tlv.p = db_scale_7bit;
			break;
		}
		case SND_AK4381:
			/* register 3 & 4 */
			knew.private_value =
				AK_COMPOSE(idx/2, (idx%2) + 3, 0, 255);
			knew.tlv.p = db_scale_linear;
			break;
		default:
			return -EINVAL;
		}

		err = snd_ctl_add(ak->card, snd_ctl_new1(&knew, ak));
		if (err < 0)
			return err;

		idx += num_stereo;
		mixer_ch++;
	}
	return 0;
}

static int build_adc_controls(struct snd_akm4xxx *ak)
{
	int idx, err, mixer_ch, num_stereo;
	struct snd_kcontrol_new knew;

	mixer_ch = 0;
	for (idx = 0; idx < ak->num_adcs;) {
		memset(&knew, 0, sizeof(knew));
		if (! ak->adc_info || ! ak->adc_info[mixer_ch].name) {
			knew.name = "ADC Volume";
			knew.index = mixer_ch + ak->idx_offset * 2;
			num_stereo = 1;
		} else {
			knew.name = ak->adc_info[mixer_ch].name;
			num_stereo = ak->adc_info[mixer_ch].num_channels;
		}
		knew.iface = SNDRV_CTL_ELEM_IFACE_MIXER;
		knew.count = 1;
		knew.access = SNDRV_CTL_ELEM_ACCESS_READWRITE |
			SNDRV_CTL_ELEM_ACCESS_TLV_READ;
		if (num_stereo == 2) {
			knew.info = snd_akm4xxx_stereo_volume_info;
			knew.get = snd_akm4xxx_stereo_volume_get;
			knew.put = snd_akm4xxx_stereo_volume_put;
		} else {
			knew.info = snd_akm4xxx_volume_info;
			knew.get = snd_akm4xxx_volume_get;
			knew.put = snd_akm4xxx_volume_put;
		}
		/* register 4 & 5 */
		if (ak->type == SND_AK5365)
			knew.private_value =
				AK_COMPOSE(idx/2, (idx%2) + 4, 0, 151) |
				AK_VOL_CVT | AK_IPGA;
		else
			knew.private_value =
				AK_COMPOSE(idx/2, (idx%2) + 4, 0, 163) |
				AK_VOL_CVT | AK_IPGA;
		knew.tlv.p = db_scale_vol_datt;
		err = snd_ctl_add(ak->card, snd_ctl_new1(&knew, ak));
		if (err < 0)
			return err;

		if (ak->type == SND_AK5365 && (idx % 2) == 0) {
			if (! ak->adc_info || 
			    ! ak->adc_info[mixer_ch].switch_name) {
				knew.name = "Capture Switch";
				knew.index = mixer_ch + ak->idx_offset * 2;
			} else
				knew.name = ak->adc_info[mixer_ch].switch_name;
			knew.info = ak4xxx_switch_info;
			knew.get = ak4xxx_switch_get;
			knew.put = ak4xxx_switch_put;
			knew.access = 0;
			/* register 2, bit 0 (SMUTE): 0 = normal operation,
			   1 = mute */
			knew.private_value =
				AK_COMPOSE(idx/2, 2, 0, 0) | AK_INVERT;
			err = snd_ctl_add(ak->card, snd_ctl_new1(&knew, ak));
			if (err < 0)
				return err;

			memset(&knew, 0, sizeof(knew));
			knew.name = ak->adc_info[mixer_ch].selector_name;
			if (!knew.name) {
				knew.name = "Capture Channel";
				knew.index = mixer_ch + ak->idx_offset * 2;
			}

			knew.iface = SNDRV_CTL_ELEM_IFACE_MIXER;
			knew.info = ak4xxx_capture_source_info;
			knew.get = ak4xxx_capture_source_get;
			knew.put = ak4xxx_capture_source_put;
			knew.access = 0;
			/* input selector control: reg. 1, bits 0-2.
			 * mis-use 'shift' to pass mixer_ch */
			knew.private_value
				= AK_COMPOSE(idx/2, 1, mixer_ch, 0x07);
			err = snd_ctl_add(ak->card, snd_ctl_new1(&knew, ak));
			if (err < 0)
				return err;
		}

		idx += num_stereo;
		mixer_ch++;
	}
	return 0;
}

static int build_deemphasis(struct snd_akm4xxx *ak, int num_emphs)
{
	int idx, err;
	struct snd_kcontrol_new knew;

	for (idx = 0; idx < num_emphs; idx++) {
		memset(&knew, 0, sizeof(knew));
		knew.name = "Deemphasis";
		knew.index = idx + ak->idx_offset;
		knew.iface = SNDRV_CTL_ELEM_IFACE_MIXER;
		knew.count = 1;
		knew.info = snd_akm4xxx_deemphasis_info;
		knew.get = snd_akm4xxx_deemphasis_get;
		knew.put = snd_akm4xxx_deemphasis_put;
		switch (ak->type) {
		case SND_AK4524:
		case SND_AK4528:
			/* register 3 */
			knew.private_value = AK_COMPOSE(idx, 3, 0, 0);
			break;
		case SND_AK4529: {
			int shift = idx == 3 ? 6 : (2 - idx) * 2;
			/* register 8 with shift */
			knew.private_value = AK_COMPOSE(0, 8, shift, 0);
			break;
		}
		case SND_AK4355:
		case SND_AK4358:
			knew.private_value = AK_COMPOSE(idx, 3, 0, 0);
			break;
		case SND_AK4381:
			knew.private_value = AK_COMPOSE(idx, 1, 1, 0);
			break;
		default:
			return -EINVAL;
		}
		err = snd_ctl_add(ak->card, snd_ctl_new1(&knew, ak));
		if (err < 0)
			return err;
	}
	return 0;
}

int snd_akm4xxx_build_controls(struct snd_akm4xxx *ak)
{
	int err, num_emphs;

	err = build_dac_controls(ak);
	if (err < 0)
		return err;

	err = build_adc_controls(ak);
	if (err < 0)
		return err;

	if (ak->type == SND_AK4355 || ak->type == SND_AK4358)
		num_emphs = 1;
	else
		num_emphs = ak->num_dacs / 2;
	err = build_deemphasis(ak, num_emphs);
	if (err < 0)
		return err;

	return 0;
}
	
EXPORT_SYMBOL(snd_akm4xxx_build_controls);

static int __init alsa_akm4xxx_module_init(void)
{
	return 0;
}
        
static void __exit alsa_akm4xxx_module_exit(void)
{
}
        
module_init(alsa_akm4xxx_module_init)
module_exit(alsa_akm4xxx_module_exit)
