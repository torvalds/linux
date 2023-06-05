// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *   ALSA driver for ICEnsemble VT1724 (Envy24HT)
 *
 *   Lowlevel functions for Terratec Aureon cards
 *
 *	Copyright (c) 2003 Takashi Iwai <tiwai@suse.de>
 *
 * NOTES:
 *
 * - we reuse the struct snd_akm4xxx record for storing the wm8770 codec data.
 *   both wm and akm codecs are pretty similar, so we can integrate
 *   both controls in the future, once if wm codecs are reused in
 *   many boards.
 *
 * - DAC digital volumes are not implemented in the mixer.
 *   if they show better response than DAC analog volumes, we can use them
 *   instead.
 *
 *   Lowlevel functions for AudioTrak Prodigy 7.1 (and possibly 192) cards
 *      Copyright (c) 2003 Dimitromanolakis Apostolos <apostol@cs.utoronto.ca>
 *
 *   version 0.82: Stable / not all features work yet (no communication with AC97 secondary)
 *       added 64x/128x oversampling switch (should be 64x only for 96khz)
 *       fixed some recording labels (still need to check the rest)
 *       recording is working probably thanks to correct wm8770 initialization
 *
 *   version 0.5: Initial release:
 *           working: analog output, mixer, headphone amplifier switch
 *       not working: prety much everything else, at least i could verify that
 *                    we have no digital output, no capture, pretty bad clicks and poops
 *                    on mixer switch and other coll stuff.
 */

#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/mutex.h>

#include <sound/core.h>

#include "ice1712.h"
#include "envy24ht.h"
#include "aureon.h"
#include <sound/tlv.h>

/* AC97 register cache for Aureon */
struct aureon_spec {
	unsigned short stac9744[64];
	unsigned int cs8415_mux;
	unsigned short master[2];
	unsigned short vol[8];
	unsigned char pca9554_out;
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

/* CS8415A registers */
#define CS8415_CTRL1	0x01
#define CS8415_CTRL2	0x02
#define CS8415_QSUB		0x14
#define CS8415_RATIO	0x1E
#define CS8415_C_BUFFER	0x20
#define CS8415_ID		0x7F

/* PCA9554 registers */
#define PCA9554_DEV     0x40            /* I2C device address */
#define PCA9554_IN      0x00            /* input port */
#define PCA9554_OUT     0x01            /* output port */
#define PCA9554_INVERT  0x02            /* input invert */
#define PCA9554_DIR     0x03            /* port directions */

/*
 * Aureon Universe additional controls using PCA9554
 */

/*
 * Send data to pca9554
 */
static void aureon_pca9554_write(struct snd_ice1712 *ice, unsigned char reg,
				 unsigned char data)
{
	unsigned int tmp;
	int i, j;
	unsigned char dev = PCA9554_DEV;  /* ID 0100000, write */
	unsigned char val = 0;

	tmp = snd_ice1712_gpio_read(ice);

	snd_ice1712_gpio_set_mask(ice, ~(AUREON_SPI_MOSI|AUREON_SPI_CLK|
					 AUREON_WM_RW|AUREON_WM_CS|
					 AUREON_CS8415_CS));
	tmp |= AUREON_WM_RW;
	tmp |= AUREON_CS8415_CS | AUREON_WM_CS; /* disable SPI devices */

	tmp &= ~AUREON_SPI_MOSI;
	tmp &= ~AUREON_SPI_CLK;
	snd_ice1712_gpio_write(ice, tmp);
	udelay(50);

	/*
	 * send i2c stop condition and start condition
	 * to obtain sane state
	 */
	tmp |= AUREON_SPI_CLK;
	snd_ice1712_gpio_write(ice, tmp);
	udelay(50);
	tmp |= AUREON_SPI_MOSI;
	snd_ice1712_gpio_write(ice, tmp);
	udelay(100);
	tmp &= ~AUREON_SPI_MOSI;
	snd_ice1712_gpio_write(ice, tmp);
	udelay(50);
	tmp &= ~AUREON_SPI_CLK;
	snd_ice1712_gpio_write(ice, tmp);
	udelay(100);
	/*
	 * send device address, command and value,
	 * skipping ack cycles in between
	 */
	for (j = 0; j < 3; j++) {
		switch (j) {
		case 0:
			val = dev;
			break;
		case 1:
			val = reg;
			break;
		case 2:
			val = data;
			break;
		}
		for (i = 7; i >= 0; i--) {
			tmp &= ~AUREON_SPI_CLK;
			snd_ice1712_gpio_write(ice, tmp);
			udelay(40);
			if (val & (1 << i))
				tmp |= AUREON_SPI_MOSI;
			else
				tmp &= ~AUREON_SPI_MOSI;
			snd_ice1712_gpio_write(ice, tmp);
			udelay(40);
			tmp |= AUREON_SPI_CLK;
			snd_ice1712_gpio_write(ice, tmp);
			udelay(40);
		}
		tmp &= ~AUREON_SPI_CLK;
		snd_ice1712_gpio_write(ice, tmp);
		udelay(40);
		tmp |= AUREON_SPI_CLK;
		snd_ice1712_gpio_write(ice, tmp);
		udelay(40);
		tmp &= ~AUREON_SPI_CLK;
		snd_ice1712_gpio_write(ice, tmp);
		udelay(40);
	}
	tmp &= ~AUREON_SPI_CLK;
	snd_ice1712_gpio_write(ice, tmp);
	udelay(40);
	tmp &= ~AUREON_SPI_MOSI;
	snd_ice1712_gpio_write(ice, tmp);
	udelay(40);
	tmp |= AUREON_SPI_CLK;
	snd_ice1712_gpio_write(ice, tmp);
	udelay(50);
	tmp |= AUREON_SPI_MOSI;
	snd_ice1712_gpio_write(ice, tmp);
	udelay(100);
}

static int aureon_universe_inmux_info(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_info *uinfo)
{
	static const char * const texts[3] =
		{"Internal Aux", "Wavetable", "Rear Line-In"};

	return snd_ctl_enum_info(uinfo, 1, 3, texts);
}

static int aureon_universe_inmux_get(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	struct aureon_spec *spec = ice->spec;
	ucontrol->value.enumerated.item[0] = spec->pca9554_out;
	return 0;
}

static int aureon_universe_inmux_put(struct snd_kcontrol *kcontrol,
				     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	struct aureon_spec *spec = ice->spec;
	unsigned char oval, nval;
	int change;

	nval = ucontrol->value.enumerated.item[0];
	if (nval >= 3)
		return -EINVAL;
	snd_ice1712_save_gpio_status(ice);
	oval = spec->pca9554_out;
	change = (oval != nval);
	if (change) {
		aureon_pca9554_write(ice, PCA9554_OUT, nval);
		spec->pca9554_out = nval;
	}
	snd_ice1712_restore_gpio_status(ice);
	return change;
}


static void aureon_ac97_write(struct snd_ice1712 *ice, unsigned short reg,
			      unsigned short val)
{
	struct aureon_spec *spec = ice->spec;
	unsigned int tmp;

	/* Send address to XILINX chip */
	tmp = (snd_ice1712_gpio_read(ice) & ~0xFF) | (reg & 0x7F);
	snd_ice1712_gpio_write(ice, tmp);
	udelay(10);
	tmp |= AUREON_AC97_ADDR;
	snd_ice1712_gpio_write(ice, tmp);
	udelay(10);
	tmp &= ~AUREON_AC97_ADDR;
	snd_ice1712_gpio_write(ice, tmp);
	udelay(10);

	/* Send low-order byte to XILINX chip */
	tmp &= ~AUREON_AC97_DATA_MASK;
	tmp |= val & AUREON_AC97_DATA_MASK;
	snd_ice1712_gpio_write(ice, tmp);
	udelay(10);
	tmp |= AUREON_AC97_DATA_LOW;
	snd_ice1712_gpio_write(ice, tmp);
	udelay(10);
	tmp &= ~AUREON_AC97_DATA_LOW;
	snd_ice1712_gpio_write(ice, tmp);
	udelay(10);

	/* Send high-order byte to XILINX chip */
	tmp &= ~AUREON_AC97_DATA_MASK;
	tmp |= (val >> 8) & AUREON_AC97_DATA_MASK;

	snd_ice1712_gpio_write(ice, tmp);
	udelay(10);
	tmp |= AUREON_AC97_DATA_HIGH;
	snd_ice1712_gpio_write(ice, tmp);
	udelay(10);
	tmp &= ~AUREON_AC97_DATA_HIGH;
	snd_ice1712_gpio_write(ice, tmp);
	udelay(10);

	/* Instruct XILINX chip to parse the data to the STAC9744 chip */
	tmp |= AUREON_AC97_COMMIT;
	snd_ice1712_gpio_write(ice, tmp);
	udelay(10);
	tmp &= ~AUREON_AC97_COMMIT;
	snd_ice1712_gpio_write(ice, tmp);
	udelay(10);

	/* Store the data in out private buffer */
	spec->stac9744[(reg & 0x7F) >> 1] = val;
}

static unsigned short aureon_ac97_read(struct snd_ice1712 *ice, unsigned short reg)
{
	struct aureon_spec *spec = ice->spec;
	return spec->stac9744[(reg & 0x7F) >> 1];
}

/*
 * Initialize STAC9744 chip
 */
static int aureon_ac97_init(struct snd_ice1712 *ice)
{
	struct aureon_spec *spec = ice->spec;
	int i;
	static const unsigned short ac97_defaults[] = {
		0x00, 0x9640,
		0x02, 0x8000,
		0x04, 0x8000,
		0x06, 0x8000,
		0x0C, 0x8008,
		0x0E, 0x8008,
		0x10, 0x8808,
		0x12, 0x8808,
		0x14, 0x8808,
		0x16, 0x8808,
		0x18, 0x8808,
		0x1C, 0x8000,
		0x26, 0x000F,
		0x28, 0x0201,
		0x2C, 0xBB80,
		0x32, 0xBB80,
		0x7C, 0x8384,
		0x7E, 0x7644,
		(unsigned short)-1
	};
	unsigned int tmp;

	/* Cold reset */
	tmp = (snd_ice1712_gpio_read(ice) | AUREON_AC97_RESET) & ~AUREON_AC97_DATA_MASK;
	snd_ice1712_gpio_write(ice, tmp);
	udelay(3);

	tmp &= ~AUREON_AC97_RESET;
	snd_ice1712_gpio_write(ice, tmp);
	udelay(3);

	tmp |= AUREON_AC97_RESET;
	snd_ice1712_gpio_write(ice, tmp);
	udelay(3);

	memset(&spec->stac9744, 0, sizeof(spec->stac9744));
	for (i = 0; ac97_defaults[i] != (unsigned short)-1; i += 2)
		spec->stac9744[(ac97_defaults[i]) >> 1] = ac97_defaults[i+1];

	/* Unmute AC'97 master volume permanently - muting is done by WM8770 */
	aureon_ac97_write(ice, AC97_MASTER, 0x0000);

	return 0;
}

#define AUREON_AC97_STEREO	0x80

/*
 * AC'97 volume controls
 */
static int aureon_ac97_vol_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = kcontrol->private_value & AUREON_AC97_STEREO ? 2 : 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 31;
	return 0;
}

