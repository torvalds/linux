/*
 *   ALSA driver for ICEnsemble VT1724 (Envy24HT)
 *
 *   Lowlevel functions for Audiotrak Prodigy 7.1 Hifi
 *   based on pontis.c
 *
 *      Copyright (c) 2007 Julian Scheel <julian@jusst.de>
 *      Copyright (c) 2007 allank
 *      Copyright (c) 2004 Takashi Iwai <tiwai@suse.de>
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


#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/mutex.h>

#include <sound/core.h>
#include <sound/info.h>
#include <sound/tlv.h>

#include "ice1712.h"
#include "envy24ht.h"
#include "prodigy_hifi.h"

struct prodigy_hifi_spec {
	unsigned short master[2];
	unsigned short vol[8];
};

/* I2C addresses */
#define WM_DEV		0x34

/* WM8776 registers */
#define WM_HP_ATTEN_L		0x00	/* headphone left attenuation */
#define WM_HP_ATTEN_R		0x01	/* headphone left attenuation */
#define WM_HP_MASTER		0x02	/* headphone master (both channels),
						override LLR */
#define WM_DAC_ATTEN_L		0x03	/* digital left attenuation */
#define WM_DAC_ATTEN_R		0x04
#define WM_DAC_MASTER		0x05
#define WM_PHASE_SWAP		0x06	/* DAC phase swap */
#define WM_DAC_CTRL1		0x07
#define WM_DAC_MUTE		0x08
#define WM_DAC_CTRL2		0x09
#define WM_DAC_INT		0x0a
#define WM_ADC_INT		0x0b
#define WM_MASTER_CTRL		0x0c
#define WM_POWERDOWN		0x0d
#define WM_ADC_ATTEN_L		0x0e
#define WM_ADC_ATTEN_R		0x0f
#define WM_ALC_CTRL1		0x10
#define WM_ALC_CTRL2		0x11
#define WM_ALC_CTRL3		0x12
#define WM_NOISE_GATE		0x13
#define WM_LIMITER		0x14
#define WM_ADC_MUX		0x15
#define WM_OUT_MUX		0x16
#define WM_RESET		0x17

/* Analog Recording Source :- Mic, LineIn, CD/Video, */

/* implement capture source select control for WM8776 */

#define WM_AIN1 "AIN1"
#define WM_AIN2 "AIN2"
#define WM_AIN3 "AIN3"
#define WM_AIN4 "AIN4"
#define WM_AIN5 "AIN5"

/* GPIO pins of envy24ht connected to wm8766 */
#define WM8766_SPI_CLK	 (1<<17) /* CLK, Pin97 on ICE1724 */
#define WM8766_SPI_MD	  (1<<16) /* DATA VT1724 -> WM8766, Pin96 */
#define WM8766_SPI_ML	  (1<<18) /* Latch, Pin98 */

/* WM8766 registers */
#define WM8766_DAC_CTRL	 0x02   /* DAC Control */
#define WM8766_INT_CTRL	 0x03   /* Interface Control */
#define WM8766_DAC_CTRL2	0x09
#define WM8766_DAC_CTRL3	0x0a
#define WM8766_RESET	    0x1f
#define WM8766_LDA1	     0x00
#define WM8766_LDA2	     0x04
#define WM8766_LDA3	     0x06
#define WM8766_RDA1	     0x01
#define WM8766_RDA2	     0x05
#define WM8766_RDA3	     0x07
#define WM8766_MUTE1	    0x0C
#define WM8766_MUTE2	    0x0F


/*
 * Prodigy HD2
 */
#define AK4396_ADDR    0x00
#define AK4396_CSN    (1 << 8)    /* CSN->GPIO8, pin 75 */
#define AK4396_CCLK   (1 << 9)    /* CCLK->GPIO9, pin 76 */
#define AK4396_CDTI   (1 << 10)   /* CDTI->GPIO10, pin 77 */

/* ak4396 registers */
#define AK4396_CTRL1	    0x00
#define AK4396_CTRL2	    0x01
#define AK4396_CTRL3	    0x02
#define AK4396_LCH_ATT	  0x03
#define AK4396_RCH_ATT	  0x04


/*
 * get the current register value of WM codec
 */
static unsigned short wm_get(struct snd_ice1712 *ice, int reg)
{
	reg <<= 1;
	return ((unsigned short)ice->akm[0].images[reg] << 8) |
		ice->akm[0].images[reg + 1];
}

/*
 * set the register value of WM codec and remember it
 */
static void wm_put_nocache(struct snd_ice1712 *ice, int reg, unsigned short val)
{
	unsigned short cval;
	cval = (reg << 9) | val;
	snd_vt1724_write_i2c(ice, WM_DEV, cval >> 8, cval & 0xff);
}

static void wm_put(struct snd_ice1712 *ice, int reg, unsigned short val)
{
	wm_put_nocache(ice, reg, val);
	reg <<= 1;
	ice->akm[0].images[reg] = val >> 8;
	ice->akm[0].images[reg + 1] = val;
}

/*
 * write data in the SPI mode
 */

static void set_gpio_bit(struct snd_ice1712 *ice, unsigned int bit, int val)
{
	unsigned int tmp = snd_ice1712_gpio_read(ice);
	if (val)
		tmp |= bit;
	else
		tmp &= ~bit;
	snd_ice1712_gpio_write(ice, tmp);
}

/*
 * SPI implementation for WM8766 codec - only writing supported, no readback
 */

