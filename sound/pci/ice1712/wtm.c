/*
 *	ALSA driver for ICEnsemble VT1724 (Envy24HT)
 *	
 *	Lowlevel functions for Ego Sys Waveterminal 192M
 *
 *		Copyright (c) 2006 Guedez Clement <klem.dev@gmail.com>
 *		Some functions are taken from the Prodigy192 driver
 *		source
 *		
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	This program is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with this program; if not, write to the Free Software
 *	Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
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
#include "wtm.h"
#include "stac946x.h"


/*
 *	2*ADC 6*DAC no1 ringbuffer r/w on i2c bus 
 */
static inline void stac9460_put(struct snd_ice1712 *ice, int reg, 
						unsigned char val)
{
	snd_vt1724_write_i2c(ice, STAC9460_I2C_ADDR, reg, val);
}

static inline unsigned char stac9460_get(struct snd_ice1712 *ice, int reg)
{
	return snd_vt1724_read_i2c(ice, STAC9460_I2C_ADDR, reg);
}

/*
 *	2*ADC 2*DAC no2 ringbuffer r/w on i2c bus
 */
static inline void stac9460_2_put(struct snd_ice1712 *ice, int reg,
						unsigned char val)
{
	snd_vt1724_write_i2c(ice, STAC9460_2_I2C_ADDR, reg, val);
}

static inline unsigned char stac9460_2_get(struct snd_ice1712 *ice, int reg)
{
	return snd_vt1724_read_i2c(ice, STAC9460_2_I2C_ADDR, reg);
}


/*
 *	DAC mute control
 */
static int stac9460_dac_mute_info(struct snd_kcontrol *kcontrol,
	       			struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	return 0;
}

static int stac9460_dac_mute_get(struct snd_kcontrol *kcontrol,
	       			struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	unsigned char val;
	int idx, id;

	if (kcontrol->private_value) {
		idx = STAC946X_MASTER_VOLUME;
		id = 0;
	} else {
		id = snd_ctl_get_ioffidx(kcontrol, &ucontrol->id);
		idx = id + STAC946X_LF_VOLUME;
	}
	if (id < 6)
		val = stac9460_get(ice, idx);
	else 
		val = stac9460_2_get(ice,idx - 6);
	ucontrol->value.integer.value[0] = (~val >> 7) & 0x1;
	return 0;
}

static int stac9460_dac_mute_put(struct snd_kcontrol *kcontrol,
	       			struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	unsigned char new, old;
	int id, idx;
	int change;

	if (kcontrol->private_value) {
		idx = STAC946X_MASTER_VOLUME;
		old = stac9460_get(ice, idx);
		new = (~ucontrol->value.integer.value[0]<< 7 & 0x80) |
		       					(old & ~0x80);
		change = (new != old);
		if (change) {
			stac9460_put(ice, idx, new);
			stac9460_2_put(ice, idx, new);
		}
	} else {
		id = snd_ctl_get_ioffidx(kcontrol, &ucontrol->id);
		idx = id + STAC946X_LF_VOLUME;
		if (id < 6)
			old = stac9460_get(ice, idx);
		else 
			old = stac9460_2_get(ice, idx - 6);
		new = (~ucontrol->value.integer.value[0]<< 7 & 0x80) |
							(old & ~0x80);
		change = (new != old);
		if (change) {
			if (id < 6)
			       	stac9460_put(ice, idx, new);
			else
			       	stac9460_2_put(ice, idx - 6, new);
		}
	}
	return change;
}

/*
 * 	DAC volume attenuation mixer control
 */
static int stac9460_dac_vol_info(struct snd_kcontrol *kcontrol,
	       			struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;			/* mute */
	uinfo->value.integer.max = 0x7f;		/* 0dB */
	return 0;
}

static int stac9460_dac_vol_get(struct snd_kcontrol *kcontrol,
	       			struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	int idx, id;
	unsigned char vol;

	if (kcontrol->private_value) {
		idx = STAC946X_MASTER_VOLUME;
		id = 0;
	} else {
		id = snd_ctl_get_ioffidx(kcontrol, &ucontrol->id);
		idx = id + STAC946X_LF_VOLUME;
	}
	if (id < 6)
		vol = stac9460_get(ice, idx) & 0x7f;
	else 
		vol = stac9460_2_get(ice, idx - 6) & 0x7f;
	ucontrol->value.integer.value[0] = 0x7f - vol;
	return 0;
}