static int aureon_ac97_vol_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	unsigned short vol;

	mutex_lock(&ice->gpio_mutex);

	vol = aureon_ac97_read(ice, kcontrol->private_value & 0x7F);
	ucontrol->value.integer.value[0] = 0x1F - (vol & 0x1F);
	if (kcontrol->private_value & AUREON_AC97_STEREO)
		ucontrol->value.integer.value[1] = 0x1F - ((vol >> 8) & 0x1F);

	mutex_unlock(&ice->gpio_mutex);
	return 0;
}

static int aureon_ac97_vol_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	unsigned short ovol, nvol;
	int change;

	snd_ice1712_save_gpio_status(ice);

	ovol = aureon_ac97_read(ice, kcontrol->private_value & 0x7F);
	nvol = (0x1F - ucontrol->value.integer.value[0]) & 0x001F;
	if (kcontrol->private_value & AUREON_AC97_STEREO)
		nvol |= ((0x1F - ucontrol->value.integer.value[1]) << 8) & 0x1F00;
	nvol |= ovol & ~0x1F1F;

	change = (ovol != nvol);
	if (change)
		aureon_ac97_write(ice, kcontrol->private_value & 0x7F, nvol);

	snd_ice1712_restore_gpio_status(ice);

	return change;
}

/*
 * AC'97 mute controls
 */
#define aureon_ac97_mute_info	snd_ctl_boolean_mono_info

static int aureon_ac97_mute_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);

	mutex_lock(&ice->gpio_mutex);

	ucontrol->value.integer.value[0] = aureon_ac97_read(ice,
			kcontrol->private_value & 0x7F) & 0x8000 ? 0 : 1;

	mutex_unlock(&ice->gpio_mutex);
	return 0;
}

static int aureon_ac97_mute_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	unsigned short ovol, nvol;
	int change;

	snd_ice1712_save_gpio_status(ice);

	ovol = aureon_ac97_read(ice, kcontrol->private_value & 0x7F);
	nvol = (ucontrol->value.integer.value[0] ? 0x0000 : 0x8000) | (ovol & ~0x8000);

	change = (ovol != nvol);
	if (change)
		aureon_ac97_write(ice, kcontrol->private_value & 0x7F, nvol);

	snd_ice1712_restore_gpio_status(ice);

	return change;
}

/*
 * AC'97 mute controls
 */
#define aureon_ac97_micboost_info	snd_ctl_boolean_mono_info

static int aureon_ac97_micboost_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);

	mutex_lock(&ice->gpio_mutex);

	ucontrol->value.integer.value[0] = aureon_ac97_read(ice, AC97_MIC) & 0x0020 ? 0 : 1;

	mutex_unlock(&ice->gpio_mutex);
	return 0;
}

static int aureon_ac97_micboost_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	unsigned short ovol, nvol;
	int change;

	snd_ice1712_save_gpio_status(ice);

	ovol = aureon_ac97_read(ice, AC97_MIC);
	nvol = (ucontrol->value.integer.value[0] ? 0x0000 : 0x0020) | (ovol & ~0x0020);

	change = (ovol != nvol);
	if (change)
		aureon_ac97_write(ice, AC97_MIC, nvol);

	snd_ice1712_restore_gpio_status(ice);

	return change;
}

/*
 * write data in the SPI mode
 */
static void aureon_spi_write(struct snd_ice1712 *ice, unsigned int cs, unsigned int data, int bits)
{
	unsigned int tmp;
	int i;
	unsigned int mosi, clk;

	tmp = snd_ice1712_gpio_read(ice);

	if (ice->eeprom.subvendor == VT1724_SUBDEVICE_PRODIGY71LT ||
	    ice->eeprom.subvendor == VT1724_SUBDEVICE_PRODIGY71XT) {
		snd_ice1712_gpio_set_mask(ice, ~(PRODIGY_SPI_MOSI|PRODIGY_SPI_CLK|PRODIGY_WM_CS));
		mosi = PRODIGY_SPI_MOSI;
		clk = PRODIGY_SPI_CLK;
	} else {
		snd_ice1712_gpio_set_mask(ice, ~(AUREON_WM_RW|AUREON_SPI_MOSI|AUREON_SPI_CLK|
						 AUREON_WM_CS|AUREON_CS8415_CS));
		mosi = AUREON_SPI_MOSI;
		clk = AUREON_SPI_CLK;

		tmp |= AUREON_WM_RW;
	}

	tmp &= ~cs;
	snd_ice1712_gpio_write(ice, tmp);
	udelay(1);

	for (i = bits - 1; i >= 0; i--) {
		tmp &= ~clk;
		snd_ice1712_gpio_write(ice, tmp);
		udelay(1);
		if (data & (1 << i))
			tmp |= mosi;
		else
			tmp &= ~mosi;
		snd_ice1712_gpio_write(ice, tmp);
		udelay(1);
		tmp |= clk;
		snd_ice1712_gpio_write(ice, tmp);
		udelay(1);
	}

	tmp &= ~clk;
	tmp |= cs;
	snd_ice1712_gpio_write(ice, tmp);
	udelay(1);
	tmp |= clk;
	snd_ice1712_gpio_write(ice, tmp);
	udelay(1);
}