static void wm8766_spi_send_word(struct snd_ice1712 *ice, unsigned int data)
{
	int i;
	for (i = 0; i < 16; i++) {
		set_gpio_bit(ice, WM8766_SPI_CLK, 0);
		udelay(1);
		set_gpio_bit(ice, WM8766_SPI_MD, data & 0x8000);
		udelay(1);
		set_gpio_bit(ice, WM8766_SPI_CLK, 1);
		udelay(1);
		data <<= 1;
	}
}

static void wm8766_spi_write(struct snd_ice1712 *ice, unsigned int reg,
			     unsigned int data)
{
	unsigned int block;

	snd_ice1712_gpio_set_dir(ice, WM8766_SPI_MD|
					WM8766_SPI_CLK|WM8766_SPI_ML);
	snd_ice1712_gpio_set_mask(ice, ~(WM8766_SPI_MD|
					WM8766_SPI_CLK|WM8766_SPI_ML));
	/* latch must be low when writing */
	set_gpio_bit(ice, WM8766_SPI_ML, 0);
	block = (reg << 9) | (data & 0x1ff);
	wm8766_spi_send_word(ice, block); /* REGISTER ADDRESS */
	/* release latch */
	set_gpio_bit(ice, WM8766_SPI_ML, 1);
	udelay(1);
	/* restore */
	snd_ice1712_gpio_set_mask(ice, ice->gpio.write_mask);
	snd_ice1712_gpio_set_dir(ice, ice->gpio.direction);
}


/*
 * serial interface for ak4396 - only writing supported, no readback
 */

static void ak4396_send_word(struct snd_ice1712 *ice, unsigned int data)
{
	int i;
	for (i = 0; i < 16; i++) {
		set_gpio_bit(ice, AK4396_CCLK, 0);
		udelay(1);
		set_gpio_bit(ice, AK4396_CDTI, data & 0x8000);
		udelay(1);
		set_gpio_bit(ice, AK4396_CCLK, 1);
		udelay(1);
		data <<= 1;
	}
}

static void ak4396_write(struct snd_ice1712 *ice, unsigned int reg,
			 unsigned int data)
{
	unsigned int block;

	snd_ice1712_gpio_set_dir(ice, AK4396_CSN|AK4396_CCLK|AK4396_CDTI);
	snd_ice1712_gpio_set_mask(ice, ~(AK4396_CSN|AK4396_CCLK|AK4396_CDTI));
	/* latch must be low when writing */
	set_gpio_bit(ice, AK4396_CSN, 0); 
	block =  ((AK4396_ADDR & 0x03) << 14) | (1 << 13) |
			((reg & 0x1f) << 8) | (data & 0xff);
	ak4396_send_word(ice, block); /* REGISTER ADDRESS */
	/* release latch */
	set_gpio_bit(ice, AK4396_CSN, 1);
	udelay(1);
	/* restore */
	snd_ice1712_gpio_set_mask(ice, ice->gpio.write_mask);
	snd_ice1712_gpio_set_dir(ice, ice->gpio.direction);
}


/*
 * ak4396 mixers
 */



/*
 * DAC volume attenuation mixer control (-64dB to 0dB)
 */

static int ak4396_dac_vol_info(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;   /* mute */
	uinfo->value.integer.max = 0xFF; /* linear */
	return 0;
}

static int ak4396_dac_vol_get(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	struct prodigy_hifi_spec *spec = ice->spec;
	int i;
	
	for (i = 0; i < 2; i++)
		ucontrol->value.integer.value[i] = spec->vol[i];

	return 0;
}

static int ak4396_dac_vol_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	struct prodigy_hifi_spec *spec = ice->spec;
	int i;
	int change = 0;
	
	mutex_lock(&ice->gpio_mutex);
	for (i = 0; i < 2; i++) {
		if (ucontrol->value.integer.value[i] != spec->vol[i]) {
			spec->vol[i] = ucontrol->value.integer.value[i];
			ak4396_write(ice, AK4396_LCH_ATT + i,
				     spec->vol[i] & 0xff);
			change = 1;
		}
	}
	mutex_unlock(&ice->gpio_mutex);
	return change;
}

static const DECLARE_TLV_DB_SCALE(db_scale_wm_dac, -12700, 100, 1);
static const DECLARE_TLV_DB_LINEAR(ak4396_db_scale, TLV_DB_GAIN_MUTE, 0);

static struct snd_kcontrol_new prodigy_hd2_controls[] = {
    {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.access = (SNDRV_CTL_ELEM_ACCESS_READWRITE |
		SNDRV_CTL_ELEM_ACCESS_TLV_READ),
	.name = "Front Playback Volume",
	.info = ak4396_dac_vol_info,
	.get = ak4396_dac_vol_get,
	.put = ak4396_dac_vol_put,
	.tlv = { .p = ak4396_db_scale },
    },
};


/* --------------- */

#define WM_VOL_MAX	255
#define WM_VOL_MUTE	0x8000


#define DAC_0dB	0xff
#define DAC_RES	128
#define DAC_MIN	(DAC_0dB - DAC_RES)


static void wm_set_vol(struct snd_ice1712 *ice, unsigned int index,
		       unsigned short vol, unsigned short master)
{
	unsigned char nvol;
	
	if ((master & WM_VOL_MUTE) || (vol & WM_VOL_MUTE))
		nvol = 0;
	else {
		nvol = (((vol & ~WM_VOL_MUTE) * (master & ~WM_VOL_MUTE)) / 128)
				& WM_VOL_MAX;
		nvol = (nvol ? (nvol + DAC_MIN) : 0) & 0xff;
	}
	
	wm_put(ice, index, nvol);
	wm_put_nocache(ice, index, 0x100 | nvol);
}

