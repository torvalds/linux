// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *   ALSA driver for ICEnsemble VT1724 (Envy24HT)
 *
 *   Lowlevel functions for Pontis MS300
 *
 *	Copyright (c) 2004 Takashi Iwai <tiwai@suse.de>
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
#include "pontis.h"

/* I2C addresses */
#define WM_DEV		0x34
#define CS_DEV		0x20

/* WM8776 registers */
#define WM_HP_ATTEN_L		0x00	/* headphone left attenuation */
#define WM_HP_ATTEN_R		0x01	/* headphone left attenuation */
#define WM_HP_MASTER		0x02	/* headphone master (both channels) */
					/* override LLR */
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

/*
 * GPIO
 */
#define PONTIS_CS_CS		(1<<4)	/* CS */
#define PONTIS_CS_CLK		(1<<5)	/* CLK */
#define PONTIS_CS_RDATA		(1<<6)	/* CS8416 -> VT1720 */
#define PONTIS_CS_WDATA		(1<<7)	/* VT1720 -> CS8416 */


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
 * DAC volume attenuation mixer control (-64dB to 0dB)
 */

#define DAC_0dB	0xff
#define DAC_RES	128
#define DAC_MIN	(DAC_0dB - DAC_RES)

static int wm_dac_vol_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;	/* mute */
	uinfo->value.integer.max = DAC_RES;	/* 0dB, 0.5dB step */
	return 0;
}

static int wm_dac_vol_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	unsigned short val;
	int i;

	guard(mutex)(&ice->gpio_mutex);
	for (i = 0; i < 2; i++) {
		val = wm_get(ice, WM_DAC_ATTEN_L + i) & 0xff;
		val = val > DAC_MIN ? (val - DAC_MIN) : 0;
		ucontrol->value.integer.value[i] = val;
	}
	return 0;
}

static int wm_dac_vol_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	unsigned short oval, nval;
	int i, idx, change = 0;

	guard(mutex)(&ice->gpio_mutex);
	for (i = 0; i < 2; i++) {
		nval = ucontrol->value.integer.value[i];
		nval = (nval ? (nval + DAC_MIN) : 0) & 0xff;
		idx = WM_DAC_ATTEN_L + i;
		oval = wm_get(ice, idx) & 0xff;
		if (oval != nval) {
			wm_put(ice, idx, nval);
			wm_put_nocache(ice, idx, nval | 0x100);
			change = 1;
		}
	}
	return change;
}

/*
 * ADC gain mixer control (-64dB to 0dB)
 */

#define ADC_0dB	0xcf
#define ADC_RES	128
#define ADC_MIN	(ADC_0dB - ADC_RES)

static int wm_adc_vol_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;	/* mute (-64dB) */
	uinfo->value.integer.max = ADC_RES;	/* 0dB, 0.5dB step */
	return 0;
}

static int wm_adc_vol_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	unsigned short val;
	int i;

	guard(mutex)(&ice->gpio_mutex);
	for (i = 0; i < 2; i++) {
		val = wm_get(ice, WM_ADC_ATTEN_L + i) & 0xff;
		val = val > ADC_MIN ? (val - ADC_MIN) : 0;
		ucontrol->value.integer.value[i] = val;
	}
	return 0;
}

static int wm_adc_vol_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	unsigned short ovol, nvol;
	int i, idx, change = 0;

	guard(mutex)(&ice->gpio_mutex);
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
	return change;
}

/*
 * ADC input mux mixer control
 */
#define wm_adc_mux_info		snd_ctl_boolean_mono_info

static int wm_adc_mux_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	int bit = kcontrol->private_value;

	guard(mutex)(&ice->gpio_mutex);
	ucontrol->value.integer.value[0] = (wm_get(ice, WM_ADC_MUX) & (1 << bit)) ? 1 : 0;
	return 0;
}

static int wm_adc_mux_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	int bit = kcontrol->private_value;
	unsigned short oval, nval;
	int change;

	guard(mutex)(&ice->gpio_mutex);
	nval = oval = wm_get(ice, WM_ADC_MUX);
	if (ucontrol->value.integer.value[0])
		nval |= (1 << bit);
	else
		nval &= ~(1 << bit);
	change = nval != oval;
	if (change) {
		wm_put(ice, WM_ADC_MUX, nval);
	}
	return change;
}

