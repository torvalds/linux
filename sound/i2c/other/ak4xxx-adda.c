/*
 *   ALSA driver for AK4524 / AK4528 / AK4529 / AK4355 / AK4358 / AK4381
 *   AD and DA converters
 *
 *	Copyright (c) 2000-2004 Jaroslav Kysela <perex@suse.cz>,
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

#include <sound/driver.h>
#include <asm/io.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <sound/core.h>
#include <sound/control.h>
#include <sound/ak4xxx-adda.h>

MODULE_AUTHOR("Jaroslav Kysela <perex@suse.cz>, Takashi Iwai <tiwai@suse.de>");
MODULE_DESCRIPTION("Routines for control of AK452x / AK43xx  AD/DA converters");
MODULE_LICENSE("GPL");

void snd_akm4xxx_write(akm4xxx_t *ak, int chip, unsigned char reg, unsigned char val)
{
	ak->ops.lock(ak, chip);
	ak->ops.write(ak, chip, reg, val);

	/* save the data */
	if (ak->type == SND_AK4524 || ak->type == SND_AK4528) {
		if ((reg != 0x04 && reg != 0x05) || (val & 0x80) == 0)
			snd_akm4xxx_set(ak, chip, reg, val);
		else
			snd_akm4xxx_set_ipga(ak, chip, reg, val);
	} else {
		/* AK4529, or else */
		snd_akm4xxx_set(ak, chip, reg, val);
	}
	ak->ops.unlock(ak, chip);
}

/*
 * reset the AKM codecs
 * @state: 1 = reset codec, 0 = restore the registers
 *
 * assert the reset operation and restores the register values to the chips.
 */
void snd_akm4xxx_reset(akm4xxx_t *ak, int state)
{
	unsigned int chip;
	unsigned char reg;
	
	switch (ak->type) {
	case SND_AK4524:
	case SND_AK4528:
		for (chip = 0; chip < ak->num_dacs/2; chip++) {
			snd_akm4xxx_write(ak, chip, 0x01, state ? 0x00 : 0x03);
			if (state)
				continue;
			/* DAC volumes */
			for (reg = 0x04; reg < (ak->type == SND_AK4528 ? 0x06 : 0x08); reg++)
				snd_akm4xxx_write(ak, chip, reg, snd_akm4xxx_get(ak, chip, reg));
			if (ak->type == SND_AK4528)
				continue;
			/* IPGA */
			for (reg = 0x04; reg < 0x06; reg++)
				snd_akm4xxx_write(ak, chip, reg, snd_akm4xxx_get_ipga(ak, chip, reg));
		}
		break;
	case SND_AK4529:
		/* FIXME: needed for ak4529? */
		break;
	case SND_AK4355:
	case SND_AK4358:
		if (state) {
			snd_akm4xxx_write(ak, 0, 0x01, 0x02); /* reset and soft-mute */
			return;
		}
		for (reg = 0x00; reg < 0x0b; reg++)
			if (reg != 0x01)
				snd_akm4xxx_write(ak, 0, reg, snd_akm4xxx_get(ak, 0, reg));
		snd_akm4xxx_write(ak, 0, 0x01, 0x01); /* un-reset, unmute */
		break;
	case SND_AK4381:
		for (chip = 0; chip < ak->num_dacs/2; chip++) {
			snd_akm4xxx_write(ak, chip, 0x00, state ? 0x0c : 0x0f);
			if (state)
				continue;
			for (reg = 0x01; reg < 0x05; reg++)
				snd_akm4xxx_write(ak, chip, reg, snd_akm4xxx_get(ak, chip, reg));
		}
		break;
	}
}

/*
 * initialize all the ak4xxx chips
 */