static void wm8766_set_vol(struct snd_ice1712 *ice, unsigned int index,
			   unsigned short vol, unsigned short master)
{
	unsigned char nvol;
	
	if ((master & WM_VOL_MUTE) || (vol & WM_VOL_MUTE))
		nvol = 0;
	else {
		nvol = (((vol & ~WM_VOL_MUTE) * (master & ~WM_VOL_MUTE)) / 128)
				& WM_VOL_MAX;
		nvol = (nvol ? (nvol + DAC_MIN) : 0) & 0xff;
	}

	wm8766_spi_write(ice, index, (0x0100 | nvol));
}


/*
 * DAC volume attenuation mixer control (-64dB to 0dB)
 */

static int wm_dac_vol_info(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;	/* mute */
	uinfo->value.integer.max = DAC_RES;	/* 0dB, 0.5dB step */
	return 0;
}

static int wm_dac_vol_get(struct snd_kcontrol *kcontrol,
			  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	struct prodigy_hifi_spec *spec = ice->spec;
	int i;

	for (i = 0; i < 2; i++)
		ucontrol->value.integer.value[i] =
			spec->vol[2 + i] & ~WM_VOL_MUTE;
	return 0;
}

static int wm_dac_vol_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	struct prodigy_hifi_spec *spec = ice->spec;
	int i, idx, change = 0;

	mutex_lock(&ice->gpio_mutex);
	for (i = 0; i < 2; i++) {
		if (ucontrol->value.integer.value[i] != spec->vol[2 + i]) {
			idx = WM_DAC_ATTEN_L + i;
			spec->vol[2 + i] &= WM_VOL_MUTE;
			spec->vol[2 + i] |= ucontrol->value.integer.value[i];
			wm_set_vol(ice, idx, spec->vol[2 + i], spec->master[i]);
			change = 1;
		}
	}
	mutex_unlock(&ice->gpio_mutex);
	return change;
}


/*
 * WM8766 DAC volume attenuation mixer control
 */
static int wm8766_vol_info(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_info *uinfo)
{
	int voices = kcontrol->private_value >> 8;
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = voices;
	uinfo->value.integer.min = 0;		/* mute */
	uinfo->value.integer.max = DAC_RES;	/* 0dB */
	return 0;
}

static int wm8766_vol_get(struct snd_kcontrol *kcontrol,
			  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	struct prodigy_hifi_spec *spec = ice->spec;
	int i, ofs, voices;

	voices = kcontrol->private_value >> 8;
	ofs = kcontrol->private_value & 0xff;
	for (i = 0; i < voices; i++)
		ucontrol->value.integer.value[i] = spec->vol[ofs + i];
	return 0;
}

static int wm8766_vol_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	struct prodigy_hifi_spec *spec = ice->spec;
	int i, idx, ofs, voices;
	int change = 0;

	voices = kcontrol->private_value >> 8;
	ofs = kcontrol->private_value & 0xff;
	mutex_lock(&ice->gpio_mutex);
	for (i = 0; i < voices; i++) {
		if (ucontrol->value.integer.value[i] != spec->vol[ofs + i]) {
			idx = WM8766_LDA1 + ofs + i;
			spec->vol[ofs + i] &= WM_VOL_MUTE;
			spec->vol[ofs + i] |= ucontrol->value.integer.value[i];
			wm8766_set_vol(ice, idx,
				       spec->vol[ofs + i], spec->master[i]);
			change = 1;
		}
	}
	mutex_unlock(&ice->gpio_mutex);
	return change;
}

/*
 * Master volume attenuation mixer control / applied to WM8776+WM8766
 */
static int wm_master_vol_info(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = DAC_RES;
	return 0;
}

static int wm_master_vol_get(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	struct prodigy_hifi_spec *spec = ice->spec;
	int i;
	for (i = 0; i < 2; i++)
		ucontrol->value.integer.value[i] = spec->master[i];
	return 0;
}

static int wm_master_vol_put(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	struct prodigy_hifi_spec *spec = ice->spec;
	int ch, change = 0;

	mutex_lock(&ice->gpio_mutex);
	for (ch = 0; ch < 2; ch++) {
		if (ucontrol->value.integer.value[ch] != spec->master[ch]) {
			spec->master[ch] = ucontrol->value.integer.value[ch];

			/* Apply to front DAC */
			wm_set_vol(ice, WM_DAC_ATTEN_L + ch,
				   spec->vol[2 + ch], spec->master[ch]);

			wm8766_set_vol(ice, WM8766_LDA1 + ch,
				       spec->vol[0 + ch], spec->master[ch]);

			wm8766_set_vol(ice, WM8766_LDA2 + ch,
				       spec->vol[4 + ch], spec->master[ch]);

			wm8766_set_vol(ice, WM8766_LDA3 + ch,
				       spec->vol[6 + ch], spec->master[ch]);
			change = 1;
		}
	}
	mutex_unlock(&ice->gpio_mutex);	
	return change;
}


/* KONSTI */