/*
 * Analog bypass (In -> Out)
 */
#define wm_bypass_info		snd_ctl_boolean_mono_info

static int wm_bypass_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);

	guard(mutex)(&ice->gpio_mutex);
	ucontrol->value.integer.value[0] = (wm_get(ice, WM_OUT_MUX) & 0x04) ? 1 : 0;
	return 0;
}

static int wm_bypass_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	unsigned short val, oval;
	int change = 0;

	guard(mutex)(&ice->gpio_mutex);
	val = oval = wm_get(ice, WM_OUT_MUX);
	if (ucontrol->value.integer.value[0])
		val |= 0x04;
	else
		val &= ~0x04;
	if (val != oval) {
		wm_put(ice, WM_OUT_MUX, val);
		change = 1;
	}
	return change;
}

/*
 * Left/Right swap
 */
#define wm_chswap_info		snd_ctl_boolean_mono_info

static int wm_chswap_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);

	guard(mutex)(&ice->gpio_mutex);
	ucontrol->value.integer.value[0] = (wm_get(ice, WM_DAC_CTRL1) & 0xf0) != 0x90;
	return 0;
}

static int wm_chswap_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	unsigned short val, oval;
	int change = 0;

	guard(mutex)(&ice->gpio_mutex);
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
	return change;
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

static void spi_send_byte(struct snd_ice1712 *ice, unsigned char data)
{
	int i;
	for (i = 0; i < 8; i++) {
		set_gpio_bit(ice, PONTIS_CS_CLK, 0);
		udelay(1);
		set_gpio_bit(ice, PONTIS_CS_WDATA, data & 0x80);
		udelay(1);
		set_gpio_bit(ice, PONTIS_CS_CLK, 1);
		udelay(1);
		data <<= 1;
	}
}

static unsigned int spi_read_byte(struct snd_ice1712 *ice)
{
	int i;
	unsigned int val = 0;

	for (i = 0; i < 8; i++) {
		val <<= 1;
		set_gpio_bit(ice, PONTIS_CS_CLK, 0);
		udelay(1);
		if (snd_ice1712_gpio_read(ice) & PONTIS_CS_RDATA)
			val |= 1;
		udelay(1);
		set_gpio_bit(ice, PONTIS_CS_CLK, 1);
		udelay(1);
	}
	return val;
}


static void spi_write(struct snd_ice1712 *ice, unsigned int dev, unsigned int reg, unsigned int data)
{
	snd_ice1712_gpio_set_dir(ice, PONTIS_CS_CS|PONTIS_CS_WDATA|PONTIS_CS_CLK);
	snd_ice1712_gpio_set_mask(ice, ~(PONTIS_CS_CS|PONTIS_CS_WDATA|PONTIS_CS_CLK));
	set_gpio_bit(ice, PONTIS_CS_CS, 0);
	spi_send_byte(ice, dev & ~1); /* WRITE */
	spi_send_byte(ice, reg); /* MAP */
	spi_send_byte(ice, data); /* DATA */
	/* trigger */
	set_gpio_bit(ice, PONTIS_CS_CS, 1);
	udelay(1);
	/* restore */
	snd_ice1712_gpio_set_mask(ice, ice->gpio.write_mask);
	snd_ice1712_gpio_set_dir(ice, ice->gpio.direction);
}

static unsigned int spi_read(struct snd_ice1712 *ice, unsigned int dev, unsigned int reg)
{
	unsigned int val;
	snd_ice1712_gpio_set_dir(ice, PONTIS_CS_CS|PONTIS_CS_WDATA|PONTIS_CS_CLK);
	snd_ice1712_gpio_set_mask(ice, ~(PONTIS_CS_CS|PONTIS_CS_WDATA|PONTIS_CS_CLK));
	set_gpio_bit(ice, PONTIS_CS_CS, 0);
	spi_send_byte(ice, dev & ~1); /* WRITE */
	spi_send_byte(ice, reg); /* MAP */
	/* trigger */
	set_gpio_bit(ice, PONTIS_CS_CS, 1);
	udelay(1);
	set_gpio_bit(ice, PONTIS_CS_CS, 0);
	spi_send_byte(ice, dev | 1); /* READ */
	val = spi_read_byte(ice);
	/* trigger */
	set_gpio_bit(ice, PONTIS_CS_CS, 1);
	udelay(1);
	/* restore */
	snd_ice1712_gpio_set_mask(ice, ice->gpio.write_mask);
	snd_ice1712_gpio_set_dir(ice, ice->gpio.direction);
	return val;
}


