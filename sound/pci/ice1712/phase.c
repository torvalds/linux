// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *   ALSA driver for ICEnsemble ICE1724 (Envy24)
 *
 *   Lowlevel functions for Terratec PHASE 22
 *
 *	Copyright (c) 2005 Misha Zhilin <misha@epiphan.com>
 */

/* PHASE 22 overview:
 *   Audio controller: VIA Envy24HT-S (slightly trimmed down Envy24HT, 4in/4out)
 *   Analog chip: AK4524 (partially via Philip's 74HCT125)
 *   Digital receiver: CS8414-CS (supported in this release)
 *		PHASE 22 revision 2.0 and Terrasoniq/Musonik TS22PCI have CS8416
 *		(support status unknown, please test and report)
 *
 *   Envy connects to AK4524
 *	- CS directly from GPIO 10
 *	- CCLK via 74HCT125's gate #4 from GPIO 4
 *	- CDTI via 74HCT125's gate #2 from GPIO 5
 *		CDTI may be completely blocked by 74HCT125's gate #1
 *		controlled by GPIO 3
 */

/* PHASE 28 overview:
 *   Audio controller: VIA Envy24HT (full untrimmed version, 4in/8out)
 *   Analog chip: WM8770 (8 channel 192k DAC, 2 channel 96k ADC)
 *   Digital receiver: CS8414-CS (supported in this release)
 */

#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/mutex.h>

#include <sound/core.h>

#include "ice1712.h"
#include "envy24ht.h"
#include "phase.h"
#include <sound/tlv.h>

/* AC97 register cache for Phase28 */
struct phase28_spec {
	unsigned short master[2];
	unsigned short vol[8];
};

/* WM8770 registers */
#define WM_DAC_ATTEN		0x00	/* DAC1-8 analog attenuation */
#define WM_DAC_MASTER_ATTEN	0x08	/* DAC master analog attenuation */
#define WM_DAC_DIG_ATTEN	0x09	/* DAC1-8 digital attenuation */
#define WM_DAC_DIG_MASTER_ATTEN	0x11	/* DAC master digital attenuation */
#define WM_PHASE_SWAP		0x12	/* DAC phase */
#define WM_DAC_CTRL1		0x13	/* DAC control bits */
#define WM_MUTE			0x14	/* mute controls */
#define WM_DAC_CTRL2		0x15	/* de-emphasis and zefo-flag */
#define WM_INT_CTRL		0x16	/* interface control */
#define WM_MASTER		0x17	/* master clock and mode */
#define WM_POWERDOWN		0x18	/* power-down controls */
#define WM_ADC_GAIN		0x19	/* ADC gain L(19)/R(1a) */
#define WM_ADC_MUX		0x1b	/* input MUX */
#define WM_OUT_MUX1		0x1c	/* output MUX */
#define WM_OUT_MUX2		0x1e	/* output MUX */
#define WM_RESET		0x1f	/* software reset */


/*
 * Logarithmic volume values for WM8770
 * Computed as 20 * Log10(255 / x)
 */