static int wm_adc_mux_enum_info(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_info *uinfo)
{
	static const char * const texts[32] = {
		"NULL", WM_AIN1, WM_AIN2, WM_AIN1 "+" WM_AIN2,
		WM_AIN3, WM_AIN1 "+" WM_AIN3, WM_AIN2 "+" WM_AIN3,
		WM_AIN1 "+" WM_AIN2 "+" WM_AIN3,
		WM_AIN4, WM_AIN1 "+" WM_AIN4, WM_AIN2 "+" WM_AIN4,
		WM_AIN1 "+" WM_AIN2 "+" WM_AIN4,
		WM_AIN3 "+" WM_AIN4, WM_AIN1 "+" WM_AIN3 "+" WM_AIN4,
		WM_AIN2 "+" WM_AIN3 "+" WM_AIN4,
		WM_AIN1 "+" WM_AIN2 "+" WM_AIN3 "+" WM_AIN4,
		WM_AIN5, WM_AIN1 "+" WM_AIN5, WM_AIN2 "+" WM_AIN5,
		WM_AIN1 "+" WM_AIN2 "+" WM_AIN5,
		WM_AIN3 "+" WM_AIN5, WM_AIN1 "+" WM_AIN3 "+" WM_AIN5,
		WM_AIN2 "+" WM_AIN3 "+" WM_AIN5,
		WM_AIN1 "+" WM_AIN2 "+" WM_AIN3 "+" WM_AIN5,
		WM_AIN4 "+" WM_AIN5, WM_AIN1 "+" WM_AIN4 "+" WM_AIN5,
		WM_AIN2 "+" WM_AIN4 "+" WM_AIN5,
		WM_AIN1 "+" WM_AIN2 "+" WM_AIN4 "+" WM_AIN5,
		WM_AIN3 "+" WM_AIN4 "+" WM_AIN5,
		WM_AIN1 "+" WM_AIN3 "+" WM_AIN4 "+" WM_AIN5,
		WM_AIN2 "+" WM_AIN3 "+" WM_AIN4 "+" WM_AIN5,
		WM_AIN1 "+" WM_AIN2 "+" WM_AIN3 "+" WM_AIN4 "+" WM_AIN5
	};

	return snd_ctl_enum_info(uinfo, 1, 32, texts);
}

static int wm_adc_mux_enum_get(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);

	mutex_lock(&ice->gpio_mutex);
	ucontrol->value.integer.value[0] = wm_get(ice, WM_ADC_MUX) & 0x1f;
	mutex_unlock(&ice->gpio_mutex);
	return 0;
}

static int wm_adc_mux_enum_put(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	unsigned short oval, nval;
	int change = 0;

	mutex_lock(&ice->gpio_mutex);
	oval = wm_get(ice, WM_ADC_MUX);
	nval = (oval & 0xe0) | ucontrol->value.integer.value[0];
	if (nval != oval) {
		wm_put(ice, WM_ADC_MUX, nval);
		change = 1;
	}
	mutex_unlock(&ice->gpio_mutex);
	return change;
}

/* KONSTI */

/*
 * ADC gain mixer control (-64dB to 0dB)
 */

#define ADC_0dB	0xcf
#define ADC_RES	128
#define ADC_MIN	(ADC_0dB - ADC_RES)

static int wm_adc_vol_info(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;	/* mute (-64dB) */
	uinfo->value.integer.max = ADC_RES;	/* 0dB, 0.5dB step */
	return 0;
}

static int wm_adc_vol_get(struct snd_kcontrol *kcontrol,
			  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	unsigned short val;
	int i;

	mutex_lock(&ice->gpio_mutex);
	for (i = 0; i < 2; i++) {
		val = wm_get(ice, WM_ADC_ATTEN_L + i) & 0xff;
		val = val > ADC_MIN ? (val - ADC_MIN) : 0;
		ucontrol->value.integer.value[i] = val;
	}
	mutex_unlock(&ice->gpio_mutex);
	return 0;
}

static int wm_adc_vol_put(struct snd_kcontrol *kcontrol,
			  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	unsigned short ovol, nvol;
	int i, idx, change = 0;

	mutex_lock(&ice->gpio_mutex);
	for (i = 0; i < 2; i++) {
		nvol = ucontrol->value.integer.value[i];
		nvol = nvol ? (nvol + ADC_MIN) : 0;
		idx  = WM_ADC_ATTEN_L + i;
		ovol = wm_get(ice, idx) & 0xff;
		if (ovol != nvol) {
			wm_put(ice, idx, nvol);
			change = 1;
		}
	}
	mutex_unlock(&ice->gpio_mutex);
	return change;
}

/*
 * ADC input mux mixer control
 */
#define wm_adc_mux_info		snd_ctl_boolean_mono_info

static int wm_adc_mux_get(struct snd_kcontrol *kcontrol,
			  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	int bit = kcontrol->private_value;

	mutex_lock(&ice->gpio_mutex);
	ucontrol->value.integer.value[0] =
		(wm_get(ice, WM_ADC_MUX) & (1 << bit)) ? 1 : 0;
	mutex_unlock(&ice->gpio_mutex);
	return 0;
}

static int wm_adc_mux_put(struct snd_kcontrol *kcontrol,
			  struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	int bit = kcontrol->private_value;
	unsigned short oval, nval;
	int change;

	mutex_lock(&ice->gpio_mutex);
	nval = oval = wm_get(ice, WM_ADC_MUX);
	if (ucontrol->value.integer.value[0])
		nval |= (1 << bit);
	else
		nval &= ~(1 << bit);
	change = nval != oval;
	if (change) {
		wm_put(ice, WM_ADC_MUX, nval);
	}
	mutex_unlock(&ice->gpio_mutex);
	return 0;
}