/*
 * SPDIF input source
 */
static int cs_source_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	static const char * const texts[] = {
		"Coax",		/* RXP0 */
		"Optical",	/* RXP1 */
		"CD",		/* RXP2 */
	};
	return snd_ctl_enum_info(uinfo, 1, 3, texts);
}

static int cs_source_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);

	guard(mutex)(&ice->gpio_mutex);
	ucontrol->value.enumerated.item[0] = ice->gpio.saved[0];
	return 0;
}

static int cs_source_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	unsigned char val;
	int change = 0;

	guard(mutex)(&ice->gpio_mutex);
	if (ucontrol->value.enumerated.item[0] != ice->gpio.saved[0]) {
		ice->gpio.saved[0] = ucontrol->value.enumerated.item[0] & 3;
		val = 0x80 | (ice->gpio.saved[0] << 3);
		spi_write(ice, CS_DEV, 0x04, val);
		change = 1;
	}
	return change;
}


/*
 * GPIO controls
 */
static int pontis_gpio_mask_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 0xffff; /* 16bit */
	return 0;
}

static int pontis_gpio_mask_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);

	guard(mutex)(&ice->gpio_mutex);
	/* 4-7 reserved */
	ucontrol->value.integer.value[0] = (~ice->gpio.write_mask & 0xffff) | 0x00f0;
	return 0;
}
	
static int pontis_gpio_mask_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	unsigned int val;
	int changed;

	guard(mutex)(&ice->gpio_mutex);
	/* 4-7 reserved */
	val = (~ucontrol->value.integer.value[0] & 0xffff) | 0x00f0;
	changed = val != ice->gpio.write_mask;
	ice->gpio.write_mask = val;
	return changed;
}

static int pontis_gpio_dir_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);

	guard(mutex)(&ice->gpio_mutex);
	/* 4-7 reserved */
	ucontrol->value.integer.value[0] = ice->gpio.direction & 0xff0f;
	return 0;
}
	
static int pontis_gpio_dir_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	unsigned int val;
	int changed;

	guard(mutex)(&ice->gpio_mutex);
	/* 4-7 reserved */
	val = ucontrol->value.integer.value[0] & 0xff0f;
	changed = (val != ice->gpio.direction);
	ice->gpio.direction = val;
	return changed;
}

static int pontis_gpio_data_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);

	guard(mutex)(&ice->gpio_mutex);
	snd_ice1712_gpio_set_dir(ice, ice->gpio.direction);
	snd_ice1712_gpio_set_mask(ice, ice->gpio.write_mask);
	ucontrol->value.integer.value[0] = snd_ice1712_gpio_read(ice) & 0xffff;
	return 0;
}

static int pontis_gpio_data_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	unsigned int val, nval;
	int changed = 0;

	guard(mutex)(&ice->gpio_mutex);
	snd_ice1712_gpio_set_dir(ice, ice->gpio.direction);
	snd_ice1712_gpio_set_mask(ice, ice->gpio.write_mask);
	val = snd_ice1712_gpio_read(ice) & 0xffff;
	nval = ucontrol->value.integer.value[0] & 0xffff;
	if (val != nval) {
		snd_ice1712_gpio_write(ice, nval);
		changed = 1;
	}
	return changed;
}

static const DECLARE_TLV_DB_SCALE(db_scale_volume, -6400, 50, 1);

/*
 * mixers
 */