/*
 * Read data in SPI mode
 */
static void aureon_spi_read(struct snd_ice1712 *ice, unsigned int cs,
		unsigned int data, int bits, unsigned char *buffer, int size)
{
	int i, j;
	unsigned int tmp;

	tmp = (snd_ice1712_gpio_read(ice) & ~AUREON_SPI_CLK) | AUREON_CS8415_CS|AUREON_WM_CS;
	snd_ice1712_gpio_write(ice, tmp);
	tmp &= ~cs;
	snd_ice1712_gpio_write(ice, tmp);
	udelay(1);

	for (i = bits-1; i >= 0; i--) {
		if (data & (1 << i))
			tmp |= AUREON_SPI_MOSI;
		else
			tmp &= ~AUREON_SPI_MOSI;
		snd_ice1712_gpio_write(ice, tmp);
		udelay(1);

		tmp |= AUREON_SPI_CLK;
		snd_ice1712_gpio_write(ice, tmp);
		udelay(1);

		tmp &= ~AUREON_SPI_CLK;
		snd_ice1712_gpio_write(ice, tmp);
		udelay(1);
	}

	for (j = 0; j < size; j++) {
		unsigned char outdata = 0;
		for (i = 7; i >= 0; i--) {
			tmp = snd_ice1712_gpio_read(ice);
			outdata <<= 1;
			outdata |= (tmp & AUREON_SPI_MISO) ? 1 : 0;
			udelay(1);

			tmp |= AUREON_SPI_CLK;
			snd_ice1712_gpio_write(ice, tmp);
			udelay(1);

			tmp &= ~AUREON_SPI_CLK;
			snd_ice1712_gpio_write(ice, tmp);
			udelay(1);
		}
		buffer[j] = outdata;
	}

	tmp |= cs;
	snd_ice1712_gpio_write(ice, tmp);
}

static unsigned char aureon_cs8415_get(struct snd_ice1712 *ice, int reg)
{
	unsigned char val;
	aureon_spi_write(ice, AUREON_CS8415_CS, 0x2000 | reg, 16);
	aureon_spi_read(ice, AUREON_CS8415_CS, 0x21, 8, &val, 1);
	return val;
}

static void aureon_cs8415_read(struct snd_ice1712 *ice, int reg,
				unsigned char *buffer, int size)
{
	aureon_spi_write(ice, AUREON_CS8415_CS, 0x2000 | reg, 16);
	aureon_spi_read(ice, AUREON_CS8415_CS, 0x21, 8, buffer, size);
}

static void aureon_cs8415_put(struct snd_ice1712 *ice, int reg,
						unsigned char val)
{
	aureon_spi_write(ice, AUREON_CS8415_CS, 0x200000 | (reg << 8) | val, 24);
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
	aureon_spi_write(ice,
			 ((ice->eeprom.subvendor == VT1724_SUBDEVICE_PRODIGY71LT ||
			   ice->eeprom.subvendor == VT1724_SUBDEVICE_PRODIGY71XT) ?
			 PRODIGY_WM_CS : AUREON_WM_CS),
			(reg << 9) | (val & 0x1ff), 16);
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

/*
 */
#define aureon_mono_bool_info		snd_ctl_boolean_mono_info

/*
 * AC'97 master playback mute controls (Mute on WM8770 chip)
 */
#define aureon_ac97_mmute_info		snd_ctl_boolean_mono_info

static int aureon_ac97_mmute_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);

	mutex_lock(&ice->gpio_mutex);

	ucontrol->value.integer.value[0] = (wm_get(ice, WM_OUT_MUX1) >> 1) & 0x01;

	mutex_unlock(&ice->gpio_mutex);
	return 0;
}

static int aureon_ac97_mmute_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	unsigned short ovol, nvol;
	int change;

	snd_ice1712_save_gpio_status(ice);

	ovol = wm_get(ice, WM_OUT_MUX1);
	nvol = (ovol & ~0x02) | (ucontrol->value.integer.value[0] ? 0x02 : 0x00);
	change = (ovol != nvol);
	if (change)
		wm_put(ice, WM_OUT_MUX1, nvol);

	snd_ice1712_restore_gpio_status(ice);

	return change;
}

static const DECLARE_TLV_DB_SCALE(db_scale_wm_dac, -10000, 100, 1);
static const DECLARE_TLV_DB_SCALE(db_scale_wm_pcm, -6400, 50, 1);
static const DECLARE_TLV_DB_SCALE(db_scale_wm_adc, -1200, 100, 0);
static const DECLARE_TLV_DB_SCALE(db_scale_ac97_master, -4650, 150, 0);
static const DECLARE_TLV_DB_SCALE(db_scale_ac97_gain, -3450, 150, 0);

#define WM_VOL_MAX	100
#define WM_VOL_CNT	101	/* 0dB .. -100dB */
#define WM_VOL_MUTE	0x8000

static void wm_set_vol(struct snd_ice1712 *ice, unsigned int index, unsigned short vol, unsigned short master)
{
	unsigned char nvol;

	if ((master & WM_VOL_MUTE) || (vol & WM_VOL_MUTE)) {
		nvol = 0;
	} else {
		nvol = ((vol % WM_VOL_CNT) * (master % WM_VOL_CNT)) /
								WM_VOL_MAX;
		nvol += 0x1b;
	}

	wm_put(ice, index, nvol);
	wm_put_nocache(ice, index, 0x180 | nvol);
}

/*
 * DAC mute control
 */
#define wm_pcm_mute_info	snd_ctl_boolean_mono_info

static int wm_pcm_mute_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);

	mutex_lock(&ice->gpio_mutex);
	ucontrol->value.integer.value[0] = (wm_get(ice, WM_MUTE) & 0x10) ? 0 : 1;
	mutex_unlock(&ice->gpio_mutex);
	return 0;
}

static int wm_pcm_mute_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	unsigned short nval, oval;
	int change;

	snd_ice1712_save_gpio_status(ice);
	oval = wm_get(ice, WM_MUTE);
	nval = (oval & ~0x10) | (ucontrol->value.integer.value[0] ? 0 : 0x10);
	change = (oval != nval);
	if (change)
		wm_put(ice, WM_MUTE, nval);
	snd_ice1712_restore_gpio_status(ice);

	return change;
}

/*
 * Master volume attenuation mixer control
 */
static int wm_master_vol_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = WM_VOL_MAX;
	return 0;
}

static int wm_master_vol_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	struct aureon_spec *spec = ice->spec;
	int i;
	for (i = 0; i < 2; i++)
		ucontrol->value.integer.value[i] =
			spec->master[i] & ~WM_VOL_MUTE;
	return 0;
}

static int wm_master_vol_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	struct aureon_spec *spec = ice->spec;
	int ch, change = 0;

	snd_ice1712_save_gpio_status(ice);
	for (ch = 0; ch < 2; ch++) {
		unsigned int vol = ucontrol->value.integer.value[ch];
		if (vol > WM_VOL_MAX)
			vol = WM_VOL_MAX;
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

/*
 * DAC volume attenuation mixer control
 */
static int wm_vol_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	int voices = kcontrol->private_value >> 8;
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = voices;
	uinfo->value.integer.min = 0;		/* mute (-101dB) */
	uinfo->value.integer.max = WM_VOL_MAX;	/* 0dB */
	return 0;
}