/*
 * Analog bypass (In -> Out)
 */
#define wm_bypass_info		snd_ctl_boolean_mono_info

static int wm_bypass_get(struct snd_kcontrol *kcontrol,
			 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);

	mutex_lock(&ice->gpio_mutex);
	ucontrol->value.integer.value[0] =
		(wm_get(ice, WM_OUT_MUX) & 0x04) ? 1 : 0;
	mutex_unlock(&ice->gpio_mutex);
	return 0;
}

static int wm_bypass_put(struct snd_kcontrol *kcontrol,
			 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	unsigned short val, oval;
	int change = 0;

	mutex_lock(&ice->gpio_mutex);
	val = oval = wm_get(ice, WM_OUT_MUX);
	if (ucontrol->value.integer.value[0])
		val |= 0x04;
	else
		val &= ~0x04;
	if (val != oval) {
		wm_put(ice, WM_OUT_MUX, val);
		change = 1;
	}
	mutex_unlock(&ice->gpio_mutex);
	return change;
}

/*
 * Left/Right swap
 */
#define wm_chswap_info		snd_ctl_boolean_mono_info

static int wm_chswap_get(struct snd_kcontrol *kcontrol,
			 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);

	mutex_lock(&ice->gpio_mutex);
	ucontrol->value.integer.value[0] =
			(wm_get(ice, WM_DAC_CTRL1) & 0xf0) != 0x90;
	mutex_unlock(&ice->gpio_mutex);
	return 0;
}

static int wm_chswap_put(struct snd_kcontrol *kcontrol,
			 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	unsigned short val, oval;
	int change = 0;

	mutex_lock(&ice->gpio_mutex);
	oval = wm_get(ice, WM_DAC_CTRL1);
	val = oval & 0x0f;
	if (ucontrol->value.integer.value[0])
		val |= 0x60;
	else
		val |= 0x90;
	if (val != oval) {
		wm_put(ice, WM_DAC_CTRL1, val);
		wm_put_nocache(ice, WM_DAC_CTRL1, val);
		change = 1;
	}
	mutex_unlock(&ice->gpio_mutex);
	return change;
}


/*
 * mixers
 */

static struct snd_kcontrol_new prodigy_hifi_controls[] = {
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.access = (SNDRV_CTL_ELEM_ACCESS_READWRITE |
			   SNDRV_CTL_ELEM_ACCESS_TLV_READ),
		.name = "Master Playback Volume",
		.info = wm_master_vol_info,
		.get = wm_master_vol_get,
		.put = wm_master_vol_put,
		.tlv = { .p = db_scale_wm_dac }
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.access = (SNDRV_CTL_ELEM_ACCESS_READWRITE |
			   SNDRV_CTL_ELEM_ACCESS_TLV_READ),
		.name = "Front Playback Volume",
		.info = wm_dac_vol_info,
		.get = wm_dac_vol_get,
		.put = wm_dac_vol_put,
		.tlv = { .p = db_scale_wm_dac },
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.access = (SNDRV_CTL_ELEM_ACCESS_READWRITE |
			   SNDRV_CTL_ELEM_ACCESS_TLV_READ),
		.name = "Rear Playback Volume",
		.info = wm8766_vol_info,
		.get = wm8766_vol_get,
		.put = wm8766_vol_put,
		.private_value = (2 << 8) | 0,
		.tlv = { .p = db_scale_wm_dac },
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.access = (SNDRV_CTL_ELEM_ACCESS_READWRITE |
			   SNDRV_CTL_ELEM_ACCESS_TLV_READ),
		.name = "Center Playback Volume",
		.info = wm8766_vol_info,
		.get = wm8766_vol_get,
		.put = wm8766_vol_put,
		.private_value = (1 << 8) | 4,
		.tlv = { .p = db_scale_wm_dac }
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.access = (SNDRV_CTL_ELEM_ACCESS_READWRITE |
			   SNDRV_CTL_ELEM_ACCESS_TLV_READ),
		.name = "LFE Playback Volume",
		.info = wm8766_vol_info,
		.get = wm8766_vol_get,
		.put = wm8766_vol_put,
		.private_value = (1 << 8) | 5,
		.tlv = { .p = db_scale_wm_dac }
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.access = (SNDRV_CTL_ELEM_ACCESS_READWRITE |
			   SNDRV_CTL_ELEM_ACCESS_TLV_READ),
		.name = "Side Playback Volume",
		.info = wm8766_vol_info,
		.get = wm8766_vol_get,
		.put = wm8766_vol_put,
		.private_value = (2 << 8) | 6,
		.tlv = { .p = db_scale_wm_dac },
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.access = (SNDRV_CTL_ELEM_ACCESS_READWRITE |
			   SNDRV_CTL_ELEM_ACCESS_TLV_READ),
		.name = "Capture Volume",
		.info = wm_adc_vol_info,
		.get = wm_adc_vol_get,
		.put = wm_adc_vol_put,
		.tlv = { .p = db_scale_wm_dac },
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "CD Capture Switch",
		.info = wm_adc_mux_info,
		.get = wm_adc_mux_get,
		.put = wm_adc_mux_put,
		.private_value = 0,
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Line Capture Switch",
		.info = wm_adc_mux_info,
		.get = wm_adc_mux_get,
		.put = wm_adc_mux_put,
		.private_value = 1,
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Analog Bypass Switch",
		.info = wm_bypass_info,
		.get = wm_bypass_get,
		.put = wm_bypass_put,
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Swap Output Channels",
		.info = wm_chswap_info,
		.get = wm_chswap_get,
		.put = wm_chswap_put,
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Analog Capture Source",
		.info = wm_adc_mux_enum_info,
		.get = wm_adc_mux_enum_get,
		.put = wm_adc_mux_enum_put,
	},
};