static const struct snd_kcontrol_new pontis_controls[] = {
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.access = (SNDRV_CTL_ELEM_ACCESS_READWRITE |
			   SNDRV_CTL_ELEM_ACCESS_TLV_READ),
		.name = "PCM Playback Volume",
		.info = wm_dac_vol_info,
		.get = wm_dac_vol_get,
		.put = wm_dac_vol_put,
		.tlv = { .p = db_scale_volume },
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.access = (SNDRV_CTL_ELEM_ACCESS_READWRITE |
			   SNDRV_CTL_ELEM_ACCESS_TLV_READ),
		.name = "Capture Volume",
		.info = wm_adc_vol_info,
		.get = wm_adc_vol_get,
		.put = wm_adc_vol_put,
		.tlv = { .p = db_scale_volume },
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
		.name = "IEC958 Input Source",
		.info = cs_source_info,
		.get = cs_source_get,
		.put = cs_source_put,
	},
	/* FIXME: which interface? */
	{
		.iface = SNDRV_CTL_ELEM_IFACE_CARD,
		.name = "GPIO Mask",
		.info = pontis_gpio_mask_info,
		.get = pontis_gpio_mask_get,
		.put = pontis_gpio_mask_put,
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_CARD,
		.name = "GPIO Direction",
		.info = pontis_gpio_mask_info,
		.get = pontis_gpio_dir_get,
		.put = pontis_gpio_dir_put,
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_CARD,
		.name = "GPIO Data",
		.info = pontis_gpio_mask_info,
		.get = pontis_gpio_data_get,
		.put = pontis_gpio_data_put,
	},
};


/*
 * WM codec registers
 */
static void wm_proc_regs_write(struct snd_info_entry *entry, struct snd_info_buffer *buffer)
{
	struct snd_ice1712 *ice = entry->private_data;
	char line[64];
	unsigned int reg, val;

	guard(mutex)(&ice->gpio_mutex);
	while (!snd_info_get_line(buffer, line, sizeof(line))) {
		if (sscanf(line, "%x %x", &reg, &val) != 2)
			continue;
		if (reg <= 0x17 && val <= 0xffff)
			wm_put(ice, reg, val);
	}
}

static void wm_proc_regs_read(struct snd_info_entry *entry, struct snd_info_buffer *buffer)
{
	struct snd_ice1712 *ice = entry->private_data;
	int reg, val;

	guard(mutex)(&ice->gpio_mutex);
	for (reg = 0; reg <= 0x17; reg++) {
		val = wm_get(ice, reg);
		snd_iprintf(buffer, "%02x = %04x\n", reg, val);
	}
}

static void wm_proc_init(struct snd_ice1712 *ice)
{
	snd_card_rw_proc_new(ice->card, "wm_codec", ice, wm_proc_regs_read,
			     wm_proc_regs_write);
}

static void cs_proc_regs_read(struct snd_info_entry *entry, struct snd_info_buffer *buffer)
{
	struct snd_ice1712 *ice = entry->private_data;
	int reg, val;

	guard(mutex)(&ice->gpio_mutex);
	for (reg = 0; reg <= 0x26; reg++) {
		val = spi_read(ice, CS_DEV, reg);
		snd_iprintf(buffer, "%02x = %02x\n", reg, val);
	}
	val = spi_read(ice, CS_DEV, 0x7f);
	snd_iprintf(buffer, "%02x = %02x\n", 0x7f, val);
}

static void cs_proc_init(struct snd_ice1712 *ice)
{
	snd_card_ro_proc_new(ice->card, "cs_codec", ice, cs_proc_regs_read);
}


static int pontis_add_controls(struct snd_ice1712 *ice)
{
	unsigned int i;
	int err;

	for (i = 0; i < ARRAY_SIZE(pontis_controls); i++) {
		err = snd_ctl_add(ice->card, snd_ctl_new1(&pontis_controls[i], ice));
		if (err < 0)
			return err;
	}

	wm_proc_init(ice);
	cs_proc_init(ice);

	return 0;
}


/*
 * initialize the chip
 */