static int wm_vol_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	struct aureon_spec *spec = ice->spec;
	int i, ofs, voices;

	voices = kcontrol->private_value >> 8;
	ofs = kcontrol->private_value & 0xff;
	for (i = 0; i < voices; i++)
		ucontrol->value.integer.value[i] =
			spec->vol[ofs+i] & ~WM_VOL_MUTE;
	return 0;
}

static int wm_vol_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	struct aureon_spec *spec = ice->spec;
	int i, idx, ofs, voices;
	int change = 0;

	voices = kcontrol->private_value >> 8;
	ofs = kcontrol->private_value & 0xff;
	snd_ice1712_save_gpio_status(ice);
	for (i = 0; i < voices; i++) {
		unsigned int vol = ucontrol->value.integer.value[i];
		if (vol > WM_VOL_MAX)
			vol = WM_VOL_MAX;
		vol |= spec->vol[ofs+i] & WM_VOL_MUTE;
		if (vol != spec->vol[ofs+i]) {
			spec->vol[ofs+i] = vol;
			idx  = WM_DAC_ATTEN + ofs + i;
			wm_set_vol(ice, idx, spec->vol[ofs + i],
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
static int wm_mute_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BOOLEAN;
	uinfo->count = kcontrol->private_value >> 8;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 1;
	return 0;
}

static int wm_mute_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	struct aureon_spec *spec = ice->spec;
	int voices, ofs, i;

	voices = kcontrol->private_value >> 8;
	ofs = kcontrol->private_value & 0xFF;

	for (i = 0; i < voices; i++)
		ucontrol->value.integer.value[i] =
			(spec->vol[ofs + i] & WM_VOL_MUTE) ? 0 : 1;
	return 0;
}

static int wm_mute_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	struct aureon_spec *spec = ice->spec;
	int change = 0, voices, ofs, i;

	voices = kcontrol->private_value >> 8;
	ofs = kcontrol->private_value & 0xFF;

	snd_ice1712_save_gpio_status(ice);
	for (i = 0; i < voices; i++) {
		int val = (spec->vol[ofs + i] & WM_VOL_MUTE) ? 0 : 1;
		if (ucontrol->value.integer.value[i] != val) {
			spec->vol[ofs + i] &= ~WM_VOL_MUTE;
			spec->vol[ofs + i] |=
				ucontrol->value.integer.value[i] ? 0 : WM_VOL_MUTE;
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

static int wm_master_mute_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	struct aureon_spec *spec = ice->spec;

	ucontrol->value.integer.value[0] =
		(spec->master[0] & WM_VOL_MUTE) ? 0 : 1;
	ucontrol->value.integer.value[1] =
		(spec->master[1] & WM_VOL_MUTE) ? 0 : 1;
	return 0;
}

static int wm_master_mute_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	struct aureon_spec *spec = ice->spec;
	int change = 0, i;

	snd_ice1712_save_gpio_status(ice);
	for (i = 0; i < 2; i++) {
		int val = (spec->master[i] & WM_VOL_MUTE) ? 0 : 1;
		if (ucontrol->value.integer.value[i] != val) {
			int dac;
			spec->master[i] &= ~WM_VOL_MUTE;
			spec->master[i] |=
				ucontrol->value.integer.value[i] ? 0 : WM_VOL_MUTE;
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
static int wm_pcm_vol_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;		/* mute (-64dB) */
	uinfo->value.integer.max = PCM_RES;	/* 0dB */
	return 0;
}

static int wm_pcm_vol_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
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

static int wm_pcm_vol_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
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
		wm_put_nocache(ice, WM_DAC_DIG_MASTER_ATTEN, nvol | 0x100); /* update */
		change = 1;
	}
	snd_ice1712_restore_gpio_status(ice);
	return change;
}

/*
 * ADC mute control
 */
#define wm_adc_mute_info		snd_ctl_boolean_stereo_info

static int wm_adc_mute_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	unsigned short val;
	int i;

	mutex_lock(&ice->gpio_mutex);
	for (i = 0; i < 2; i++) {
		val = wm_get(ice, WM_ADC_GAIN + i);
		ucontrol->value.integer.value[i] = ~val>>5 & 0x1;
	}
	mutex_unlock(&ice->gpio_mutex);
	return 0;
}

static int wm_adc_mute_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	unsigned short new, old;
	int i, change = 0;

	snd_ice1712_save_gpio_status(ice);
	for (i = 0; i < 2; i++) {
		old = wm_get(ice, WM_ADC_GAIN + i);
		new = (~ucontrol->value.integer.value[i]<<5&0x20) | (old&~0x20);
		if (new != old) {
			wm_put(ice, WM_ADC_GAIN + i, new);
			change = 1;
		}
	}
	snd_ice1712_restore_gpio_status(ice);

	return change;
}

/*
 * ADC gain mixer control
 */
static int wm_adc_vol_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;		/* -12dB */
	uinfo->value.integer.max = 0x1f;	/* 19dB */
	return 0;
}

static int wm_adc_vol_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	int i, idx;
	unsigned short vol;

	mutex_lock(&ice->gpio_mutex);
	for (i = 0; i < 2; i++) {
		idx = WM_ADC_GAIN + i;
		vol = wm_get(ice, idx) & 0x1f;
		ucontrol->value.integer.value[i] = vol;
	}
	mutex_unlock(&ice->gpio_mutex);
	return 0;
}

static int wm_adc_vol_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	int i, idx;
	unsigned short ovol, nvol;
	int change = 0;

	snd_ice1712_save_gpio_status(ice);
	for (i = 0; i < 2; i++) {
		idx  = WM_ADC_GAIN + i;
		nvol = ucontrol->value.integer.value[i] & 0x1f;
		ovol = wm_get(ice, idx);
		if ((ovol & 0x1f) != nvol) {
			wm_put(ice, idx, nvol | (ovol & ~0x1f));
			change = 1;
		}
	}
	snd_ice1712_restore_gpio_status(ice);
	return change;
}

/*
 * ADC input mux mixer control
 */
static int wm_adc_mux_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	static const char * const texts[] = {
		"CD",		/* AIN1 */
		"Aux",		/* AIN2 */
		"Line",		/* AIN3 */
		"Mic",		/* AIN4 */
		"AC97"		/* AIN5 */
	};
	static const char * const universe_texts[] = {
		"Aux1",		/* AIN1 */
		"CD",		/* AIN2 */
		"Phono",	/* AIN3 */
		"Line",		/* AIN4 */
		"Aux2",		/* AIN5 */
		"Mic",		/* AIN6 */
		"Aux3",		/* AIN7 */
		"AC97"		/* AIN8 */
	};
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);

	if (ice->eeprom.subvendor == VT1724_SUBDEVICE_AUREON71_UNIVERSE)
		return snd_ctl_enum_info(uinfo, 2, 8, universe_texts);
	else
		return snd_ctl_enum_info(uinfo, 2, 5, texts);
}

static int wm_adc_mux_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	unsigned short val;

	mutex_lock(&ice->gpio_mutex);
	val = wm_get(ice, WM_ADC_MUX);
	ucontrol->value.enumerated.item[0] = val & 7;
	ucontrol->value.enumerated.item[1] = (val >> 4) & 7;
	mutex_unlock(&ice->gpio_mutex);
	return 0;
}

static int wm_adc_mux_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	unsigned short oval, nval;
	int change;

	snd_ice1712_save_gpio_status(ice);
	oval = wm_get(ice, WM_ADC_MUX);
	nval = oval & ~0x77;
	nval |= ucontrol->value.enumerated.item[0] & 7;
	nval |= (ucontrol->value.enumerated.item[1] & 7) << 4;
	change = (oval != nval);
	if (change)
		wm_put(ice, WM_ADC_MUX, nval);
	snd_ice1712_restore_gpio_status(ice);
	return change;
}

/*
 * CS8415 Input mux
 */