/*
 * WM codec registers
 */
static void wm_proc_regs_write(struct snd_info_entry *entry,
			       struct snd_info_buffer *buffer)
{
	struct snd_ice1712 *ice = entry->private_data;
	char line[64];
	unsigned int reg, val;
	mutex_lock(&ice->gpio_mutex);
	while (!snd_info_get_line(buffer, line, sizeof(line))) {
		if (sscanf(line, "%x %x", &reg, &val) != 2)
			continue;
		if (reg <= 0x17 && val <= 0xffff)
			wm_put(ice, reg, val);
	}
	mutex_unlock(&ice->gpio_mutex);
}

static void wm_proc_regs_read(struct snd_info_entry *entry,
			      struct snd_info_buffer *buffer)
{
	struct snd_ice1712 *ice = entry->private_data;
	int reg, val;

	mutex_lock(&ice->gpio_mutex);
	for (reg = 0; reg <= 0x17; reg++) {
		val = wm_get(ice, reg);
		snd_iprintf(buffer, "%02x = %04x\n", reg, val);
	}
	mutex_unlock(&ice->gpio_mutex);
}

static void wm_proc_init(struct snd_ice1712 *ice)
{
	struct snd_info_entry *entry;
	if (!snd_card_proc_new(ice->card, "wm_codec", &entry)) {
		snd_info_set_text_ops(entry, ice, wm_proc_regs_read);
		entry->mode |= 0200;
		entry->c.text.write = wm_proc_regs_write;
	}
}

static int prodigy_hifi_add_controls(struct snd_ice1712 *ice)
{
	unsigned int i;
	int err;

	for (i = 0; i < ARRAY_SIZE(prodigy_hifi_controls); i++) {
		err = snd_ctl_add(ice->card,
				  snd_ctl_new1(&prodigy_hifi_controls[i], ice));
		if (err < 0)
			return err;
	}

	wm_proc_init(ice);

	return 0;
}

static int prodigy_hd2_add_controls(struct snd_ice1712 *ice)
{
	unsigned int i;
	int err;

	for (i = 0; i < ARRAY_SIZE(prodigy_hd2_controls); i++) {
		err = snd_ctl_add(ice->card,
				  snd_ctl_new1(&prodigy_hd2_controls[i], ice));
		if (err < 0)
			return err;
	}

	wm_proc_init(ice);

	return 0;
}

static void wm8766_init(struct snd_ice1712 *ice)
{
	static unsigned short wm8766_inits[] = {
		WM8766_RESET,	   0x0000,
		WM8766_DAC_CTRL,	0x0120,
		WM8766_INT_CTRL,	0x0022, /* I2S Normal Mode, 24 bit */
		WM8766_DAC_CTRL2,       0x0001,
		WM8766_DAC_CTRL3,       0x0080,
		WM8766_LDA1,	    0x0100,
		WM8766_LDA2,	    0x0100,
		WM8766_LDA3,	    0x0100,
		WM8766_RDA1,	    0x0100,
		WM8766_RDA2,	    0x0100,
		WM8766_RDA3,	    0x0100,
		WM8766_MUTE1,	   0x0000,
		WM8766_MUTE2,	   0x0000,
	};
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(wm8766_inits); i += 2)
		wm8766_spi_write(ice, wm8766_inits[i], wm8766_inits[i + 1]);
}

static void wm8776_init(struct snd_ice1712 *ice)
{
	static unsigned short wm8776_inits[] = {
		/* These come first to reduce init pop noise */
		WM_ADC_MUX,	0x0003,	/* ADC mute */
		/* 0x00c0 replaced by 0x0003 */
		
		WM_DAC_MUTE,	0x0001,	/* DAC softmute */
		WM_DAC_CTRL1,	0x0000,	/* DAC mute */

		WM_POWERDOWN,	0x0008,	/* All power-up except HP */
		WM_RESET,	0x0000,	/* reset */
	};
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(wm8776_inits); i += 2)
		wm_put(ice, wm8776_inits[i], wm8776_inits[i + 1]);
}

