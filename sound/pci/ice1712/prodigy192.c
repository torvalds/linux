/*
 *   ALSA driver for ICEnsemble VT1724 (Envy24HT)
 *
 *   Lowlevel functions for AudioTrak Prodigy 192 cards
 *
 *	Copyright (c) 2003 Takashi Iwai <tiwai@suse.de>
 *      Copyright (c) 2003 Dimitromanolakis Apostolos <apostol@cs.utoronto.ca>
 *      Copyright (c) 2004 Kouichi ONO <co2b@ceres.dti.ne.jp>
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
#include <linux/slab.h>
#include <sound/core.h>

#include "ice1712.h"
#include "envy24ht.h"
#include "prodigy192.h"
#include "stac946x.h"

static inline void stac9460_put(ice1712_t *ice, int reg, unsigned char val)
{
	snd_vt1724_write_i2c(ice, PRODIGY192_STAC9460_ADDR, reg, val);
}

static inline unsigned char stac9460_get(ice1712_t *ice, int reg)
{
	return snd_vt1724_read_i2c(ice, PRODIGY192_STAC9460_ADDR, reg);
}

/*
 * DAC mute control
 */
static int stac9460_dac_mute_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;
	return 0;
}

static int stac9460_dac_mute_get(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	ice1712_t *ice = snd_kcontrol_chip(kcontrol);
	unsigned char val;
	int idx;

	if (kcontrol->private_value)
		idx = STAC946X_MASTER_VOLUME;
	else
		idx  = snd_ctl_get_ioffidx(kcontrol, &ucontrol->id) + STAC946X_LF_VOLUME;
	val = stac9460_get(ice, idx);
	ucontrol->value.integer.value[0] = (~val >> 7) & 0x1;
	return 0;
}

static int stac9460_dac_mute_put(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	ice1712_t *ice = snd_kcontrol_chip(kcontrol);
	unsigned char new, old;
	int idx;
	int change;

	if (kcontrol->private_value)
		idx = STAC946X_MASTER_VOLUME;
	else
		idx  = snd_ctl_get_ioffidx(kcontrol, &ucontrol->id) + STAC946X_LF_VOLUME;
	old = stac9460_get(ice, idx);
	new = (~ucontrol->value.integer.value[0]<< 7 & 0x80) | (old & ~0x80);
	change = (new != old);
	if (change)
		stac9460_put(ice, idx, new);

	return change;
}

/*
 * DAC volume attenuation mixer control
 */
static int stac9460_dac_vol_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;			/* mute */
	uinfo->value.integer.max = 0x7f;		/* 0dB */
	return 0;
}

static int stac9460_dac_vol_get(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	ice1712_t *ice = snd_kcontrol_chip(kcontrol);
	int idx;
	unsigned char vol;

	if (kcontrol->private_value)
		idx = STAC946X_MASTER_VOLUME;
	else
		idx  = snd_ctl_get_ioffidx(kcontrol, &ucontrol->id) + STAC946X_LF_VOLUME;
	vol = stac9460_get(ice, idx) & 0x7f;
	ucontrol->value.integer.value[0] = 0x7f - vol;

	return 0;
}

static int stac9460_dac_vol_put(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	ice1712_t *ice = snd_kcontrol_chip(kcontrol);
	int idx;
	unsigned char tmp, ovol, nvol;
	int change;

	if (kcontrol->private_value)
		idx = STAC946X_MASTER_VOLUME;
	else
		idx  = snd_ctl_get_ioffidx(kcontrol, &ucontrol->id) + STAC946X_LF_VOLUME;
	nvol = ucontrol->value.integer.value[0];
	tmp = stac9460_get(ice, idx);
	ovol = 0x7f - (tmp & 0x7f);
	change = (ovol != nvol);
	if (change) {
		stac9460_put(ice, idx, (0x7f - nvol) | (tmp & 0x80));
	}
	return change;
}

/*
 * ADC mute control
 */
static int stac9460_adc_mute_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t * uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;
	return 0;
}

static int stac9460_adc_mute_get(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	ice1712_t *ice = snd_kcontrol_chip(kcontrol);
	unsigned char val;
	int i;

	for (i = 0; i < 2; ++i) {
		val = stac9460_get(ice, STAC946X_MIC_L_VOLUME + i);
		ucontrol->value.integer.value[i] = ~val>>7 & 0x1;
	}

	return 0;
}