static const unsigned char wm_vol[256] = {
	127, 48, 42, 39, 36, 34, 33, 31, 30, 29, 28, 27, 27, 26, 25, 25, 24,
	24, 23, 23, 22, 22, 21, 21, 21, 20, 20, 20, 19, 19, 19, 18, 18, 18, 18,
	17, 17, 17, 17, 16, 16, 16, 16, 15, 15, 15, 15, 15, 15, 14, 14, 14, 14,
	14, 13, 13, 13, 13, 13, 13, 13, 12, 12, 12, 12, 12, 12, 12, 11, 11, 11,
	11, 11, 11, 11, 11, 11, 10, 10, 10, 10, 10, 10, 10, 10, 10, 9, 9, 9, 9,
	9, 9, 9, 9, 9, 9, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 7, 7, 7, 7, 7, 7,
	7, 7, 7, 7, 7, 7, 7, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 5, 5,
	5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
	4, 4, 4, 4, 4, 4, 4, 4, 4, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
	3, 3, 3, 3, 3, 3, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
	2, 2, 2, 2, 2, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

#define WM_VOL_MAX	(sizeof(wm_vol) - 1)
#define WM_VOL_MUTE	0x8000

static const struct snd_akm4xxx akm_phase22 = {
	.type = SND_AK4524,
	.num_dacs = 2,
	.num_adcs = 2,
};

static const struct snd_ak4xxx_private akm_phase22_priv = {
	.caddr =	2,
	.cif =		1,
	.data_mask =	1 << 4,
	.clk_mask =	1 << 5,
	.cs_mask =	1 << 10,
	.cs_addr =	1 << 10,
	.cs_none =	0,
	.add_flags = 	1 << 3,
	.mask_flags =	0,
};

static int phase22_init(struct snd_ice1712 *ice)
{
	struct snd_akm4xxx *ak;
	int err;

	/* Configure DAC/ADC description for generic part of ice1724 */
	switch (ice->eeprom.subvendor) {
	case VT1724_SUBDEVICE_PHASE22:
	case VT1724_SUBDEVICE_TS22:
		ice->num_total_dacs = 2;
		ice->num_total_adcs = 2;
		ice->vt1720 = 1; /* Envy24HT-S have 16 bit wide GPIO */
		break;
	default:
		snd_BUG();
		return -EINVAL;
	}

	/* Initialize analog chips */
	ice->akm = kzalloc(sizeof(struct snd_akm4xxx), GFP_KERNEL);
	ak = ice->akm;
	if (!ak)
		return -ENOMEM;
	ice->akm_codecs = 1;
	switch (ice->eeprom.subvendor) {
	case VT1724_SUBDEVICE_PHASE22:
	case VT1724_SUBDEVICE_TS22:
		err = snd_ice1712_akm4xxx_init(ak, &akm_phase22,
						&akm_phase22_priv, ice);
		if (err < 0)
			return err;
		break;
	}

	return 0;
}

static int phase22_add_controls(struct snd_ice1712 *ice)
{
	int err = 0;

	switch (ice->eeprom.subvendor) {
	case VT1724_SUBDEVICE_PHASE22:
	case VT1724_SUBDEVICE_TS22:
		err = snd_ice1712_akm4xxx_build_controls(ice);
		if (err < 0)
			return err;
	}
	return 0;
}

static const unsigned char phase22_eeprom[] = {
	[ICE_EEP2_SYSCONF]     = 0x28,  /* clock 512, mpu 401,
					spdif-in/1xADC, 1xDACs */
	[ICE_EEP2_ACLINK]      = 0x80,	/* I2S */
	[ICE_EEP2_I2S]         = 0xf0,	/* vol, 96k, 24bit */
	[ICE_EEP2_SPDIF]       = 0xc3,	/* out-en, out-int, spdif-in */
	[ICE_EEP2_GPIO_DIR]    = 0xff,
	[ICE_EEP2_GPIO_DIR1]   = 0xff,
	[ICE_EEP2_GPIO_DIR2]   = 0xff,
	[ICE_EEP2_GPIO_MASK]   = 0x00,
	[ICE_EEP2_GPIO_MASK1]  = 0x00,
	[ICE_EEP2_GPIO_MASK2]  = 0x00,
	[ICE_EEP2_GPIO_STATE]  = 0x00,
	[ICE_EEP2_GPIO_STATE1] = 0x00,
	[ICE_EEP2_GPIO_STATE2] = 0x00,
};

static const unsigned char phase28_eeprom[] = {
	[ICE_EEP2_SYSCONF]     = 0x2b,  /* clock 512, mpu401,
					spdif-in/1xADC, 4xDACs */
	[ICE_EEP2_ACLINK]      = 0x80,	/* I2S */
	[ICE_EEP2_I2S]         = 0xfc,	/* vol, 96k, 24bit, 192k */
	[ICE_EEP2_SPDIF]       = 0xc3,	/* out-en, out-int, spdif-in */
	[ICE_EEP2_GPIO_DIR]    = 0xff,
	[ICE_EEP2_GPIO_DIR1]   = 0xff,
	[ICE_EEP2_GPIO_DIR2]   = 0x5f,
	[ICE_EEP2_GPIO_MASK]   = 0x00,
	[ICE_EEP2_GPIO_MASK1]  = 0x00,
	[ICE_EEP2_GPIO_MASK2]  = 0x00,
	[ICE_EEP2_GPIO_STATE]  = 0x00,
	[ICE_EEP2_GPIO_STATE1] = 0x00,
	[ICE_EEP2_GPIO_STATE2] = 0x00,
};

/*
 * write data in the SPI mode
 */
static void phase28_spi_write(struct snd_ice1712 *ice, unsigned int cs,
				unsigned int data, int bits)
{
	unsigned int tmp;
	int i;

	tmp = snd_ice1712_gpio_read(ice);

	snd_ice1712_gpio_set_mask(ice, ~(PHASE28_WM_RW|PHASE28_SPI_MOSI|
					PHASE28_SPI_CLK|PHASE28_WM_CS));
	tmp |= PHASE28_WM_RW;
	tmp &= ~cs;
	snd_ice1712_gpio_write(ice, tmp);
	udelay(1);

	for (i = bits - 1; i >= 0; i--) {
		tmp &= ~PHASE28_SPI_CLK;
		snd_ice1712_gpio_write(ice, tmp);
		udelay(1);
		if (data & (1 << i))
			tmp |= PHASE28_SPI_MOSI;
		else
			tmp &= ~PHASE28_SPI_MOSI;
		snd_ice1712_gpio_write(ice, tmp);
		udelay(1);
		tmp |= PHASE28_SPI_CLK;
		snd_ice1712_gpio_write(ice, tmp);
		udelay(1);
	}

	tmp &= ~PHASE28_SPI_CLK;
	tmp |= cs;
	snd_ice1712_gpio_write(ice, tmp);
	udelay(1);
	tmp |= PHASE28_SPI_CLK;
	snd_ice1712_gpio_write(ice, tmp);
	udelay(1);
}

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
 * set the register value of WM codec
 */
static void wm_put_nocache(struct snd_ice1712 *ice, int reg, unsigned short val)
{
	phase28_spi_write(ice, PHASE28_WM_CS, (reg << 9) | (val & 0x1ff), 16);
}

/*
 * set the register value of WM codec and remember it
 */
static void wm_put(struct snd_ice1712 *ice, int reg, unsigned short val)
{
	wm_put_nocache(ice, reg, val);
	reg <<= 1;
	ice->akm[0].images[reg] = val >> 8;
	ice->akm[0].images[reg + 1] = val;
}

static void wm_set_vol(struct snd_ice1712 *ice, unsigned int index,
			unsigned short vol, unsigned short master)
{
	unsigned char nvol;

	if ((master & WM_VOL_MUTE) || (vol & WM_VOL_MUTE))
		nvol = 0;
	else
		nvol = 127 - wm_vol[(((vol & ~WM_VOL_MUTE) *
			(master & ~WM_VOL_MUTE)) / 127) & WM_VOL_MAX];

	wm_put(ice, index, nvol);
	wm_put_nocache(ice, index, 0x180 | nvol);
}

/*
 * DAC mute control
 */
#define wm_pcm_mute_info	snd_ctl_boolean_mono_info

static int wm_pcm_mute_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);

	mutex_lock(&ice->gpio_mutex);
	ucontrol->value.integer.value[0] = (wm_get(ice, WM_MUTE) & 0x10) ?
						0 : 1;
	mutex_unlock(&ice->gpio_mutex);
	return 0;
}