void snd_akm4xxx_init(akm4xxx_t *ak)
{
	static unsigned char inits_ak4524[] = {
		0x00, 0x07, /* 0: all power up */
		0x01, 0x00, /* 1: ADC/DAC reset */
		0x02, 0x60, /* 2: 24bit I2S */
		0x03, 0x19, /* 3: deemphasis off */
		0x01, 0x03, /* 1: ADC/DAC enable */
		0x04, 0x00, /* 4: ADC left muted */
		0x05, 0x00, /* 5: ADC right muted */
		0x04, 0x80, /* 4: ADC IPGA gain 0dB */
		0x05, 0x80, /* 5: ADC IPGA gain 0dB */
		0x06, 0x00, /* 6: DAC left muted */
		0x07, 0x00, /* 7: DAC right muted */
		0xff, 0xff
	};
	static unsigned char inits_ak4528[] = {
		0x00, 0x07, /* 0: all power up */
		0x01, 0x00, /* 1: ADC/DAC reset */
		0x02, 0x60, /* 2: 24bit I2S */
		0x03, 0x0d, /* 3: deemphasis off, turn LR highpass filters on */
		0x01, 0x03, /* 1: ADC/DAC enable */
		0x04, 0x00, /* 4: ADC left muted */
		0x05, 0x00, /* 5: ADC right muted */
		0xff, 0xff
	};
	static unsigned char inits_ak4529[] = {
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
	static unsigned char inits_ak4355[] = {
		0x01, 0x02, /* 1: reset and soft-mute */
		0x00, 0x06, /* 0: mode3(i2s), disable auto-clock detect, disable DZF, sharp roll-off, RSTN#=0 */
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
	static unsigned char inits_ak4358[] = {
		0x01, 0x02, /* 1: reset and soft-mute */
		0x00, 0x06, /* 0: mode3(i2s), disable auto-clock detect, disable DZF, sharp roll-off, RSTN#=0 */
		0x02, 0x0e, /* 2: DA's power up, normal speed, RSTN#=0 */
		// 0x02, 0x2e, /* quad speed */
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
	static unsigned char inits_ak4381[] = {
		0x00, 0x0c, /* 0: mode3(i2s), disable auto-clock detect */
		0x01, 0x02, /* 1: de-emphasis off, normal speed, sharp roll-off, DZF off */
		// 0x01, 0x12, /* quad speed */
		0x02, 0x00, /* 2: DZF disabled */
		0x03, 0x00, /* 3: LATT 0 */
		0x04, 0x00, /* 4: RATT 0 */
		0x00, 0x0f, /* 0: power-up, un-reset */
		0xff, 0xff
	};

	int chip, num_chips;
	unsigned char *ptr, reg, data, *inits;

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

#define AK_GET_CHIP(val)		(((val) >> 8) & 0xff)
#define AK_GET_ADDR(val)		((val) & 0xff)
#define AK_GET_SHIFT(val)		(((val) >> 16) & 0x7f)
#define AK_GET_INVERT(val)		(((val) >> 23) & 1)
#define AK_GET_MASK(val)		(((val) >> 24) & 0xff)
#define AK_COMPOSE(chip,addr,shift,mask) (((chip) << 8) | (addr) | ((shift) << 16) | ((mask) << 24))
#define AK_INVERT 			(1<<23)

static int snd_akm4xxx_volume_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	unsigned int mask = AK_GET_MASK(kcontrol->private_value);

	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = mask;
	return 0;
}

static int snd_akm4xxx_volume_get(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	akm4xxx_t *ak = snd_kcontrol_chip(kcontrol);
	int chip = AK_GET_CHIP(kcontrol->private_value);
	int addr = AK_GET_ADDR(kcontrol->private_value);
	int invert = AK_GET_INVERT(kcontrol->private_value);
	unsigned int mask = AK_GET_MASK(kcontrol->private_value);
	unsigned char val = snd_akm4xxx_get(ak, chip, addr);
	
	ucontrol->value.integer.value[0] = invert ? mask - val : val;
	return 0;
}

static int snd_akm4xxx_volume_put(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	akm4xxx_t *ak = snd_kcontrol_chip(kcontrol);
	int chip = AK_GET_CHIP(kcontrol->private_value);
	int addr = AK_GET_ADDR(kcontrol->private_value);
	int invert = AK_GET_INVERT(kcontrol->private_value);
	unsigned int mask = AK_GET_MASK(kcontrol->private_value);
	unsigned char nval = ucontrol->value.integer.value[0] % (mask+1);
	int change;

	if (invert)
		nval = mask - nval;
	change = snd_akm4xxx_get(ak, chip, addr) != nval;
	if (change)
		snd_akm4xxx_write(ak, chip, addr, nval);
	return change;
}

static int snd_akm4xxx_ipga_gain_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 36;
	return 0;
}

static int snd_akm4xxx_ipga_gain_get(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	akm4xxx_t *ak = snd_kcontrol_chip(kcontrol);
	int chip = AK_GET_CHIP(kcontrol->private_value);
	int addr = AK_GET_ADDR(kcontrol->private_value);
	ucontrol->value.integer.value[0] = snd_akm4xxx_get_ipga(ak, chip, addr) & 0x7f;
	return 0;
}

static int snd_akm4xxx_ipga_gain_put(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	akm4xxx_t *ak = snd_kcontrol_chip(kcontrol);
	int chip = AK_GET_CHIP(kcontrol->private_value);
	int addr = AK_GET_ADDR(kcontrol->private_value);
	unsigned char nval = (ucontrol->value.integer.value[0] % 37) | 0x80;
	int change = snd_akm4xxx_get_ipga(ak, chip, addr) != nval;
	if (change)
		snd_akm4xxx_write(ak, chip, addr, nval);
	return change;
}

static int snd_akm4xxx_deemphasis_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t *uinfo)
{
	static char *texts[4] = {
		"44.1kHz", "Off", "48kHz", "32kHz",
	};
	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 4;
	if (uinfo->value.enumerated.item >= 4)
		uinfo->value.enumerated.item = 3;
	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}

static int snd_akm4xxx_deemphasis_get(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	akm4xxx_t *ak = snd_kcontrol_chip(kcontrol);
	int chip = AK_GET_CHIP(kcontrol->private_value);
	int addr = AK_GET_ADDR(kcontrol->private_value);
	int shift = AK_GET_SHIFT(kcontrol->private_value);
	ucontrol->value.enumerated.item[0] = (snd_akm4xxx_get(ak, chip, addr) >> shift) & 3;
	return 0;
}

static int snd_akm4xxx_deemphasis_put(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	akm4xxx_t *ak = snd_kcontrol_chip(kcontrol);
	int chip = AK_GET_CHIP(kcontrol->private_value);
	int addr = AK_GET_ADDR(kcontrol->private_value);
	int shift = AK_GET_SHIFT(kcontrol->private_value);
	unsigned char nval = ucontrol->value.enumerated.item[0] & 3;
	int change;
	
	nval = (nval << shift) | (snd_akm4xxx_get(ak, chip, addr) & ~(3 << shift));
	change = snd_akm4xxx_get(ak, chip, addr) != nval;
	if (change)
		snd_akm4xxx_write(ak, chip, addr, nval);
	return change;
}

/*
 * build AK4xxx controls
 */

int snd_akm4xxx_build_controls(akm4xxx_t *ak)
{
	unsigned int idx, num_emphs;
	snd_kcontrol_t *ctl;
	int err;

	ctl = kmalloc(sizeof(*ctl), GFP_KERNEL);
	if (! ctl)
		return -ENOMEM;

	for (idx = 0; idx < ak->num_dacs; ++idx) {
		memset(ctl, 0, sizeof(*ctl));
		strcpy(ctl->id.name, "DAC Volume");
		ctl->id.index = idx + ak->idx_offset * 2;
		ctl->id.iface = SNDRV_CTL_ELEM_IFACE_MIXER;
		ctl->count = 1;
		ctl->info = snd_akm4xxx_volume_info;
		ctl->get = snd_akm4xxx_volume_get;
		ctl->put = snd_akm4xxx_volume_put;
		switch (ak->type) {
		case SND_AK4524:
			ctl->private_value = AK_COMPOSE(idx/2, (idx%2) + 6, 0, 127); /* register 6 & 7 */
			break;
		case SND_AK4528:
			ctl->private_value = AK_COMPOSE(idx/2, (idx%2) + 4, 0, 127); /* register 4 & 5 */
			break;
		case SND_AK4529: {
			int val = idx < 6 ? idx + 2 : (idx - 6) + 0xb; /* registers 2-7 and b,c */
			ctl->private_value = AK_COMPOSE(0, val, 0, 255) | AK_INVERT;
			break;
		}
		case SND_AK4355:
			ctl->private_value = AK_COMPOSE(0, idx + 4, 0, 255); /* register 4-9, chip #0 only */
			break;
		case SND_AK4358:
			if (idx >= 6)
				ctl->private_value = AK_COMPOSE(0, idx + 5, 0, 255); /* register 4-9, chip #0 only */
			else
				ctl->private_value = AK_COMPOSE(0, idx + 4, 0, 255); /* register 4-9, chip #0 only */
			break;
		case SND_AK4381:
			ctl->private_value = AK_COMPOSE(idx/2, (idx%2) + 3, 0, 255); /* register 3 & 4 */
			break;
		default:
			err = -EINVAL;
			goto __error;
		}
		ctl->private_data = ak;
		if ((err = snd_ctl_add(ak->card, snd_ctl_new(ctl, SNDRV_CTL_ELEM_ACCESS_READ|SNDRV_CTL_ELEM_ACCESS_WRITE))) < 0)
			goto __error;
	}
	for (idx = 0; idx < ak->num_adcs && ak->type == SND_AK4524; ++idx) {
		memset(ctl, 0, sizeof(*ctl));
		strcpy(ctl->id.name, "ADC Volume");
		ctl->id.index = idx + ak->idx_offset * 2;
		ctl->id.iface = SNDRV_CTL_ELEM_IFACE_MIXER;
		ctl->count = 1;
		ctl->info = snd_akm4xxx_volume_info;
		ctl->get = snd_akm4xxx_volume_get;
		ctl->put = snd_akm4xxx_volume_put;
		ctl->private_value = AK_COMPOSE(idx/2, (idx%2) + 4, 0, 127); /* register 4 & 5 */
		ctl->private_data = ak;
		if ((err = snd_ctl_add(ak->card, snd_ctl_new(ctl, SNDRV_CTL_ELEM_ACCESS_READ|SNDRV_CTL_ELEM_ACCESS_WRITE))) < 0)
			goto __error;

		memset(ctl, 0, sizeof(*ctl));
		strcpy(ctl->id.name, "IPGA Analog Capture Volume");
		ctl->id.index = idx + ak->idx_offset * 2;
		ctl->id.iface = SNDRV_CTL_ELEM_IFACE_MIXER;
		ctl->count = 1;
		ctl->info = snd_akm4xxx_ipga_gain_info;
		ctl->get = snd_akm4xxx_ipga_gain_get;
		ctl->put = snd_akm4xxx_ipga_gain_put;
		ctl->private_value = AK_COMPOSE(idx/2, (idx%2) + 4, 0, 0); /* register 4 & 5 */
		ctl->private_data = ak;
		if ((err = snd_ctl_add(ak->card, snd_ctl_new(ctl, SNDRV_CTL_ELEM_ACCESS_READ|SNDRV_CTL_ELEM_ACCESS_WRITE))) < 0)
			goto __error;
	}
	if (ak->type == SND_AK4355 || ak->type == SND_AK4358)
		num_emphs = 1;
	else
		num_emphs = ak->num_dacs / 2;
	for (idx = 0; idx < num_emphs; idx++) {
		memset(ctl, 0, sizeof(*ctl));
		strcpy(ctl->id.name, "Deemphasis");
		ctl->id.index = idx + ak->idx_offset;
		ctl->id.iface = SNDRV_CTL_ELEM_IFACE_MIXER;
		ctl->count = 1;
		ctl->info = snd_akm4xxx_deemphasis_info;
		ctl->get = snd_akm4xxx_deemphasis_get;
		ctl->put = snd_akm4xxx_deemphasis_put;
		switch (ak->type) {
		case SND_AK4524:
		case SND_AK4528:
			ctl->private_value = AK_COMPOSE(idx, 3, 0, 0); /* register 3 */
			break;
		case SND_AK4529: {
			int shift = idx == 3 ? 6 : (2 - idx) * 2;
			ctl->private_value = AK_COMPOSE(0, 8, shift, 0); /* register 8 with shift */
			break;
		}
		case SND_AK4355:
		case SND_AK4358:
			ctl->private_value = AK_COMPOSE(idx, 3, 0, 0);
			break;
		case SND_AK4381:
			ctl->private_value = AK_COMPOSE(idx, 1, 1, 0);
			break;
		}
		ctl->private_data = ak;
		if ((err = snd_ctl_add(ak->card, snd_ctl_new(ctl, SNDRV_CTL_ELEM_ACCESS_READ|SNDRV_CTL_ELEM_ACCESS_WRITE))) < 0)
			goto __error;
	}
	err = 0;

 __error:
	kfree(ctl);
	return err;
}

static int __init alsa_akm4xxx_module_init(void)
{
	return 0;
}
        
static void __exit alsa_akm4xxx_module_exit(void)
{
}
        
module_init(alsa_akm4xxx_module_init)
module_exit(alsa_akm4xxx_module_exit)

EXPORT_SYMBOL(snd_akm4xxx_write);
EXPORT_SYMBOL(snd_akm4xxx_reset);
EXPORT_SYMBOL(snd_akm4xxx_init);
EXPORT_SYMBOL(snd_akm4xxx_build_controls);