static int stac9460_adc_mute_put(snd_kcontrol_t * kcontrol, snd_ctl_elem_value_t * ucontrol)
{
	ice1712_t *ice = snd_kcontrol_chip(kcontrol);
	unsigned char new, old;
	int i, reg;
	int change;

	for (i = 0; i < 2; ++i) {
		reg = STAC946X_MIC_L_VOLUME + i;
		old = stac9460_get(ice, reg);
		new = (~ucontrol->value.integer.value[i]<<7&0x80) | (old&~0x80);
		change = (new != old);
		if (change)
			stac9460_put(ice, reg, new);
	}

	return change;
}

/*
 * ADC gain mixer control
 */
static int stac9460_adc_vol_info(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;		/* 0dB */
	uinfo->value.integer.max = 0x0f;	/* 22.5dB */
	return 0;
}

static int stac9460_adc_vol_get(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	ice1712_t *ice = snd_kcontrol_chip(kcontrol);
	int i, reg;
	unsigned char vol;

	for (i = 0; i < 2; ++i) {
		reg = STAC946X_MIC_L_VOLUME + i;
		vol = stac9460_get(ice, reg) & 0x0f;
		ucontrol->value.integer.value[i] = 0x0f - vol;
	}

	return 0;
}

static int stac9460_adc_vol_put(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	ice1712_t *ice = snd_kcontrol_chip(kcontrol);
	int i, reg;
	unsigned char ovol, nvol;
	int change;

	for (i = 0; i < 2; ++i) {
		reg = STAC946X_MIC_L_VOLUME + i;
		nvol = ucontrol->value.integer.value[i];
		ovol = 0x0f - stac9460_get(ice, reg);
		change = ((ovol & 0x0f)  != nvol);
		if (change)
			stac9460_put(ice, reg, (0x0f - nvol) | (ovol & ~0x0f));
	}

	return change;
}

#if 0
/*
 * Headphone Amplifier
 */
static int aureon_set_headphone_amp(ice1712_t *ice, int enable)
{
	unsigned int tmp, tmp2;

	tmp2 = tmp = snd_ice1712_gpio_read(ice);
	if (enable)
		tmp |= AUREON_HP_SEL;
	else
		tmp &= ~ AUREON_HP_SEL;
	if (tmp != tmp2) {
		snd_ice1712_gpio_write(ice, tmp);
		return 1;
	}
	return 0;
}

static int aureon_get_headphone_amp(ice1712_t *ice)
{
	unsigned int tmp = snd_ice1712_gpio_read(ice);

	return ( tmp & AUREON_HP_SEL )!= 0;
}

static int aureon_bool_info(snd_kcontrol_t *k, snd_ctl_elem_info_t *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;
	return 0;
}

static int aureon_hpamp_get(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	ice1712_t *ice = snd_kcontrol_chip(kcontrol);

	ucontrol->value.integer.value[0] = aureon_get_headphone_amp(ice);
	return 0;
}


static int aureon_hpamp_put(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	ice1712_t *ice = snd_kcontrol_chip(kcontrol);

	return aureon_set_headphone_amp(ice,ucontrol->value.integer.value[0]);
}

/*
 * Deemphasis
 */
static int aureon_deemp_get(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	ice1712_t *ice = snd_kcontrol_chip(kcontrol);
	ucontrol->value.integer.value[0] = (wm_get(ice, WM_DAC_CTRL2) & 0xf) == 0xf;
	return 0;
}

static int aureon_deemp_put(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	ice1712_t *ice = snd_kcontrol_chip(kcontrol);
	int temp, temp2;
	temp2 = temp = wm_get(ice, WM_DAC_CTRL2);
	if (ucontrol->value.integer.value[0])
		temp |= 0xf;
	else
		temp &= ~0xf;
	if (temp != temp2) {
		wm_put(ice, WM_DAC_CTRL2, temp);
		return 1;
	}
	return 0;
}

/*
 * ADC Oversampling
 */
static int aureon_oversampling_info(snd_kcontrol_t *k, snd_ctl_elem_info_t *uinfo)
{
	static char *texts[2] = { "128x", "64x"	};

	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 2;

	if (uinfo->value.enumerated.item >= uinfo->value.enumerated.items)
		uinfo->value.enumerated.item = uinfo->value.enumerated.items - 1;
	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);

        return 0;
}

static int aureon_oversampling_get(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	ice1712_t *ice = snd_kcontrol_chip(kcontrol);
	ucontrol->value.enumerated.item[0] = (wm_get(ice, WM_MASTER) & 0x8) == 0x8;
	return 0;
}

static int aureon_oversampling_put(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	int temp, temp2;
	ice1712_t *ice = snd_kcontrol_chip(kcontrol);

	temp2 = temp = wm_get(ice, WM_MASTER);

	if (ucontrol->value.enumerated.item[0])
		temp |= 0x8;
	else
		temp &= ~0x8;

	if (temp != temp2) {
		wm_put(ice, WM_MASTER, temp);
		return 1;
	}
	return 0;
}
#endif