static int wm_pcm_mute_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	unsigned short nval, oval;
	int change;

	snd_ice1712_save_gpio_status(ice);
	oval = wm_get(ice, WM_MUTE);
	nval = (oval & ~0x10) | (ucontrol->value.integer.value[0] ? 0 : 0x10);
	change = (nval != oval);
	if (change)
		wm_put(ice, WM_MUTE, nval);
	snd_ice1712_restore_gpio_status(ice);

	return change;
}

/*
 * Master volume attenuation mixer control
 */
static int wm_master_vol_info(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = WM_VOL_MAX;
	return 0;
}

static int wm_master_vol_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	struct phase28_spec *spec = ice->spec;
	int i;
	for (i = 0; i < 2; i++)
		ucontrol->value.integer.value[i] = spec->master[i] &
							~WM_VOL_MUTE;
	return 0;
}

static int wm_master_vol_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	struct phase28_spec *spec = ice->spec;
	int ch, change = 0;

	snd_ice1712_save_gpio_status(ice);
	for (ch = 0; ch < 2; ch++) {
		unsigned int vol = ucontrol->value.integer.value[ch];
		if (vol > WM_VOL_MAX)
			continue;
		vol |= spec->master[ch] & WM_VOL_MUTE;
		if (vol != spec->master[ch]) {
			int dac;
			spec->master[ch] = vol;
			for (dac = 0; dac < ice->num_total_dacs; dac += 2)
				wm_set_vol(ice, WM_DAC_ATTEN + dac + ch,
					   spec->vol[dac + ch],
					   spec->master[ch]);
			change = 1;
		}
	}
	snd_ice1712_restore_gpio_status(ice);
	return change;
}