static int aureon_cs8415_mux_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	static const char * const aureon_texts[] = {
		"CD",		/* RXP0 */
		"Optical"	/* RXP1 */
	};
	static const char * const prodigy_texts[] = {
		"CD",
		"Coax"
	};
	if (ice->eeprom.subvendor == VT1724_SUBDEVICE_PRODIGY71)
		return snd_ctl_enum_info(uinfo, 1, 2, prodigy_texts);
	else
		return snd_ctl_enum_info(uinfo, 1, 2, aureon_texts);
}

static int aureon_cs8415_mux_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	struct aureon_spec *spec = ice->spec;

	/* snd_ice1712_save_gpio_status(ice); */
	/* val = aureon_cs8415_get(ice, CS8415_CTRL2); */
	ucontrol->value.enumerated.item[0] = spec->cs8415_mux;
	/* snd_ice1712_restore_gpio_status(ice); */
	return 0;
}

static int aureon_cs8415_mux_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	struct aureon_spec *spec = ice->spec;
	unsigned short oval, nval;
	int change;

	snd_ice1712_save_gpio_status(ice);
	oval = aureon_cs8415_get(ice, CS8415_CTRL2);
	nval = oval & ~0x07;
	nval |= ucontrol->value.enumerated.item[0] & 7;
	change = (oval != nval);
	if (change)
		aureon_cs8415_put(ice, CS8415_CTRL2, nval);
	snd_ice1712_restore_gpio_status(ice);
	spec->cs8415_mux = ucontrol->value.enumerated.item[0];
	return change;
}

static int aureon_cs8415_rate_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 192000;
	return 0;
}

static int aureon_cs8415_rate_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	unsigned char ratio;
	ratio = aureon_cs8415_get(ice, CS8415_RATIO);
	ucontrol->value.integer.value[0] = (int)((unsigned int)ratio * 750);
	return 0;
}

/*
 * CS8415A Mute
 */
#define aureon_cs8415_mute_info		snd_ctl_boolean_mono_info

static int aureon_cs8415_mute_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	snd_ice1712_save_gpio_status(ice);
	ucontrol->value.integer.value[0] = (aureon_cs8415_get(ice, CS8415_CTRL1) & 0x20) ? 0 : 1;
	snd_ice1712_restore_gpio_status(ice);
	return 0;
}

static int aureon_cs8415_mute_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	unsigned char oval, nval;
	int change;
	snd_ice1712_save_gpio_status(ice);
	oval = aureon_cs8415_get(ice, CS8415_CTRL1);
	if (ucontrol->value.integer.value[0])
		nval = oval & ~0x20;
	else
		nval = oval | 0x20;
	change = (oval != nval);
	if (change)
		aureon_cs8415_put(ice, CS8415_CTRL1, nval);
	snd_ice1712_restore_gpio_status(ice);
	return change;
}

/*
 * CS8415A Q-Sub info
 */
static int aureon_cs8415_qsub_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_BYTES;
	uinfo->count = 10;
	return 0;
}

static int aureon_cs8415_qsub_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);

	snd_ice1712_save_gpio_status(ice);
	aureon_cs8415_read(ice, CS8415_QSUB, ucontrol->value.bytes.data, 10);
	snd_ice1712_restore_gpio_status(ice);

	return 0;
}

static int aureon_cs8415_spdif_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_IEC958;
	uinfo->count = 1;
	return 0;
}

static int aureon_cs8415_mask_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	memset(ucontrol->value.iec958.status, 0xFF, 24);
	return 0;
}

static int aureon_cs8415_spdif_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);

	snd_ice1712_save_gpio_status(ice);
	aureon_cs8415_read(ice, CS8415_C_BUFFER, ucontrol->value.iec958.status, 24);
	snd_ice1712_restore_gpio_status(ice);
	return 0;
}

/*
 * Headphone Amplifier
 */
static int aureon_set_headphone_amp(struct snd_ice1712 *ice, int enable)
{
	unsigned int tmp, tmp2;

	tmp2 = tmp = snd_ice1712_gpio_read(ice);
	if (enable)
		if (ice->eeprom.subvendor != VT1724_SUBDEVICE_PRODIGY71LT &&
		    ice->eeprom.subvendor != VT1724_SUBDEVICE_PRODIGY71XT)
			tmp |= AUREON_HP_SEL;
		else
			tmp |= PRODIGY_HP_SEL;
	else
		if (ice->eeprom.subvendor != VT1724_SUBDEVICE_PRODIGY71LT &&
		    ice->eeprom.subvendor != VT1724_SUBDEVICE_PRODIGY71XT)
			tmp &= ~AUREON_HP_SEL;
		else
			tmp &= ~PRODIGY_HP_SEL;
	if (tmp != tmp2) {
		snd_ice1712_gpio_write(ice, tmp);
		return 1;
	}
	return 0;
}

static int aureon_get_headphone_amp(struct snd_ice1712 *ice)
{
	unsigned int tmp = snd_ice1712_gpio_read(ice);

	return (tmp & AUREON_HP_SEL) != 0;
}

#define aureon_hpamp_info	snd_ctl_boolean_mono_info

static int aureon_hpamp_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);

	ucontrol->value.integer.value[0] = aureon_get_headphone_amp(ice);
	return 0;
}


static int aureon_hpamp_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);

	return aureon_set_headphone_amp(ice, ucontrol->value.integer.value[0]);
}

/*
 * Deemphasis
 */

#define aureon_deemp_info	snd_ctl_boolean_mono_info

static int aureon_deemp_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	ucontrol->value.integer.value[0] = (wm_get(ice, WM_DAC_CTRL2) & 0xf) == 0xf;
	return 0;
}

static int aureon_deemp_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
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
static int aureon_oversampling_info(struct snd_kcontrol *k, struct snd_ctl_elem_info *uinfo)
{
	static const char * const texts[2] = { "128x", "64x"	};

	return snd_ctl_enum_info(uinfo, 1, 2, texts);
}

static int aureon_oversampling_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	ucontrol->value.enumerated.item[0] = (wm_get(ice, WM_MASTER) & 0x8) == 0x8;
	return 0;
}

static int aureon_oversampling_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	int temp, temp2;
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);

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

/*
 * mixers
 */

static const struct snd_kcontrol_new aureon_dac_controls[] = {
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
		.name = "Capture Switch",
		.info = wm_adc_mute_info,
		.get = wm_adc_mute_get,
		.put = wm_adc_mute_put,
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.access = (SNDRV_CTL_ELEM_ACCESS_READWRITE |
				SNDRV_CTL_ELEM_ACCESS_TLV_READ),
		.name = "Capture Volume",
		.info = wm_adc_vol_info,
		.get = wm_adc_vol_get,
		.put = wm_adc_vol_put,
		.tlv = { .p = db_scale_wm_adc }
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Capture Source",
		.info = wm_adc_mux_info,
		.get = wm_adc_mux_get,
		.put = wm_adc_mux_put,
		.private_value = 5
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "External Amplifier",
		.info = aureon_hpamp_info,
		.get = aureon_hpamp_get,
		.put = aureon_hpamp_put
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "DAC Deemphasis Switch",
		.info = aureon_deemp_info,
		.get = aureon_deemp_get,
		.put = aureon_deemp_put
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "ADC Oversampling",
		.info = aureon_oversampling_info,
		.get = aureon_oversampling_get,
		.put = aureon_oversampling_put
	}
};