/*
 * mixers
 */

static snd_kcontrol_new_t stac_controls[] __devinitdata = {
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Master Playback Switch",
		.info = stac9460_dac_mute_info,
		.get = stac9460_dac_mute_get,
		.put = stac9460_dac_mute_put,
		.private_value = 1,
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Master Playback Volume",
		.info = stac9460_dac_vol_info,
		.get = stac9460_dac_vol_get,
		.put = stac9460_dac_vol_put,
		.private_value = 1,
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "DAC Switch",
		.count = 6,
		.info = stac9460_dac_mute_info,
		.get = stac9460_dac_mute_get,
		.put = stac9460_dac_mute_put,
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "DAC Volume",
		.count = 6,
		.info = stac9460_dac_vol_info,
		.get = stac9460_dac_vol_get,
		.put = stac9460_dac_vol_put,
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "ADC Switch",
		.count = 1,
		.info = stac9460_adc_mute_info,
		.get = stac9460_adc_mute_get,
		.put = stac9460_adc_mute_put,

	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "ADC Volume",
		.count = 1,
		.info = stac9460_adc_vol_info,
		.get = stac9460_adc_vol_get,
		.put = stac9460_adc_vol_put,
	},
#if 0
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Capture Route",
		.info = wm_adc_mux_info,
		.get = wm_adc_mux_get,
		.put = wm_adc_mux_put,
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Headphone Amplifier Switch",
		.info = aureon_bool_info,
		.get = aureon_hpamp_get,
		.put = aureon_hpamp_put
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "DAC Deemphasis Switch",
		.info = aureon_bool_info,
		.get = aureon_deemp_get,
		.put = aureon_deemp_put
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "ADC Oversampling",
		.info = aureon_oversampling_info,
		.get = aureon_oversampling_get,
		.put = aureon_oversampling_put
	},
#endif
};

static int __devinit prodigy192_add_controls(ice1712_t *ice)
{
	unsigned int i;
	int err;

	for (i = 0; i < ARRAY_SIZE(stac_controls); i++) {
		err = snd_ctl_add(ice->card, snd_ctl_new1(&stac_controls[i], ice));
		if (err < 0)
			return err;
	}
	return 0;
}


/*
 * initialize the chip
 */
static int __devinit prodigy192_init(ice1712_t *ice)
{
	static unsigned short stac_inits_prodigy[] = {
		STAC946X_RESET, 0,
/*		STAC946X_MASTER_VOLUME, 0,
		STAC946X_LF_VOLUME, 0,
		STAC946X_RF_VOLUME, 0,
		STAC946X_LR_VOLUME, 0,
		STAC946X_RR_VOLUME, 0,
		STAC946X_CENTER_VOLUME, 0,
		STAC946X_LFE_VOLUME, 0,*/
		(unsigned short)-1
	};
	unsigned short *p;

	/* prodigy 192 */
	ice->num_total_dacs = 6;
	ice->num_total_adcs = 2;
	
	/* initialize codec */
	p = stac_inits_prodigy;
	for (; *p != (unsigned short)-1; p += 2)
		stac9460_put(ice, p[0], p[1]);

	return 0;
}


/*
 * Aureon boards don't provide the EEPROM data except for the vendor IDs.
 * hence the driver needs to sets up it properly.
 */

static unsigned char prodigy71_eeprom[] __devinitdata = {
	0x2b,	/* SYSCONF: clock 512, mpu401, spdif-in/ADC, 4DACs */
	0x80,	/* ACLINK: I2S */
	0xf8,	/* I2S: vol, 96k, 24bit, 192k */
	0xc3,	/* SPDIF: out-en, out-int, spdif-in */
	0xff,	/* GPIO_DIR */
	0xff,	/* GPIO_DIR1 */
	0xbf,	/* GPIO_DIR2 */
	0x00,	/* GPIO_MASK */
	0x00,	/* GPIO_MASK1 */
	0x00,	/* GPIO_MASK2 */
	0x00,	/* GPIO_STATE */
	0x00,	/* GPIO_STATE1 */
	0x00,	/* GPIO_STATE2 */
};


/* entry point */
struct snd_ice1712_card_info snd_vt1724_prodigy192_cards[] __devinitdata = {
	{
		.subvendor = VT1724_SUBDEVICE_PRODIGY192VE,
		.name = "Audiotrak Prodigy 192",
		.model = "prodigy192",
		.chip_init = prodigy192_init,
		.build_controls = prodigy192_add_controls,
		.eeprom_size = sizeof(prodigy71_eeprom),
		.eeprom_data = prodigy71_eeprom,
	},
	{ } /* terminator */
};