static int pontis_init(struct snd_ice1712 *ice)
{
	static const unsigned short wm_inits[] = {
		/* These come first to reduce init pop noise */
		WM_ADC_MUX,	0x00c0,	/* ADC mute */
		WM_DAC_MUTE,	0x0001,	/* DAC softmute */
		WM_DAC_CTRL1,	0x0000,	/* DAC mute */

		WM_POWERDOWN,	0x0008,	/* All power-up except HP */
		WM_RESET,	0x0000,	/* reset */
	};
	static const unsigned short wm_inits2[] = {
		WM_MASTER_CTRL,	0x0022,	/* 256fs, slave mode */
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
		/* WM_DAC_MASTER,	0x0100, */	/* DAC master muted */
		WM_PHASE_SWAP,	0x0000,	/* phase normal */
		WM_DAC_CTRL2,	0x0000,	/* no deemphasis, no ZFLG */
		WM_ADC_ATTEN_L,	0x0000,	/* ADC muted */
		WM_ADC_ATTEN_R,	0x0000,	/* ADC muted */
#if 0
		WM_ALC_CTRL1,	0x007b,	/* */
		WM_ALC_CTRL2,	0x0000,	/* */
		WM_ALC_CTRL3,	0x0000,	/* */
		WM_NOISE_GATE,	0x0000,	/* */
#endif
		WM_DAC_MUTE,	0x0000,	/* DAC unmute */
		WM_ADC_MUX,	0x0003,	/* ADC unmute, both CD/Line On */
	};
	static const unsigned char cs_inits[] = {
		0x04,	0x80,	/* RUN, RXP0 */
		0x05,	0x05,	/* slave, 24bit */
		0x01,	0x00,
		0x02,	0x00,
		0x03,	0x00,
	};
	unsigned int i;

	ice->vt1720 = 1;
	ice->num_total_dacs = 2;
	ice->num_total_adcs = 2;

	/* to remember the register values */
	ice->akm = kzalloc(sizeof(struct snd_akm4xxx), GFP_KERNEL);
	if (! ice->akm)
		return -ENOMEM;
	ice->akm_codecs = 1;

	/* HACK - use this as the SPDIF source.
	 * don't call snd_ice1712_gpio_get/put(), otherwise it's overwritten
	 */
	ice->gpio.saved[0] = 0;

	/* initialize WM8776 codec */
	for (i = 0; i < ARRAY_SIZE(wm_inits); i += 2)
		wm_put(ice, wm_inits[i], wm_inits[i+1]);
	schedule_timeout_uninterruptible(1);
	for (i = 0; i < ARRAY_SIZE(wm_inits2); i += 2)
		wm_put(ice, wm_inits2[i], wm_inits2[i+1]);

	/* initialize CS8416 codec */
	/* assert PRST#; MT05 bit 7 */
	outb(inb(ICEMT1724(ice, AC97_CMD)) | 0x80, ICEMT1724(ice, AC97_CMD));
	mdelay(5);
	/* deassert PRST# */
	outb(inb(ICEMT1724(ice, AC97_CMD)) & ~0x80, ICEMT1724(ice, AC97_CMD));

	for (i = 0; i < ARRAY_SIZE(cs_inits); i += 2)
		spi_write(ice, CS_DEV, cs_inits[i], cs_inits[i+1]);

	return 0;
}


/*
 * Pontis boards don't provide the EEPROM data at all.
 * hence the driver needs to sets up it properly.
 */

static const unsigned char pontis_eeprom[] = {
	[ICE_EEP2_SYSCONF]     = 0x08,	/* clock 256, mpu401, spdif-in/ADC, 1DAC */
	[ICE_EEP2_ACLINK]      = 0x80,	/* I2S */
	[ICE_EEP2_I2S]         = 0xf8,	/* vol, 96k, 24bit, 192k */
	[ICE_EEP2_SPDIF]       = 0xc3,	/* out-en, out-int, spdif-in */
	[ICE_EEP2_GPIO_DIR]    = 0x07,
	[ICE_EEP2_GPIO_DIR1]   = 0x00,
	[ICE_EEP2_GPIO_DIR2]   = 0x00,	/* ignored */
	[ICE_EEP2_GPIO_MASK]   = 0x0f,	/* 4-7 reserved for CS8416 */
	[ICE_EEP2_GPIO_MASK1]  = 0xff,
	[ICE_EEP2_GPIO_MASK2]  = 0x00,	/* ignored */
	[ICE_EEP2_GPIO_STATE]  = 0x06,	/* 0-low, 1-high, 2-high */
	[ICE_EEP2_GPIO_STATE1] = 0x00,
	[ICE_EEP2_GPIO_STATE2] = 0x00,	/* ignored */
};

/* entry point */
struct snd_ice1712_card_info snd_vt1720_pontis_cards[] = {
	{
		.subvendor = VT1720_SUBDEVICE_PONTIS_MS300,
		.name = "Pontis MS300",
		.model = "ms300",
		.chip_init = pontis_init,
		.build_controls = pontis_add_controls,
		.eeprom_size = sizeof(pontis_eeprom),
		.eeprom_data = pontis_eeprom,
	},
	{ } /* terminator */
};