static const struct snd_kcontrol_new ac97_controls[] = {
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "AC97 Playback Switch",
		.info = aureon_ac97_mmute_info,
		.get = aureon_ac97_mmute_get,
		.put = aureon_ac97_mmute_put,
		.private_value = AC97_MASTER
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.access = (SNDRV_CTL_ELEM_ACCESS_READWRITE |
				SNDRV_CTL_ELEM_ACCESS_TLV_READ),
		.name = "AC97 Playback Volume",
		.info = aureon_ac97_vol_info,
		.get = aureon_ac97_vol_get,
		.put = aureon_ac97_vol_put,
		.private_value = AC97_MASTER|AUREON_AC97_STEREO,
		.tlv = { .p = db_scale_ac97_master }
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "CD Playback Switch",
		.info = aureon_ac97_mute_info,
		.get = aureon_ac97_mute_get,
		.put = aureon_ac97_mute_put,
		.private_value = AC97_CD
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.access = (SNDRV_CTL_ELEM_ACCESS_READWRITE |
				SNDRV_CTL_ELEM_ACCESS_TLV_READ),
		.name = "CD Playback Volume",
		.info = aureon_ac97_vol_info,
		.get = aureon_ac97_vol_get,
		.put = aureon_ac97_vol_put,
		.private_value = AC97_CD|AUREON_AC97_STEREO,
		.tlv = { .p = db_scale_ac97_gain }
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Aux Playback Switch",
		.info = aureon_ac97_mute_info,
		.get = aureon_ac97_mute_get,
		.put = aureon_ac97_mute_put,
		.private_value = AC97_AUX,
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.access = (SNDRV_CTL_ELEM_ACCESS_READWRITE |
				SNDRV_CTL_ELEM_ACCESS_TLV_READ),
		.name = "Aux Playback Volume",
		.info = aureon_ac97_vol_info,
		.get = aureon_ac97_vol_get,
		.put = aureon_ac97_vol_put,
		.private_value = AC97_AUX|AUREON_AC97_STEREO,
		.tlv = { .p = db_scale_ac97_gain }
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Line Playback Switch",
		.info = aureon_ac97_mute_info,
		.get = aureon_ac97_mute_get,
		.put = aureon_ac97_mute_put,
		.private_value = AC97_LINE
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.access = (SNDRV_CTL_ELEM_ACCESS_READWRITE |
				SNDRV_CTL_ELEM_ACCESS_TLV_READ),
		.name = "Line Playback Volume",
		.info = aureon_ac97_vol_info,
		.get = aureon_ac97_vol_get,
		.put = aureon_ac97_vol_put,
		.private_value = AC97_LINE|AUREON_AC97_STEREO,
		.tlv = { .p = db_scale_ac97_gain }
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Mic Playback Switch",
		.info = aureon_ac97_mute_info,
		.get = aureon_ac97_mute_get,
		.put = aureon_ac97_mute_put,
		.private_value = AC97_MIC
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.access = (SNDRV_CTL_ELEM_ACCESS_READWRITE |
				SNDRV_CTL_ELEM_ACCESS_TLV_READ),
		.name = "Mic Playback Volume",
		.info = aureon_ac97_vol_info,
		.get = aureon_ac97_vol_get,
		.put = aureon_ac97_vol_put,
		.private_value = AC97_MIC,
		.tlv = { .p = db_scale_ac97_gain }
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Mic Boost (+20dB)",
		.info = aureon_ac97_micboost_info,
		.get = aureon_ac97_micboost_get,
		.put = aureon_ac97_micboost_put
	}
};

static const struct snd_kcontrol_new universe_ac97_controls[] = {
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "AC97 Playback Switch",
		.info = aureon_ac97_mmute_info,
		.get = aureon_ac97_mmute_get,
		.put = aureon_ac97_mmute_put,
		.private_value = AC97_MASTER
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.access = (SNDRV_CTL_ELEM_ACCESS_READWRITE |
				SNDRV_CTL_ELEM_ACCESS_TLV_READ),
		.name = "AC97 Playback Volume",
		.info = aureon_ac97_vol_info,
		.get = aureon_ac97_vol_get,
		.put = aureon_ac97_vol_put,
		.private_value = AC97_MASTER|AUREON_AC97_STEREO,
		.tlv = { .p = db_scale_ac97_master }
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "CD Playback Switch",
		.info = aureon_ac97_mute_info,
		.get = aureon_ac97_mute_get,
		.put = aureon_ac97_mute_put,
		.private_value = AC97_AUX
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.access = (SNDRV_CTL_ELEM_ACCESS_READWRITE |
				SNDRV_CTL_ELEM_ACCESS_TLV_READ),
		.name = "CD Playback Volume",
		.info = aureon_ac97_vol_info,
		.get = aureon_ac97_vol_get,
		.put = aureon_ac97_vol_put,
		.private_value = AC97_AUX|AUREON_AC97_STEREO,
		.tlv = { .p = db_scale_ac97_gain }
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Phono Playback Switch",
		.info = aureon_ac97_mute_info,
		.get = aureon_ac97_mute_get,
		.put = aureon_ac97_mute_put,
		.private_value = AC97_CD
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.access = (SNDRV_CTL_ELEM_ACCESS_READWRITE |
				SNDRV_CTL_ELEM_ACCESS_TLV_READ),
		.name = "Phono Playback Volume",
		.info = aureon_ac97_vol_info,
		.get = aureon_ac97_vol_get,
		.put = aureon_ac97_vol_put,
		.private_value = AC97_CD|AUREON_AC97_STEREO,
		.tlv = { .p = db_scale_ac97_gain }
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Line Playback Switch",
		.info = aureon_ac97_mute_info,
		.get = aureon_ac97_mute_get,
		.put = aureon_ac97_mute_put,
		.private_value = AC97_LINE
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.access = (SNDRV_CTL_ELEM_ACCESS_READWRITE |
				SNDRV_CTL_ELEM_ACCESS_TLV_READ),
		.name = "Line Playback Volume",
		.info = aureon_ac97_vol_info,
		.get = aureon_ac97_vol_get,
		.put = aureon_ac97_vol_put,
		.private_value = AC97_LINE|AUREON_AC97_STEREO,
		.tlv = { .p = db_scale_ac97_gain }
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Mic Playback Switch",
		.info = aureon_ac97_mute_info,
		.get = aureon_ac97_mute_get,
		.put = aureon_ac97_mute_put,
		.private_value = AC97_MIC
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.access = (SNDRV_CTL_ELEM_ACCESS_READWRITE |
				SNDRV_CTL_ELEM_ACCESS_TLV_READ),
		.name = "Mic Playback Volume",
		.info = aureon_ac97_vol_info,
		.get = aureon_ac97_vol_get,
		.put = aureon_ac97_vol_put,
		.private_value = AC97_MIC,
		.tlv = { .p = db_scale_ac97_gain }
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Mic Boost (+20dB)",
		.info = aureon_ac97_micboost_info,
		.get = aureon_ac97_micboost_get,
		.put = aureon_ac97_micboost_put
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Aux Playback Switch",
		.info = aureon_ac97_mute_info,
		.get = aureon_ac97_mute_get,
		.put = aureon_ac97_mute_put,
		.private_value = AC97_VIDEO,
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.access = (SNDRV_CTL_ELEM_ACCESS_READWRITE |
				SNDRV_CTL_ELEM_ACCESS_TLV_READ),
		.name = "Aux Playback Volume",
		.info = aureon_ac97_vol_info,
		.get = aureon_ac97_vol_get,
		.put = aureon_ac97_vol_put,
		.private_value = AC97_VIDEO|AUREON_AC97_STEREO,
		.tlv = { .p = db_scale_ac97_gain }
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Aux Source",
		.info = aureon_universe_inmux_info,
		.get = aureon_universe_inmux_get,
		.put = aureon_universe_inmux_put
	}

};