#ifdef CONFIG_PM_SLEEP
static int prodigy_hifi_resume(struct snd_ice1712 *ice)
{
	static unsigned short wm8776_reinit_registers[] = {
		WM_MASTER_CTRL,
		WM_DAC_INT,
		WM_ADC_INT,
		WM_OUT_MUX,
		WM_HP_ATTEN_L,
		WM_HP_ATTEN_R,
		WM_PHASE_SWAP,
		WM_DAC_CTRL2,
		WM_ADC_ATTEN_L,
		WM_ADC_ATTEN_R,
		WM_ALC_CTRL1,
		WM_ALC_CTRL2,
		WM_ALC_CTRL3,
		WM_NOISE_GATE,
		WM_ADC_MUX,
		/* no DAC attenuation here */
	};
	struct prodigy_hifi_spec *spec = ice->spec;
	int i, ch;

	mutex_lock(&ice->gpio_mutex);

	/* reinitialize WM8776 and re-apply old register values */
	wm8776_init(ice);
	schedule_timeout_uninterruptible(1);
	for (i = 0; i < ARRAY_SIZE(wm8776_reinit_registers); i++)
		wm_put(ice, wm8776_reinit_registers[i],
		       wm_get(ice, wm8776_reinit_registers[i]));

	/* reinitialize WM8766 and re-apply volumes for all DACs */
	wm8766_init(ice);
	for (ch = 0; ch < 2; ch++) {
		wm_set_vol(ice, WM_DAC_ATTEN_L + ch,
			   spec->vol[2 + ch], spec->master[ch]);

		wm8766_set_vol(ice, WM8766_LDA1 + ch,
			       spec->vol[0 + ch], spec->master[ch]);

		wm8766_set_vol(ice, WM8766_LDA2 + ch,
			       spec->vol[4 + ch], spec->master[ch]);

		wm8766_set_vol(ice, WM8766_LDA3 + ch,
			       spec->vol[6 + ch], spec->master[ch]);
	}

	/* unmute WM8776 DAC */
	wm_put(ice, WM_DAC_MUTE, 0x00);
	wm_put(ice, WM_DAC_CTRL1, 0x90);

	mutex_unlock(&ice->gpio_mutex);
	return 0;
}
#endif

/*
 * initialize the chip
 */
static int prodigy_hifi_init(struct snd_ice1712 *ice)
{
	static unsigned short wm8776_defaults[] = {
		WM_MASTER_CTRL,  0x0022, /* 256fs, slave mode */
		WM_DAC_INT,	0x0022,	/* I2S, normal polarity, 24bit */
		WM_ADC_INT,	0x0022,	/* I2S, normal polarity, 24bit */
		WM_DAC_CTRL1,	0x0090,	/* DAC L/R */
		WM_OUT_MUX,	0x0001,	/* OUT DAC */
		WM_HP_ATTEN_L,	0x0179,	/* HP 0dB */
		WM_HP_ATTEN_R,	0x0179,	/* HP 0dB */
		WM_DAC_ATTEN_L,	0x0000,	/* DAC 0dB */
		WM_DAC_ATTEN_L,	0x0100,	/* DAC 0dB */
		WM_DAC_ATTEN_R,	0x0000,	/* DAC 0dB */
		WM_DAC_ATTEN_R,	0x0100,	/* DAC 0dB */
		WM_PHASE_SWAP,	0x0000,	/* phase normal */
#if 0
		WM_DAC_MASTER,	0x0100,	/* DAC master muted */
#endif
		WM_DAC_CTRL2,	0x0000,	/* no deemphasis, no ZFLG */
		WM_ADC_ATTEN_L,	0x0000,	/* ADC muted */
		WM_ADC_ATTEN_R,	0x0000,	/* ADC muted */
#if 1
		WM_ALC_CTRL1,	0x007b,	/* */
		WM_ALC_CTRL2,	0x0000,	/* */
		WM_ALC_CTRL3,	0x0000,	/* */
		WM_NOISE_GATE,	0x0000,	/* */
#endif
		WM_DAC_MUTE,	0x0000,	/* DAC unmute */
		WM_ADC_MUX,	0x0003,	/* ADC unmute, both CD/Line On */
	};
	struct prodigy_hifi_spec *spec;
	unsigned int i;

	ice->vt1720 = 0;
	ice->vt1724 = 1;

	ice->num_total_dacs = 8;
	ice->num_total_adcs = 1;

	/* HACK - use this as the SPDIF source.
	* don't call snd_ice1712_gpio_get/put(), otherwise it's overwritten
	*/
	ice->gpio.saved[0] = 0;
	/* to remember the register values */

	ice->akm = kzalloc(sizeof(struct snd_akm4xxx), GFP_KERNEL);
	if (! ice->akm)
		return -ENOMEM;
	ice->akm_codecs = 1;

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec)
		return -ENOMEM;
	ice->spec = spec;

	/* initialize WM8776 codec */
	wm8776_init(ice);
	schedule_timeout_uninterruptible(1);
	for (i = 0; i < ARRAY_SIZE(wm8776_defaults); i += 2)
		wm_put(ice, wm8776_defaults[i], wm8776_defaults[i + 1]);

	wm8766_init(ice);

#ifdef CONFIG_PM_SLEEP
	ice->pm_resume = &prodigy_hifi_resume;
	ice->pm_suspend_enabled = 1;
#endif

	return 0;
}


/*
 * initialize the chip
 */
static void ak4396_init(struct snd_ice1712 *ice)
{
	static unsigned short ak4396_inits[] = {
		AK4396_CTRL1,	   0x87,   /* I2S Normal Mode, 24 bit */
		AK4396_CTRL2,	   0x02,
		AK4396_CTRL3,	   0x00, 
		AK4396_LCH_ATT,	 0x00,
		AK4396_RCH_ATT,	 0x00,
	};

	unsigned int i;

	/* initialize ak4396 codec */
	/* reset codec */
	ak4396_write(ice, AK4396_CTRL1, 0x86);
	msleep(100);
	ak4396_write(ice, AK4396_CTRL1, 0x87);

	for (i = 0; i < ARRAY_SIZE(ak4396_inits); i += 2)
		ak4396_write(ice, ak4396_inits[i], ak4396_inits[i+1]);
}