static int phase28_init(struct snd_ice1712 *ice)
{
	static const unsigned short wm_inits_phase28[] = {
		/* These come first to reduce init pop noise */
		0x1b, 0x044,	/* ADC Mux (AC'97 source) */
		0x1c, 0x00B,	/* Out Mux1 (VOUT1 = DAC+AUX, VOUT2 = DAC) */
		0x1d, 0x009,	/* Out Mux2 (VOUT2 = DAC, VOUT3 = DAC) */

		0x18, 0x000,	/* All power-up */

		0x16, 0x122,	/* I2S, normal polarity, 24bit */
		0x17, 0x022,	/* 256fs, slave mode */
		0x00, 0,	/* DAC1 analog mute */
		0x01, 0,	/* DAC2 analog mute */
		0x02, 0,	/* DAC3 analog mute */
		0x03, 0,	/* DAC4 analog mute */
		0x04, 0,	/* DAC5 analog mute */
		0x05, 0,	/* DAC6 analog mute */
		0x06, 0,	/* DAC7 analog mute */
		0x07, 0,	/* DAC8 analog mute */
		0x08, 0x100,	/* master analog mute */
		0x09, 0xff,	/* DAC1 digital full */
		0x0a, 0xff,	/* DAC2 digital full */
		0x0b, 0xff,	/* DAC3 digital full */
		0x0c, 0xff,	/* DAC4 digital full */
		0x0d, 0xff,	/* DAC5 digital full */
		0x0e, 0xff,	/* DAC6 digital full */
		0x0f, 0xff,	/* DAC7 digital full */
		0x10, 0xff,	/* DAC8 digital full */
		0x11, 0x1ff,	/* master digital full */
		0x12, 0x000,	/* phase normal */
		0x13, 0x090,	/* unmute DAC L/R */
		0x14, 0x000,	/* all unmute */
		0x15, 0x000,	/* no deemphasis, no ZFLG */
		0x19, 0x000,	/* -12dB ADC/L */
		0x1a, 0x000,	/* -12dB ADC/R */
		(unsigned short)-1
	};

	unsigned int tmp;
	struct snd_akm4xxx *ak;
	struct phase28_spec *spec;
	const unsigned short *p;
	int i;

	ice->num_total_dacs = 8;
	ice->num_total_adcs = 2;

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec)
		return -ENOMEM;
	ice->spec = spec;

	/* Initialize analog chips */
	ice->akm = kzalloc(sizeof(struct snd_akm4xxx), GFP_KERNEL);
	ak = ice->akm;
	if (!ak)
		return -ENOMEM;
	ice->akm_codecs = 1;

	snd_ice1712_gpio_set_dir(ice, 0x5fffff); /* fix this for time being */

	/* reset the wm codec as the SPI mode */
	snd_ice1712_save_gpio_status(ice);
	snd_ice1712_gpio_set_mask(ice, ~(PHASE28_WM_RESET|PHASE28_WM_CS|
					PHASE28_HP_SEL));

	tmp = snd_ice1712_gpio_read(ice);
	tmp &= ~PHASE28_WM_RESET;
	snd_ice1712_gpio_write(ice, tmp);
	udelay(1);
	tmp |= PHASE28_WM_CS;
	snd_ice1712_gpio_write(ice, tmp);
	udelay(1);
	tmp |= PHASE28_WM_RESET;
	snd_ice1712_gpio_write(ice, tmp);
	udelay(1);

	p = wm_inits_phase28;
	for (; *p != (unsigned short)-1; p += 2)
		wm_put(ice, p[0], p[1]);

	snd_ice1712_restore_gpio_status(ice);

	spec->master[0] = WM_VOL_MUTE;
	spec->master[1] = WM_VOL_MUTE;
	for (i = 0; i < ice->num_total_dacs; i++) {
		spec->vol[i] = WM_VOL_MUTE;
		wm_set_vol(ice, i, spec->vol[i], spec->master[i % 2]);
	}

	return 0;
}

/*
 * DAC volume attenuation mixer control
 */