static int stac9460_dac_vol_put(struct snd_kcontrol *kcontrol,
	       			struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	int idx, id;
	unsigned char tmp, ovol, nvol;
	int change;

	if (kcontrol->private_value) {
		idx = STAC946X_MASTER_VOLUME;
		nvol = ucontrol->value.integer.value[0];
		tmp = stac9460_get(ice, idx);
		ovol = 0x7f - (tmp & 0x7f);
		change = (ovol != nvol);
		if (change) {
			 stac9460_put(ice, idx, (0x7f - nvol) | (tmp & 0x80));
			 stac9460_2_put(ice, idx, (0x7f - nvol) | (tmp & 0x80));
		}
	} else {
		id = snd_ctl_get_ioffidx(kcontrol, &ucontrol->id);
		idx = id + STAC946X_LF_VOLUME;
		nvol = ucontrol->value.integer.value[0];
		if (id < 6)
			tmp = stac9460_get(ice, idx);
		else 
			tmp = stac9460_2_get(ice, idx - 6);
		ovol = 0x7f - (tmp & 0x7f);
		change = (ovol != nvol);
		if (change) {
			if (id < 6)
				stac9460_put(ice, idx, (0x7f - nvol) |
					       		(tmp & 0x80));
			else 
				stac9460_2_put(ice, idx-6, (0x7f - nvol) |
					       			(tmp & 0x80));
		}
	}
	return change;
}

/*
 * ADC mute control
 */
static int stac9460_adc_mute_info(struct snd_kcontrol *kcontrol,
	       			struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;
	return 0;
}

static int stac9460_adc_mute_get(struct snd_kcontrol *kcontrol,
	       			struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	unsigned char val;
	int i, id;
	
	id = snd_ctl_get_ioffidx(kcontrol, &ucontrol->id);
	if (id == 0) {
		for (i = 0; i < 2; ++i) {
			val = stac9460_get(ice, STAC946X_MIC_L_VOLUME + i);
			ucontrol->value.integer.value[i] = ~val>>7 & 0x1;
		}
	} else {
		for (i = 0; i < 2; ++i) {
			val = stac9460_2_get(ice, STAC946X_MIC_L_VOLUME + i);
			ucontrol->value.integer.value[i] = ~val>>7 & 0x1;
		}
	}
	return 0;
}

static int stac9460_adc_mute_put(struct snd_kcontrol *kcontrol,
	       			struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	unsigned char new, old;
	int i, reg, id;
	int change;
	
	id = snd_ctl_get_ioffidx(kcontrol, &ucontrol->id);
	if (id == 0) {
		for (i = 0; i < 2; ++i) {
			reg = STAC946X_MIC_L_VOLUME + i;
			old = stac9460_get(ice, reg);
			new = (~ucontrol->value.integer.value[i]<<7&0x80) |
			       					(old&~0x80);
			change = (new != old);
			if (change)
				stac9460_put(ice, reg, new);
		}
	} else {
		for (i = 0; i < 2; ++i) {
			reg = STAC946X_MIC_L_VOLUME + i;
			old = stac9460_2_get(ice, reg);
			new = (~ucontrol->value.integer.value[i]<<7&0x80) |
			       					(old&~0x80);
			change = (new != old);
			if (change)
				stac9460_2_put(ice, reg, new);
		}
	}
	return change;
}

/*
 *ADC gain mixer control
 */
static int stac9460_adc_vol_info(struct snd_kcontrol *kcontrol,
	       			struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;		/* 0dB */
	uinfo->value.integer.max = 0x0f;	/* 22.5dB */
	return 0;
}

static int stac9460_adc_vol_get(struct snd_kcontrol *kcontrol,
	       			struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	int i, reg, id;
	unsigned char vol;
	
	id = snd_ctl_get_ioffidx(kcontrol, &ucontrol->id);
	if (id == 0) {
		for (i = 0; i < 2; ++i) {
			reg = STAC946X_MIC_L_VOLUME + i;
			vol = stac9460_get(ice, reg) & 0x0f;
			ucontrol->value.integer.value[i] = 0x0f - vol;
		}
	} else {
		for (i = 0; i < 2; ++i) {
			reg = STAC946X_MIC_L_VOLUME + i;
			vol = stac9460_2_get(ice, reg) & 0x0f;
			ucontrol->value.integer.value[i] = 0x0f - vol;
		}
	}
	return 0;
}

static int stac9460_adc_vol_put(struct snd_kcontrol *kcontrol,
	       		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	int i, reg, id;
	unsigned char ovol, nvol;
	int change;
	
	id = snd_ctl_get_ioffidx(kcontrol, &ucontrol->id);
	if (id == 0) {
		for (i = 0; i < 2; ++i) {
			reg = STAC946X_MIC_L_VOLUME + i;
			nvol = ucontrol->value.integer.value[i];
			ovol = 0x0f - stac9460_get(ice, reg);
			change = ((ovol & 0x0f) != nvol);
			if (change)
				stac9460_put(ice, reg, (0x0f - nvol) |
					       		(ovol & ~0x0f));
		}
	} else {
		for (i = 0; i < 2; ++i) {
			reg = STAC946X_MIC_L_VOLUME + i;
			nvol = ucontrol->value.integer.value[i];
			ovol = 0x0f - stac9460_2_get(ice, reg);
			change = ((ovol & 0x0f) != nvol);
			if (change)
				stac9460_2_put(ice, reg, (0x0f - nvol) |
					       		(ovol & ~0x0f));
		}
	}
	return change;
}

/*
 * MIC / LINE switch fonction
 */