#ifdef CONFIG_PM_SLEEP
static int prodigy_hd2_resume(struct snd_ice1712 *ice)
{
	/* initialize ak4396 codec and restore previous mixer volumes */
	struct prodigy_hifi_spec *spec = ice->spec;
	int i;
	mutex_lock(&ice->gpio_mutex);
	ak4396_init(ice);
	for (i = 0; i < 2; i++)
		ak4396_write(ice, AK4396_LCH_ATT + i, spec->vol[i] & 0xff);
	mutex_unlock(&ice->gpio_mutex);
	return 0;
}
#endif

static int prodigy_hd2_init(struct snd_ice1712 *ice)
{
	struct prodigy_hifi_spec *spec;

	ice->vt1720 = 0;
	ice->vt1724 = 1;

	ice->num_total_dacs = 1;
	ice->num_total_adcs = 1;

	/* HACK - use this as the SPDIF source.
	* don't call snd_ice1712_gpio_get/put(), otherwise it's overwritten
	*/
	ice->gpio.saved[0] = 0;
	/* to remember the register values */

	ice->akm = kzalloc(sizeof(struct snd_akm4xxx), GFP_KERNEL);
	if (! ice->akm)
		return -ENOMEM;
	ice->akm_codecs = 1;

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec)
		return -ENOMEM;
	ice->spec = spec;

#ifdef CONFIG_PM_SLEEP
	ice->pm_resume = &prodigy_hd2_resume;
	ice->pm_suspend_enabled = 1;
#endif

	ak4396_init(ice);

	return 0;
}


static unsigned char prodigy71hifi_eeprom[] = {
	0x4b,   /* SYSCONF: clock 512, spdif-in/ADC, 4DACs */
	0x80,   /* ACLINK: I2S */
	0xfc,   /* I2S: vol, 96k, 24bit, 192k */
	0xc3,   /* SPDIF: out-en, out-int, spdif-in */
	0xff,   /* GPIO_DIR */
	0xff,   /* GPIO_DIR1 */
	0x5f,   /* GPIO_DIR2 */
	0x00,   /* GPIO_MASK */
	0x00,   /* GPIO_MASK1 */
	0x00,   /* GPIO_MASK2 */
	0x00,   /* GPIO_STATE */
	0x00,   /* GPIO_STATE1 */
	0x00,   /* GPIO_STATE2 */
};

static unsigned char prodigyhd2_eeprom[] = {
	0x4b,   /* SYSCONF: clock 512, spdif-in/ADC, 4DACs */
	0x80,   /* ACLINK: I2S */
	0xfc,   /* I2S: vol, 96k, 24bit, 192k */
	0xc3,   /* SPDIF: out-en, out-int, spdif-in */
	0xff,   /* GPIO_DIR */
	0xff,   /* GPIO_DIR1 */
	0x5f,   /* GPIO_DIR2 */
	0x00,   /* GPIO_MASK */
	0x00,   /* GPIO_MASK1 */
	0x00,   /* GPIO_MASK2 */
	0x00,   /* GPIO_STATE */
	0x00,   /* GPIO_STATE1 */
	0x00,   /* GPIO_STATE2 */
};

static unsigned char fortissimo4_eeprom[] = {
	0x43,   /* SYSCONF: clock 512, ADC, 4DACs */	
	0x80,   /* ACLINK: I2S */
	0xfc,   /* I2S: vol, 96k, 24bit, 192k */
	0xc1,   /* SPDIF: out-en, out-int */
	0xff,   /* GPIO_DIR */
	0xff,   /* GPIO_DIR1 */
	0x5f,   /* GPIO_DIR2 */
	0x00,   /* GPIO_MASK */
	0x00,   /* GPIO_MASK1 */
	0x00,   /* GPIO_MASK2 */
	0x00,   /* GPIO_STATE */
	0x00,   /* GPIO_STATE1 */
	0x00,   /* GPIO_STATE2 */
};

/* entry point */
struct snd_ice1712_card_info snd_vt1724_prodigy_hifi_cards[] = {
	{
		.subvendor = VT1724_SUBDEVICE_PRODIGY_HIFI,
		.name = "Audiotrak Prodigy 7.1 HiFi",
		.model = "prodigy71hifi",
		.chip_init = prodigy_hifi_init,
		.build_controls = prodigy_hifi_add_controls,
		.eeprom_size = sizeof(prodigy71hifi_eeprom),
		.eeprom_data = prodigy71hifi_eeprom,
		.driver = "Prodigy71HIFI",
	},
	{
	.subvendor = VT1724_SUBDEVICE_PRODIGY_HD2,
	.name = "Audiotrak Prodigy HD2",
	.model = "prodigyhd2",
	.chip_init = prodigy_hd2_init,
	.build_controls = prodigy_hd2_add_controls,
	.eeprom_size = sizeof(prodigyhd2_eeprom),
	.eeprom_data = prodigyhd2_eeprom,
	.driver = "Prodigy71HD2",
	},
	{
		.subvendor = VT1724_SUBDEVICE_FORTISSIMO4,
		.name = "Hercules Fortissimo IV",
		.model = "fortissimo4",
		.chip_init = prodigy_hifi_init,
		.build_controls = prodigy_hifi_add_controls,
		.eeprom_size = sizeof(fortissimo4_eeprom),
		.eeprom_data = fortissimo4_eeprom,
		.driver = "Fortissimo4",
	},
	{ } /* terminator */
};