static int wm_vol_info(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_info *uinfo)
{
	int voices = kcontrol->private_value >> 8;
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = voices;
	uinfo->value.integer.min = 0;		/* mute (-101dB) */
	uinfo->value.integer.max = 0x7F;	/* 0dB */
	return 0;
}

static int wm_vol_get(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	struct phase28_spec *spec = ice->spec;
	int i, ofs, voices;

	voices = kcontrol->private_value >> 8;
	ofs = kcontrol->private_value & 0xff;
	for (i = 0; i < voices; i++)
		ucontrol->value.integer.value[i] =
			spec->vol[ofs+i] & ~WM_VOL_MUTE;
	return 0;
}

static int wm_vol_put(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	struct phase28_spec *spec = ice->spec;
	int i, idx, ofs, voices;
	int change = 0;

	voices = kcontrol->private_value >> 8;
	ofs = kcontrol->private_value & 0xff;
	snd_ice1712_save_gpio_status(ice);
	for (i = 0; i < voices; i++) {
		unsigned int vol;
		vol = ucontrol->value.integer.value[i];
		if (vol > 0x7f)
			continue;
		vol |= spec->vol[ofs+i] & WM_VOL_MUTE;
		if (vol != spec->vol[ofs+i]) {
			spec->vol[ofs+i] = vol;
			idx  = WM_DAC_ATTEN + ofs + i;
			wm_set_vol(ice, idx, spec->vol[ofs+i],
				   spec->master[i]);
			change = 1;
		}
	}
	snd_ice1712_restore_gpio_status(ice);
	return change;
}

/*
 * WM8770 mute control
 */
static int wm_mute_info(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_info *uinfo) {
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = kcontrol->private_value >> 8;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;
	return 0;
}

static int wm_mute_get(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	struct phase28_spec *spec = ice->spec;
	int voices, ofs, i;

	voices = kcontrol->private_value >> 8;
	ofs = kcontrol->private_value & 0xFF;

	for (i = 0; i < voices; i++)
		ucontrol->value.integer.value[i] =
			(spec->vol[ofs+i] & WM_VOL_MUTE) ? 0 : 1;
	return 0;
}

static int wm_mute_put(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	struct phase28_spec *spec = ice->spec;
	int change = 0, voices, ofs, i;

	voices = kcontrol->private_value >> 8;
	ofs = kcontrol->private_value & 0xFF;

	snd_ice1712_save_gpio_status(ice);
	for (i = 0; i < voices; i++) {
		int val = (spec->vol[ofs + i] & WM_VOL_MUTE) ? 0 : 1;
		if (ucontrol->value.integer.value[i] != val) {
			spec->vol[ofs + i] &= ~WM_VOL_MUTE;
			spec->vol[ofs + i] |=
				ucontrol->value.integer.value[i] ? 0 :
				WM_VOL_MUTE;
			wm_set_vol(ice, ofs + i, spec->vol[ofs + i],
					spec->master[i]);
			change = 1;
		}
	}
	snd_ice1712_restore_gpio_status(ice);

	return change;
}

/*
 * WM8770 master mute control
 */
#define wm_master_mute_info		snd_ctl_boolean_stereo_info

static int wm_master_mute_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	struct phase28_spec *spec = ice->spec;

	ucontrol->value.integer.value[0] =
		(spec->master[0] & WM_VOL_MUTE) ? 0 : 1;
	ucontrol->value.integer.value[1] =
		(spec->master[1] & WM_VOL_MUTE) ? 0 : 1;
	return 0;
}

static int wm_master_mute_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	struct phase28_spec *spec = ice->spec;
	int change = 0, i;

	snd_ice1712_save_gpio_status(ice);
	for (i = 0; i < 2; i++) {
		int val = (spec->master[i] & WM_VOL_MUTE) ? 0 : 1;
		if (ucontrol->value.integer.value[i] != val) {
			int dac;
			spec->master[i] &= ~WM_VOL_MUTE;
			spec->master[i] |=
				ucontrol->value.integer.value[i] ? 0 :
				WM_VOL_MUTE;
			for (dac = 0; dac < ice->num_total_dacs; dac += 2)
				wm_set_vol(ice, WM_DAC_ATTEN + dac + i,
						spec->vol[dac + i],
						spec->master[i]);
			change = 1;
		}
	}
	snd_ice1712_restore_gpio_status(ice);

	return change;
}