static int stac9460_mic_sw_info(struct snd_kcontrol *kcontrol,
	       			struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;
	return 0;
}

static int stac9460_mic_sw_get(struct snd_kcontrol *kcontrol,
	       		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	unsigned char val;
	int id;
		
	id = snd_ctl_get_ioffidx(kcontrol, &ucontrol->id);
	if (id == 0)
	       	val = stac9460_get(ice, STAC946X_GENERAL_PURPOSE);
	else
	       	val = stac9460_2_get(ice, STAC946X_GENERAL_PURPOSE);
	ucontrol->value.integer.value[0] = ~val>>7 & 0x1;
	return 0;
}

static int stac9460_mic_sw_put(struct snd_kcontrol *kcontrol,
	       		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	unsigned char new, old;
	int change, id;

	id = snd_ctl_get_ioffidx(kcontrol, &ucontrol->id);
	if (id == 0)
	       	old = stac9460_get(ice, STAC946X_GENERAL_PURPOSE);
	else
	       	old = stac9460_2_get(ice, STAC946X_GENERAL_PURPOSE);
	new = (~ucontrol->value.integer.value[0]<< 7 & 0x80) | (old & ~0x80);
	change = (new != old);
	if (change) {
		if (id == 0)
		       	stac9460_put(ice, STAC946X_GENERAL_PURPOSE, new);
		else
		       	stac9460_2_put(ice, STAC946X_GENERAL_PURPOSE, new);
	}
	return change;
}

/*
 * Control tabs
 */
static struct snd_kcontrol_new stac9640_controls[] __devinitdata = {
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Master Playback Switch",
		.info = stac9460_dac_mute_info,
		.get = stac9460_dac_mute_get,
		.put = stac9460_dac_mute_put,
		.private_value = 1
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
		.name = "MIC/Line switch",
		.count = 2,
		.info = stac9460_mic_sw_info,
		.get = stac9460_mic_sw_get,
		.put = stac9460_mic_sw_put,

	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "DAC Switch",
		.count = 8,
		.info = stac9460_dac_mute_info,
		.get = stac9460_dac_mute_get,
		.put = stac9460_dac_mute_put,
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "DAC Volume",
		.count = 8,
		.info = stac9460_dac_vol_info,
		.get = stac9460_dac_vol_get,
		.put = stac9460_dac_vol_put,
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "ADC Switch",
		.count = 2,
		.info = stac9460_adc_mute_info,
		.get = stac9460_adc_mute_get,
		.put = stac9460_adc_mute_put,
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "ADC Volume",
		.count = 2,
		.info = stac9460_adc_vol_info,
		.get = stac9460_adc_vol_get,
		.put = stac9460_adc_vol_put,

	}	
};



/*INIT*/
static int __devinit wtm_add_controls(struct snd_ice1712 *ice)
{
	unsigned int i;
	int err;

	for (i = 0; i < ARRAY_SIZE(stac9640_controls); i++) {
		err = snd_ctl_add(ice->card,
				snd_ctl_new1(&stac9640_controls[i], ice));
		if (err < 0)
			return err;
	}
	return 0;
}

static int __devinit wtm_init(struct snd_ice1712 *ice)
{
	static unsigned short stac_inits_prodigy[] = {
		STAC946X_RESET, 0,
		(unsigned short)-1
	};
	unsigned short *p;
		
	/*WTM 192M*/
	ice->num_total_dacs = 8;
	ice->num_total_adcs = 4;
	ice->force_rdma1 = 1;

	/*initialize codec*/
	p = stac_inits_prodigy;
	for (; *p != (unsigned short)-1; p += 2) {
		stac9460_put(ice, p[0], p[1]);
		stac9460_2_put(ice, p[0], p[1]);
	}
	return 0;
}


static unsigned char wtm_eeprom[] __devinitdata = {
	0x47,	/*SYSCONF: clock 192KHz, 4ADC, 8DAC */
	0x80,	/* ACLINK : I2S */
	0xf8,	/* I2S: vol; 96k, 24bit, 192k */
	0xc1	/*SPDIF: out-en, spidf ext out*/,
	0x9f,	/* GPIO_DIR */
	0xff,	/* GPIO_DIR1 */
	0x7f,	/* GPIO_DIR2 */
	0x9f,	/* GPIO_MASK */
	0xff,	/* GPIO_MASK1 */
	0x7f,	/* GPIO_MASK2 */
	0x16,	/* GPIO_STATE */
	0x80,	/* GPIO_STATE1 */
	0x00,	/* GPIO_STATE2 */
};


/*entry point*/
struct snd_ice1712_card_info snd_vt1724_wtm_cards[] __devinitdata = {
	{
		.subvendor = VT1724_SUBDEVICE_WTM,
		.name = "ESI Waveterminal 192M",
		.model = "WT192M",
		.chip_init = wtm_init,
		.build_controls = wtm_add_controls,
		.eeprom_size = sizeof(wtm_eeprom),
		.eeprom_data = wtm_eeprom,
	},
	{} /*terminator*/
};