static const struct snd_kcontrol_new cs8415_controls[] = {
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = SNDRV_CTL_NAME_IEC958("", CAPTURE, SWITCH),
		.info = aureon_cs8415_mute_info,
		.get = aureon_cs8415_mute_get,
		.put = aureon_cs8415_mute_put
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = SNDRV_CTL_NAME_IEC958("", CAPTURE, NONE) "Source",
		.info = aureon_cs8415_mux_info,
		.get = aureon_cs8415_mux_get,
		.put = aureon_cs8415_mux_put,
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_PCM,
		.name = SNDRV_CTL_NAME_IEC958("Q-subcode ", CAPTURE, DEFAULT),
		.access = SNDRV_CTL_ELEM_ACCESS_READ | SNDRV_CTL_ELEM_ACCESS_VOLATILE,
		.info = aureon_cs8415_qsub_info,
		.get = aureon_cs8415_qsub_get,
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_PCM,
		.name = SNDRV_CTL_NAME_IEC958("", CAPTURE, MASK),
		.access = SNDRV_CTL_ELEM_ACCESS_READ,
		.info = aureon_cs8415_spdif_info,
		.get = aureon_cs8415_mask_get
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_PCM,
		.name = SNDRV_CTL_NAME_IEC958("", CAPTURE, DEFAULT),
		.access = SNDRV_CTL_ELEM_ACCESS_READ | SNDRV_CTL_ELEM_ACCESS_VOLATILE,
		.info = aureon_cs8415_spdif_info,
		.get = aureon_cs8415_spdif_get
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_PCM,
		.name = SNDRV_CTL_NAME_IEC958("", CAPTURE, NONE) "Rate",
		.access = SNDRV_CTL_ELEM_ACCESS_READ | SNDRV_CTL_ELEM_ACCESS_VOLATILE,
		.info = aureon_cs8415_rate_info,
		.get = aureon_cs8415_rate_get
	}
};

static int aureon_add_controls(struct snd_ice1712 *ice)
{
	unsigned int i, counts;
	int err;

	counts = ARRAY_SIZE(aureon_dac_controls);
	if (ice->eeprom.subvendor == VT1724_SUBDEVICE_AUREON51_SKY)
		counts -= 2; /* no side */
	for (i = 0; i < counts; i++) {
		err = snd_ctl_add(ice->card, snd_ctl_new1(&aureon_dac_controls[i], ice));
		if (err < 0)
			return err;
	}

	for (i = 0; i < ARRAY_SIZE(wm_controls); i++) {
		err = snd_ctl_add(ice->card, snd_ctl_new1(&wm_controls[i], ice));
		if (err < 0)
			return err;
	}

	if (ice->eeprom.subvendor == VT1724_SUBDEVICE_AUREON71_UNIVERSE) {
		for (i = 0; i < ARRAY_SIZE(universe_ac97_controls); i++) {
			err = snd_ctl_add(ice->card, snd_ctl_new1(&universe_ac97_controls[i], ice));
			if (err < 0)
				return err;
		}
	} else if (ice->eeprom.subvendor != VT1724_SUBDEVICE_PRODIGY71LT &&
		 ice->eeprom.subvendor != VT1724_SUBDEVICE_PRODIGY71XT) {
		for (i = 0; i < ARRAY_SIZE(ac97_controls); i++) {
			err = snd_ctl_add(ice->card, snd_ctl_new1(&ac97_controls[i], ice));
			if (err < 0)
				return err;
		}
	}

	if (ice->eeprom.subvendor != VT1724_SUBDEVICE_PRODIGY71LT &&
	    ice->eeprom.subvendor != VT1724_SUBDEVICE_PRODIGY71XT) {
		unsigned char id;
		snd_ice1712_save_gpio_status(ice);
		id = aureon_cs8415_get(ice, CS8415_ID);
		snd_ice1712_restore_gpio_status(ice);
		if (id != 0x41)
			dev_info(ice->card->dev,
				 "No CS8415 chip. Skipping CS8415 controls.\n");
		else {
			for (i = 0; i < ARRAY_SIZE(cs8415_controls); i++) {
				struct snd_kcontrol *kctl;
				err = snd_ctl_add(ice->card, (kctl = snd_ctl_new1(&cs8415_controls[i], ice)));
				if (err < 0)
					return err;
				if (i > 1)
					kctl->id.device = ice->pcm->device;
			}
		}
	}

	return 0;
}

/*
 * reset the chip
 */
static int aureon_reset(struct snd_ice1712 *ice)
{
	static const unsigned short wm_inits_aureon[] = {
		/* These come first to reduce init pop noise */
		0x1b, 0x044,		/* ADC Mux (AC'97 source) */
		0x1c, 0x00B,		/* Out Mux1 (VOUT1 = DAC+AUX, VOUT2 = DAC) */
		0x1d, 0x009,		/* Out Mux2 (VOUT2 = DAC, VOUT3 = DAC) */

		0x18, 0x000,		/* All power-up */

		0x16, 0x122,		/* I2S, normal polarity, 24bit */
		0x17, 0x022,		/* 256fs, slave mode */
		0x00, 0,		/* DAC1 analog mute */
		0x01, 0,		/* DAC2 analog mute */
		0x02, 0,		/* DAC3 analog mute */
		0x03, 0,		/* DAC4 analog mute */
		0x04, 0,		/* DAC5 analog mute */
		0x05, 0,		/* DAC6 analog mute */
		0x06, 0,		/* DAC7 analog mute */
		0x07, 0,		/* DAC8 analog mute */
		0x08, 0x100,		/* master analog mute */
		0x09, 0xff,		/* DAC1 digital full */
		0x0a, 0xff,		/* DAC2 digital full */
		0x0b, 0xff,		/* DAC3 digital full */
		0x0c, 0xff,		/* DAC4 digital full */
		0x0d, 0xff,		/* DAC5 digital full */
		0x0e, 0xff,		/* DAC6 digital full */
		0x0f, 0xff,		/* DAC7 digital full */
		0x10, 0xff,		/* DAC8 digital full */
		0x11, 0x1ff,		/* master digital full */
		0x12, 0x000,		/* phase normal */
		0x13, 0x090,		/* unmute DAC L/R */
		0x14, 0x000,		/* all unmute */
		0x15, 0x000,		/* no deemphasis, no ZFLG */
		0x19, 0x000,		/* -12dB ADC/L */
		0x1a, 0x000,		/* -12dB ADC/R */
		(unsigned short)-1
	};
	static const unsigned short wm_inits_prodigy[] = {

		/* These come first to reduce init pop noise */
		0x1b, 0x000,		/* ADC Mux */
		0x1c, 0x009,		/* Out Mux1 */
		0x1d, 0x009,		/* Out Mux2 */

		0x18, 0x000,		/* All power-up */

		0x16, 0x022,		/* I2S, normal polarity, 24bit, high-pass on */
		0x17, 0x006,		/* 128fs, slave mode */

		0x00, 0,		/* DAC1 analog mute */
		0x01, 0,		/* DAC2 analog mute */
		0x02, 0,		/* DAC3 analog mute */
		0x03, 0,		/* DAC4 analog mute */
		0x04, 0,		/* DAC5 analog mute */
		0x05, 0,		/* DAC6 analog mute */
		0x06, 0,		/* DAC7 analog mute */
		0x07, 0,		/* DAC8 analog mute */
		0x08, 0x100,		/* master analog mute */

		0x09, 0x7f,		/* DAC1 digital full */
		0x0a, 0x7f,		/* DAC2 digital full */
		0x0b, 0x7f,		/* DAC3 digital full */
		0x0c, 0x7f,		/* DAC4 digital full */
		0x0d, 0x7f,		/* DAC5 digital full */
		0x0e, 0x7f,		/* DAC6 digital full */
		0x0f, 0x7f,		/* DAC7 digital full */
		0x10, 0x7f,		/* DAC8 digital full */
		0x11, 0x1FF,		/* master digital full */

		0x12, 0x000,		/* phase normal */
		0x13, 0x090,		/* unmute DAC L/R */
		0x14, 0x000,		/* all unmute */
		0x15, 0x000,		/* no deemphasis, no ZFLG */

		0x19, 0x000,		/* -12dB ADC/L */
		0x1a, 0x000,		/* -12dB ADC/R */
		(unsigned short)-1

	};
	static const unsigned short cs_inits[] = {
		0x0441, /* RUN */
		0x0180, /* no mute, OMCK output on RMCK pin */
		0x0201, /* S/PDIF source on RXP1 */
		0x0605, /* slave, 24bit, MSB on second OSCLK, SDOUT for right channel when OLRCK is high */
		(unsigned short)-1
	};
	unsigned int tmp;
	const unsigned short *p;
	int err;
	struct aureon_spec *spec = ice->spec;

	err = aureon_ac97_init(ice);
	if (err != 0)
		return err;

	snd_ice1712_gpio_set_dir(ice, 0x5fffff); /* fix this for the time being */

	/* reset the wm codec as the SPI mode */
	snd_ice1712_save_gpio_status(ice);
	snd_ice1712_gpio_set_mask(ice, ~(AUREON_WM_RESET|AUREON_WM_CS|AUREON_CS8415_CS|AUREON_HP_SEL));

	tmp = snd_ice1712_gpio_read(ice);
	tmp &= ~AUREON_WM_RESET;
	snd_ice1712_gpio_write(ice, tmp);
	udelay(1);
	tmp |= AUREON_WM_CS | AUREON_CS8415_CS;
	snd_ice1712_gpio_write(ice, tmp);
	udelay(1);
	tmp |= AUREON_WM_RESET;
	snd_ice1712_gpio_write(ice, tmp);
	udelay(1);

	/* initialize WM8770 codec */
	if (ice->eeprom.subvendor == VT1724_SUBDEVICE_PRODIGY71 ||
		ice->eeprom.subvendor == VT1724_SUBDEVICE_PRODIGY71LT ||
		ice->eeprom.subvendor == VT1724_SUBDEVICE_PRODIGY71XT)
		p = wm_inits_prodigy;
	else
		p = wm_inits_aureon;
	for (; *p != (unsigned short)-1; p += 2)
		wm_put(ice, p[0], p[1]);

	/* initialize CS8415A codec */
	if (ice->eeprom.subvendor != VT1724_SUBDEVICE_PRODIGY71LT &&
	    ice->eeprom.subvendor != VT1724_SUBDEVICE_PRODIGY71XT) {
		for (p = cs_inits; *p != (unsigned short)-1; p++)
			aureon_spi_write(ice, AUREON_CS8415_CS, *p | 0x200000, 24);
		spec->cs8415_mux = 1;

		aureon_set_headphone_amp(ice, 1);
	}

	snd_ice1712_restore_gpio_status(ice);

	/* initialize PCA9554 pin directions & set default input */
	aureon_pca9554_write(ice, PCA9554_DIR, 0x00);
	aureon_pca9554_write(ice, PCA9554_OUT, 0x00);   /* internal AUX */
	return 0;
}