/* digital master volume */
#define PCM_0dB 0xff
#define PCM_RES 128	/* -64dB */
#define PCM_MIN (PCM_0dB - PCM_RES)
static int wm_pcm_vol_info(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;		/* mute (-64dB) */
	uinfo->value.integer.max = PCM_RES;	/* 0dB */
	return 0;
}

static int wm_pcm_vol_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	unsigned short val;

	mutex_lock(&ice->gpio_mutex);
	val = wm_get(ice, WM_DAC_DIG_MASTER_ATTEN) & 0xff;
	val = val > PCM_MIN ? (val - PCM_MIN) : 0;
	ucontrol->value.integer.value[0] = val;
	mutex_unlock(&ice->gpio_mutex);
	return 0;
}

static int wm_pcm_vol_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	unsigned short ovol, nvol;
	int change = 0;

	nvol = ucontrol->value.integer.value[0];
	if (nvol > PCM_RES)
		return -EINVAL;
	snd_ice1712_save_gpio_status(ice);
	nvol = (nvol ? (nvol + PCM_MIN) : 0) & 0xff;
	ovol = wm_get(ice, WM_DAC_DIG_MASTER_ATTEN) & 0xff;
	if (ovol != nvol) {
		wm_put(ice, WM_DAC_DIG_MASTER_ATTEN, nvol); /* prelatch */
		/* update */
		wm_put_nocache(ice, WM_DAC_DIG_MASTER_ATTEN, nvol | 0x100);
		change = 1;
	}
	snd_ice1712_restore_gpio_status(ice);
	return change;
}

/*
 * Deemphasis
 */
#define phase28_deemp_info	snd_ctl_boolean_mono_info

static int phase28_deemp_get(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	ucontrol->value.integer.value[0] = (wm_get(ice, WM_DAC_CTRL2) & 0xf) ==
						0xf;
	return 0;
}

static int phase28_deemp_put(struct snd_kcontrol *kcontrol,
				struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	int temp, temp2;
	temp = wm_get(ice, WM_DAC_CTRL2);
	temp2 = temp;
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
static int phase28_oversampling_info(struct snd_kcontrol *k,
					struct snd_ctl_elem_info *uinfo)
{
	static const char * const texts[2] = { "128x", "64x"	};

	return snd_ctl_enum_info(uinfo, 1, 2, texts);
}

static int phase28_oversampling_get(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	ucontrol->value.enumerated.item[0] = (wm_get(ice, WM_MASTER) & 0x8) ==
						0x8;
	return 0;
}

static int phase28_oversampling_put(struct snd_kcontrol *kcontrol,
					struct snd_ctl_elem_value *ucontrol)
{
	int temp, temp2;
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);

	temp = wm_get(ice, WM_MASTER);
	temp2 = temp;

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

static const DECLARE_TLV_DB_SCALE(db_scale_wm_dac, -12700, 100, 1);
static const DECLARE_TLV_DB_SCALE(db_scale_wm_pcm, -6400, 50, 1);

static const struct snd_kcontrol_new phase28_dac_controls[] = {
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Master Playback Switch",
		.info = wm_master_mute_info,
		.get = wm_master_mute_get,
		.put = wm_master_mute_put
	},
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
		.name = "Front Playback Switch",
		.info = wm_mute_info,
		.get = wm_mute_get,
		.put = wm_mute_put,
		.private_value = (2 << 8) | 0
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.access = (SNDRV_CTL_ELEM_ACCESS_READWRITE |
			   SNDRV_CTL_ELEM_ACCESS_TLV_READ),
		.name = "Front Playback Volume",
		.info = wm_vol_info,
		.get = wm_vol_get,
		.put = wm_vol_put,
		.private_value = (2 << 8) | 0,
		.tlv = { .p = db_scale_wm_dac }
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Rear Playback Switch",
		.info = wm_mute_info,
		.get = wm_mute_get,
		.put = wm_mute_put,
		.private_value = (2 << 8) | 2
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.access = (SNDRV_CTL_ELEM_ACCESS_READWRITE |
			   SNDRV_CTL_ELEM_ACCESS_TLV_READ),
		.name = "Rear Playback Volume",
		.info = wm_vol_info,
		.get = wm_vol_get,
		.put = wm_vol_put,
		.private_value = (2 << 8) | 2,
		.tlv = { .p = db_scale_wm_dac }
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Center Playback Switch",
		.info = wm_mute_info,
		.get = wm_mute_get,
		.put = wm_mute_put,
		.private_value = (1 << 8) | 4
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.access = (SNDRV_CTL_ELEM_ACCESS_READWRITE |
			   SNDRV_CTL_ELEM_ACCESS_TLV_READ),
		.name = "Center Playback Volume",
		.info = wm_vol_info,
		.get = wm_vol_get,
		.put = wm_vol_put,
		.private_value = (1 << 8) | 4,
		.tlv = { .p = db_scale_wm_dac }
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "LFE Playback Switch",
		.info = wm_mute_info,
		.get = wm_mute_get,
		.put = wm_mute_put,
		.private_value = (1 << 8) | 5
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.access = (SNDRV_CTL_ELEM_ACCESS_READWRITE |
			   SNDRV_CTL_ELEM_ACCESS_TLV_READ),
		.name = "LFE Playback Volume",
		.info = wm_vol_info,
		.get = wm_vol_get,
		.put = wm_vol_put,
		.private_value = (1 << 8) | 5,
		.tlv = { .p = db_scale_wm_dac }
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Side Playback Switch",
		.info = wm_mute_info,
		.get = wm_mute_get,
		.put = wm_mute_put,
		.private_value = (2 << 8) | 6
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.access = (SNDRV_CTL_ELEM_ACCESS_READWRITE |
			   SNDRV_CTL_ELEM_ACCESS_TLV_READ),
		.name = "Side Playback Volume",
		.info = wm_vol_info,
		.get = wm_vol_get,
		.put = wm_vol_put,
		.private_value = (2 << 8) | 6,
		.tlv = { .p = db_scale_wm_dac }
	}
};