/*
 * suspend/resume
 */
#ifdef CONFIG_PM_SLEEP
static int aureon_resume(struct snd_ice1712 *ice)
{
	struct aureon_spec *spec = ice->spec;
	int err, i;

	err = aureon_reset(ice);
	if (err != 0)
		return err;

	/* workaround for poking volume with alsamixer after resume:
	 * just set stored volume again */
	for (i = 0; i < ice->num_total_dacs; i++)
		wm_set_vol(ice, i, spec->vol[i], spec->master[i % 2]);
	return 0;
}
#endif

/*
 * initialize the chip
 */
static int aureon_init(struct snd_ice1712 *ice)
{
	struct aureon_spec *spec;
	int i, err;

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec)
		return -ENOMEM;
	ice->spec = spec;

	if (ice->eeprom.subvendor == VT1724_SUBDEVICE_AUREON51_SKY) {
		ice->num_total_dacs = 6;
		ice->num_total_adcs = 2;
	} else {
		/* aureon 7.1 and prodigy 7.1 */
		ice->num_total_dacs = 8;
		ice->num_total_adcs = 2;
	}

	/* to remember the register values of CS8415 */
	ice->akm = kzalloc(sizeof(struct snd_akm4xxx), GFP_KERNEL);
	if (!ice->akm)
		return -ENOMEM;
	ice->akm_codecs = 1;

	err = aureon_reset(ice);
	if (err != 0)
		return err;

	spec->master[0] = WM_VOL_MUTE;
	spec->master[1] = WM_VOL_MUTE;
	for (i = 0; i < ice->num_total_dacs; i++) {
		spec->vol[i] = WM_VOL_MUTE;
		wm_set_vol(ice, i, spec->vol[i], spec->master[i % 2]);
	}

#ifdef CONFIG_PM_SLEEP
	ice->pm_resume = aureon_resume;
	ice->pm_suspend_enabled = 1;
#endif

	return 0;
}


/*
 * Aureon boards don't provide the EEPROM data except for the vendor IDs.
 * hence the driver needs to sets up it properly.
 */

static const unsigned char aureon51_eeprom[] = {
	[ICE_EEP2_SYSCONF]     = 0x0a,	/* clock 512, spdif-in/ADC, 3DACs */
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

static const unsigned char aureon71_eeprom[] = {
	[ICE_EEP2_SYSCONF]     = 0x0b,	/* clock 512, spdif-in/ADC, 4DACs */
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
#define prodigy71_eeprom aureon71_eeprom

static const unsigned char aureon71_universe_eeprom[] = {
	[ICE_EEP2_SYSCONF]     = 0x2b,	/* clock 512, mpu401, spdif-in/ADC,
					 * 4DACs
					 */
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

static const unsigned char prodigy71lt_eeprom[] = {
	[ICE_EEP2_SYSCONF]     = 0x4b,	/* clock 384, spdif-in/ADC, 4DACs */
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
#define prodigy71xt_eeprom prodigy71lt_eeprom

/* entry point */
struct snd_ice1712_card_info snd_vt1724_aureon_cards[] = {
	{
		.subvendor = VT1724_SUBDEVICE_AUREON51_SKY,
		.name = "Terratec Aureon 5.1-Sky",
		.model = "aureon51",
		.chip_init = aureon_init,
		.build_controls = aureon_add_controls,
		.eeprom_size = sizeof(aureon51_eeprom),
		.eeprom_data = aureon51_eeprom,
		.driver = "Aureon51",
	},
	{
		.subvendor = VT1724_SUBDEVICE_AUREON71_SPACE,
		.name = "Terratec Aureon 7.1-Space",
		.model = "aureon71",
		.chip_init = aureon_init,
		.build_controls = aureon_add_controls,
		.eeprom_size = sizeof(aureon71_eeprom),
		.eeprom_data = aureon71_eeprom,
		.driver = "Aureon71",
	},
	{
		.subvendor = VT1724_SUBDEVICE_AUREON71_UNIVERSE,
		.name = "Terratec Aureon 7.1-Universe",
		.model = "universe",
		.chip_init = aureon_init,
		.build_controls = aureon_add_controls,
		.eeprom_size = sizeof(aureon71_universe_eeprom),
		.eeprom_data = aureon71_universe_eeprom,
		.driver = "Aureon71Univ", /* keep in 15 letters */
	},
	{
		.subvendor = VT1724_SUBDEVICE_PRODIGY71,
		.name = "Audiotrak Prodigy 7.1",
		.model = "prodigy71",
		.chip_init = aureon_init,
		.build_controls = aureon_add_controls,
		.eeprom_size = sizeof(prodigy71_eeprom),
		.eeprom_data = prodigy71_eeprom,
		.driver = "Prodigy71", /* should be identical with Aureon71 */
	},
	{
		.subvendor = VT1724_SUBDEVICE_PRODIGY71LT,
		.name = "Audiotrak Prodigy 7.1 LT",
		.model = "prodigy71lt",
		.chip_init = aureon_init,
		.build_controls = aureon_add_controls,
		.eeprom_size = sizeof(prodigy71lt_eeprom),
		.eeprom_data = prodigy71lt_eeprom,
		.driver = "Prodigy71LT",
	},
	{
		.subvendor = VT1724_SUBDEVICE_PRODIGY71XT,
		.name = "Audiotrak Prodigy 7.1 XT",
		.model = "prodigy71xt",
		.chip_init = aureon_init,
		.build_controls = aureon_add_controls,
		.eeprom_size = sizeof(prodigy71xt_eeprom),
		.eeprom_data = prodigy71xt_eeprom,
		.driver = "Prodigy71LT",
	},
	{ } /* terminator */
};