static const struct snd_kcontrol_new wm_controls[] = {
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "PCM Playback Switch",
		.info = wm_pcm_mute_info,
		.get = wm_pcm_mute_get,
		.put = wm_pcm_mute_put
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.access = (SNDRV_CTL_ELEM_ACCESS_READWRITE |
			   SNDRV_CTL_ELEM_ACCESS_TLV_READ),
		.name = "PCM Playback Volume",
		.info = wm_pcm_vol_info,
		.get = wm_pcm_vol_get,
		.put = wm_pcm_vol_put,
		.tlv = { .p = db_scale_wm_pcm }
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "DAC Deemphasis Switch",
		.info = phase28_deemp_info,
		.get = phase28_deemp_get,
		.put = phase28_deemp_put
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "ADC Oversampling",
		.info = phase28_oversampling_info,
		.get = phase28_oversampling_get,
		.put = phase28_oversampling_put
	}
};

static int phase28_add_controls(struct snd_ice1712 *ice)
{
	unsigned int i, counts;
	int err;

	counts = ARRAY_SIZE(phase28_dac_controls);
	for (i = 0; i < counts; i++) {
		err = snd_ctl_add(ice->card,
					snd_ctl_new1(&phase28_dac_controls[i],
							ice));
		if (err < 0)
			return err;
	}

	for (i = 0; i < ARRAY_SIZE(wm_controls); i++) {
		err = snd_ctl_add(ice->card,
					snd_ctl_new1(&wm_controls[i], ice));
		if (err < 0)
			return err;
	}

	return 0;
}

struct snd_ice1712_card_info snd_vt1724_phase_cards[] = {
	{
		.subvendor = VT1724_SUBDEVICE_PHASE22,
		.name = "Terratec PHASE 22",
		.model = "phase22",
		.chip_init = phase22_init,
		.build_controls = phase22_add_controls,
		.eeprom_size = sizeof(phase22_eeprom),
		.eeprom_data = phase22_eeprom,
	},
	{
		.subvendor = VT1724_SUBDEVICE_PHASE28,
		.name = "Terratec PHASE 28",
		.model = "phase28",
		.chip_init = phase28_init,
		.build_controls = phase28_add_controls,
		.eeprom_size = sizeof(phase28_eeprom),
		.eeprom_data = phase28_eeprom,
	},
	{
		.subvendor = VT1724_SUBDEVICE_TS22,
		.name = "Terrasoniq TS22 PCI",
		.model = "TS22",
		.chip_init = phase22_init,
		.build_controls = phase22_add_controls,
		.eeprom_size = sizeof(phase22_eeprom),
		.eeprom_data = phase22_eeprom,
	},
	{ } /* terminator */
};
