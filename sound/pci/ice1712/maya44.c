// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *   ALSA driver for ICEnsemble VT1724 (Envy24HT)
 *
 *   Lowlevel functions for ESI Maya44 cards
 *
 *	Copyright (c) 2009 Takashi Iwai <tiwai@suse.de>
 *	Based on the patches by Rainer Zimmermann <mail@lightshed.de>
 */

#include <linux/init.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/control.h>
#include <sound/pcm.h>
#include <sound/tlv.h>

#include "ice1712.h"
#include "envy24ht.h"
#include "maya44.h"

/* WM8776 register indexes */
#define WM8776_REG_HEADPHONE_L		0x00
#define WM8776_REG_HEADPHONE_R		0x01
#define WM8776_REG_HEADPHONE_MASTER	0x02
#define WM8776_REG_DAC_ATTEN_L		0x03
#define WM8776_REG_DAC_ATTEN_R		0x04
#define WM8776_REG_DAC_ATTEN_MASTER	0x05
#define WM8776_REG_DAC_PHASE		0x06
#define WM8776_REG_DAC_CONTROL		0x07
#define WM8776_REG_DAC_MUTE		0x08
#define WM8776_REG_DAC_DEEMPH		0x09
#define WM8776_REG_DAC_IF_CONTROL	0x0a
#define WM8776_REG_ADC_IF_CONTROL	0x0b
#define WM8776_REG_MASTER_MODE_CONTROL	0x0c
#define WM8776_REG_POWERDOWN		0x0d
#define WM8776_REG_ADC_ATTEN_L		0x0e
#define WM8776_REG_ADC_ATTEN_R		0x0f
#define WM8776_REG_ADC_ALC1		0x10
#define WM8776_REG_ADC_ALC2		0x11
#define WM8776_REG_ADC_ALC3		0x12
#define WM8776_REG_ADC_NOISE_GATE	0x13
#define WM8776_REG_ADC_LIMITER		0x14
#define WM8776_REG_ADC_MUX		0x15
#define WM8776_REG_OUTPUT_MUX		0x16
#define WM8776_REG_RESET		0x17

#define WM8776_NUM_REGS			0x18

/* clock ratio identifiers for snd_wm8776_set_rate() */
#define WM8776_CLOCK_RATIO_128FS	0
#define WM8776_CLOCK_RATIO_192FS	1
#define WM8776_CLOCK_RATIO_256FS	2
#define WM8776_CLOCK_RATIO_384FS	3
#define WM8776_CLOCK_RATIO_512FS	4
#define WM8776_CLOCK_RATIO_768FS	5

enum { WM_VOL_HP, WM_VOL_DAC, WM_VOL_ADC, WM_NUM_VOLS };
enum { WM_SW_DAC, WM_SW_BYPASS, WM_NUM_SWITCHES };

struct snd_wm8776 {
	unsigned char addr;
	unsigned short regs[WM8776_NUM_REGS];
	unsigned char volumes[WM_NUM_VOLS][2];
	unsigned int switch_bits;
};

struct snd_maya44 {
	struct snd_ice1712 *ice;
	struct snd_wm8776 wm[2];
	struct mutex mutex;
};


/* write the given register and save the data to the cache */
static void wm8776_write(struct snd_ice1712 *ice, struct snd_wm8776 *wm,
			 unsigned char reg, unsigned short val)
{
	/*
	 * WM8776 registers are up to 9 bits wide, bit 8 is placed in the LSB
	 * of the address field
	 */
	snd_vt1724_write_i2c(ice, wm->addr,
			     (reg << 1) | ((val >> 8) & 1),
			     val & 0xff);
	wm->regs[reg] = val;
}

/*
 * update the given register with and/or mask and save the data to the cache
 */
static int wm8776_write_bits(struct snd_ice1712 *ice, struct snd_wm8776 *wm,
			     unsigned char reg,
			     unsigned short mask, unsigned short val)
{
	val |= wm->regs[reg] & ~mask;
	if (val != wm->regs[reg]) {
		wm8776_write(ice, wm, reg, val);
		return 1;
	}
	return 0;
}


/*
 * WM8776 volume controls
 */

struct maya_vol_info {
	unsigned int maxval;		/* volume range: 0..maxval */
	unsigned char regs[2];		/* left and right registers */
	unsigned short mask;		/* value mask */
	unsigned short offset;		/* zero-value offset */
	unsigned short mute;		/* mute bit */
	unsigned short update;		/* update bits */
	unsigned char mux_bits[2];	/* extra bits for ADC mute */
};

static const struct maya_vol_info vol_info[WM_NUM_VOLS] = {
	[WM_VOL_HP] = {
		.maxval = 80,
		.regs = { WM8776_REG_HEADPHONE_L, WM8776_REG_HEADPHONE_R },
		.mask = 0x7f,
		.offset = 0x30,
		.mute = 0x00,
		.update = 0x180,	/* update and zero-cross enable */
	},
	[WM_VOL_DAC] = {
		.maxval = 255,
		.regs = { WM8776_REG_DAC_ATTEN_L, WM8776_REG_DAC_ATTEN_R },
		.mask = 0xff,
		.offset = 0x01,
		.mute = 0x00,
		.update = 0x100,	/* zero-cross enable */
	},
	[WM_VOL_ADC] = {
		.maxval = 91,
		.regs = { WM8776_REG_ADC_ATTEN_L, WM8776_REG_ADC_ATTEN_R },
		.mask = 0xff,
		.offset = 0xa5,
		.mute = 0xa5,
		.update = 0x100,	/* update */
		.mux_bits = { 0x80, 0x40 }, /* ADCMUX bits */
	},
};

/*
 * dB tables
 */
/* headphone output: mute, -73..+6db (1db step) */
static const DECLARE_TLV_DB_SCALE(db_scale_hp, -7400, 100, 1);
/* DAC output: mute, -127..0db (0.5db step) */
static const DECLARE_TLV_DB_SCALE(db_scale_dac, -12750, 50, 1);
/* ADC gain: mute, -21..+24db (0.5db step) */
static const DECLARE_TLV_DB_SCALE(db_scale_adc, -2100, 50, 1);

static int maya_vol_info(struct snd_kcontrol *kcontrol,
			 struct snd_ctl_elem_info *uinfo)
{
	unsigned int idx = kcontrol->private_value;
	const struct maya_vol_info *vol = &vol_info[idx];

	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = vol->maxval;
	return 0;
}

static int maya_vol_get(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct snd_maya44 *chip = snd_kcontrol_chip(kcontrol);
	struct snd_wm8776 *wm =
		&chip->wm[snd_ctl_get_ioff(kcontrol, &ucontrol->id)];
	unsigned int idx = kcontrol->private_value;

	mutex_lock(&chip->mutex);
	ucontrol->value.integer.value[0] = wm->volumes[idx][0];
	ucontrol->value.integer.value[1] = wm->volumes[idx][1];
	mutex_unlock(&chip->mutex);
	return 0;
}

static int maya_vol_put(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct snd_maya44 *chip = snd_kcontrol_chip(kcontrol);
	struct snd_wm8776 *wm =
		&chip->wm[snd_ctl_get_ioff(kcontrol, &ucontrol->id)];
	unsigned int idx = kcontrol->private_value;
	const struct maya_vol_info *vol = &vol_info[idx];
	unsigned int val, data;
	int ch, changed = 0;

	mutex_lock(&chip->mutex);
	for (ch = 0; ch < 2; ch++) {
		val = ucontrol->value.integer.value[ch];
		if (val > vol->maxval)
			val = vol->maxval;
		if (val == wm->volumes[idx][ch])
			continue;
		if (!val)
			data = vol->mute;
		else
			data = (val - 1) + vol->offset;
		data |= vol->update;
		changed |= wm8776_write_bits(chip->ice, wm, vol->regs[ch],
					     vol->mask | vol->update, data);
		if (vol->mux_bits[ch])
			wm8776_write_bits(chip->ice, wm, WM8776_REG_ADC_MUX,
					  vol->mux_bits[ch],
					  val ? 0 : vol->mux_bits[ch]);
		wm->volumes[idx][ch] = val;
	}
	mutex_unlock(&chip->mutex);
	return changed;
}

/*
 * WM8776 switch controls
 */

#define COMPOSE_SW_VAL(idx, reg, mask)	((idx) | ((reg) << 8) | ((mask) << 16))
#define GET_SW_VAL_IDX(val)	((val) & 0xff)
#define GET_SW_VAL_REG(val)	(((val) >> 8) & 0xff)
#define GET_SW_VAL_MASK(val)	(((val) >> 16) & 0xff)

#define maya_sw_info	snd_ctl_boolean_mono_info

static int maya_sw_get(struct snd_kcontrol *kcontrol,
		       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_maya44 *chip = snd_kcontrol_chip(kcontrol);
	struct snd_wm8776 *wm =
		&chip->wm[snd_ctl_get_ioff(kcontrol, &ucontrol->id)];
	unsigned int idx = GET_SW_VAL_IDX(kcontrol->private_value);

	ucontrol->value.integer.value[0] = (wm->switch_bits >> idx) & 1;
	return 0;
}

static int maya_sw_put(struct snd_kcontrol *kcontrol,
		       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_maya44 *chip = snd_kcontrol_chip(kcontrol);
	struct snd_wm8776 *wm =
		&chip->wm[snd_ctl_get_ioff(kcontrol, &ucontrol->id)];
	unsigned int idx = GET_SW_VAL_IDX(kcontrol->private_value);
	unsigned int mask, val;
	int changed;

	mutex_lock(&chip->mutex);
	mask = 1 << idx;
	wm->switch_bits &= ~mask;
	val = ucontrol->value.integer.value[0];
	if (val)
		wm->switch_bits |= mask;
	mask = GET_SW_VAL_MASK(kcontrol->private_value);
	changed = wm8776_write_bits(chip->ice, wm,
				    GET_SW_VAL_REG(kcontrol->private_value),
				    mask, val ? mask : 0);
	mutex_unlock(&chip->mutex);
	return changed;
}

/*
 * GPIO pins (known ones for maya44)
 */
#define GPIO_PHANTOM_OFF	2
#define GPIO_MIC_RELAY		4
#define GPIO_SPDIF_IN_INV	5
#define GPIO_MUST_BE_0		7

/*
 * GPIO switch controls
 */

#define COMPOSE_GPIO_VAL(shift, inv)	((shift) | ((inv) << 8))
#define GET_GPIO_VAL_SHIFT(val)		((val) & 0xff)
#define GET_GPIO_VAL_INV(val)		(((val) >> 8) & 1)

static int maya_set_gpio_bits(struct snd_ice1712 *ice, unsigned int mask,
			      unsigned int bits)
{
	unsigned int data;
	data = snd_ice1712_gpio_read(ice);
	if ((data & mask) == bits)
		return 0;
	snd_ice1712_gpio_write(ice, (data & ~mask) | bits);
	return 1;
}

#define maya_gpio_sw_info	snd_ctl_boolean_mono_info

static int maya_gpio_sw_get(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_maya44 *chip = snd_kcontrol_chip(kcontrol);
	unsigned int shift = GET_GPIO_VAL_SHIFT(kcontrol->private_value);
	unsigned int val;

	val = (snd_ice1712_gpio_read(chip->ice) >> shift) & 1;
	if (GET_GPIO_VAL_INV(kcontrol->private_value))
		val = !val;
	ucontrol->value.integer.value[0] = val;
	return 0;
}

static int maya_gpio_sw_put(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_maya44 *chip = snd_kcontrol_chip(kcontrol);
	unsigned int shift = GET_GPIO_VAL_SHIFT(kcontrol->private_value);
	unsigned int val, mask;
	int changed;

	mutex_lock(&chip->mutex);
	mask = 1 << shift;
	val = ucontrol->value.integer.value[0];
	if (GET_GPIO_VAL_INV(kcontrol->private_value))
		val = !val;
	val = val ? mask : 0;
	changed = maya_set_gpio_bits(chip->ice, mask, val);
	mutex_unlock(&chip->mutex);
	return changed;
}

/*
 * capture source selection
 */

/* known working input slots (0-4) */
#define MAYA_LINE_IN	1	/* in-2 */
#define MAYA_MIC_IN	3	/* in-4 */

static void wm8776_select_input(struct snd_maya44 *chip, int idx, int line)
{
	wm8776_write_bits(chip->ice, &chip->wm[idx], WM8776_REG_ADC_MUX,
			  0x1f, 1 << line);
}

static int maya_rec_src_info(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_info *uinfo)
{
	static const char * const texts[] = { "Line", "Mic" };

	return snd_ctl_enum_info(uinfo, 1, ARRAY_SIZE(texts), texts);
}

static int maya_rec_src_get(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_maya44 *chip = snd_kcontrol_chip(kcontrol);
	int sel;

	if (snd_ice1712_gpio_read(chip->ice) & (1 << GPIO_MIC_RELAY))
		sel = 1;
	else
		sel = 0;
	ucontrol->value.enumerated.item[0] = sel;
	return 0;
}

static int maya_rec_src_put(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_value *ucontrol)
{
	struct snd_maya44 *chip = snd_kcontrol_chip(kcontrol);
	int sel = ucontrol->value.enumerated.item[0];
	int changed;

	mutex_lock(&chip->mutex);
	changed = maya_set_gpio_bits(chip->ice, 1 << GPIO_MIC_RELAY,
				     sel ? (1 << GPIO_MIC_RELAY) : 0);
	wm8776_select_input(chip, 0, sel ? MAYA_MIC_IN : MAYA_LINE_IN);
	mutex_unlock(&chip->mutex);
	return changed;
}

/*
 * Maya44 routing switch settings have different meanings than the standard
 * ice1724 switches as defined in snd_vt1724_pro_route_info (ice1724.c).
 */
static int maya_pb_route_info(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_info *uinfo)
{
	static const char * const texts[] = {
		"PCM Out", /* 0 */
		"Input 1", "Input 2", "Input 3", "Input 4"
	};

	return snd_ctl_enum_info(uinfo, 1, ARRAY_SIZE(texts), texts);
}

static int maya_pb_route_shift(int idx)
{
	static const unsigned char shift[10] =
		{ 8, 20, 0, 3, 11, 23, 14, 26, 17, 29 };
	return shift[idx % 10];
}

static int maya_pb_route_get(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_maya44 *chip = snd_kcontrol_chip(kcontrol);
	int idx = snd_ctl_get_ioffidx(kcontrol, &ucontrol->id);
	ucontrol->value.enumerated.item[0] =
		snd_ice1724_get_route_val(chip->ice, maya_pb_route_shift(idx));
	return 0;
}

static int maya_pb_route_put(struct snd_kcontrol *kcontrol,
			     struct snd_ctl_elem_value *ucontrol)
{
	struct snd_maya44 *chip = snd_kcontrol_chip(kcontrol);
	int idx = snd_ctl_get_ioffidx(kcontrol, &ucontrol->id);
	return snd_ice1724_put_route_val(chip->ice,
					 ucontrol->value.enumerated.item[0],
					 maya_pb_route_shift(idx));
}


/*
 * controls to be added
 */

static const struct snd_kcontrol_new maya_controls[] = {
	{
		.name = "Crossmix Playback Volume",
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.access = SNDRV_CTL_ELEM_ACCESS_READWRITE |
			SNDRV_CTL_ELEM_ACCESS_TLV_READ,
		.info = maya_vol_info,
		.get = maya_vol_get,
		.put = maya_vol_put,
		.tlv = { .p = db_scale_hp },
		.private_value = WM_VOL_HP,
		.count = 2,
	},
	{
		.name = "PCM Playback Volume",
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.access = SNDRV_CTL_ELEM_ACCESS_READWRITE |
			SNDRV_CTL_ELEM_ACCESS_TLV_READ,
		.info = maya_vol_info,
		.get = maya_vol_get,
		.put = maya_vol_put,
		.tlv = { .p = db_scale_dac },
		.private_value = WM_VOL_DAC,
		.count = 2,
	},
	{
		.name = "Line Capture Volume",
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.access = SNDRV_CTL_ELEM_ACCESS_READWRITE |
			SNDRV_CTL_ELEM_ACCESS_TLV_READ,
		.info = maya_vol_info,
		.get = maya_vol_get,
		.put = maya_vol_put,
		.tlv = { .p = db_scale_adc },
		.private_value = WM_VOL_ADC,
		.count = 2,
	},
	{
		.name = "PCM Playback Switch",
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.info = maya_sw_info,
		.get = maya_sw_get,
		.put = maya_sw_put,
		.private_value = COMPOSE_SW_VAL(WM_SW_DAC,
						WM8776_REG_OUTPUT_MUX, 0x01),
		.count = 2,
	},
	{
		.name = "Bypass Playback Switch",
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.info = maya_sw_info,
		.get = maya_sw_get,
		.put = maya_sw_put,
		.private_value = COMPOSE_SW_VAL(WM_SW_BYPASS,
						WM8776_REG_OUTPUT_MUX, 0x04),
		.count = 2,
	},
	{
		.name = "Capture Source",
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.info = maya_rec_src_info,
		.get = maya_rec_src_get,
		.put = maya_rec_src_put,
	},
	{
		.name = "Mic Phantom Power Switch",
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.info = maya_gpio_sw_info,
		.get = maya_gpio_sw_get,
		.put = maya_gpio_sw_put,
		.private_value = COMPOSE_GPIO_VAL(GPIO_PHANTOM_OFF, 1),
	},
	{
		.name = "SPDIF Capture Switch",
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.info = maya_gpio_sw_info,
		.get = maya_gpio_sw_get,
		.put = maya_gpio_sw_put,
		.private_value = COMPOSE_GPIO_VAL(GPIO_SPDIF_IN_INV, 1),
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "H/W Playback Route",
		.info = maya_pb_route_info,
		.get = maya_pb_route_get,
		.put = maya_pb_route_put,
		.count = 4,  /* FIXME: do controls 5-9 have any meaning? */
	},
};

static int maya44_add_controls(struct snd_ice1712 *ice)
{
	int err, i;

	for (i = 0; i < ARRAY_SIZE(maya_controls); i++) {
		err = snd_ctl_add(ice->card, snd_ctl_new1(&maya_controls[i],
							  ice->spec));
		if (err < 0)
			return err;
	}
	return 0;
}


/*
 * initialize a wm8776 chip
 */
static void wm8776_init(struct snd_ice1712 *ice,
			struct snd_wm8776 *wm, unsigned int addr)
{
	static const unsigned short inits_wm8776[] = {
		0x02, 0x100, /* R2: headphone L+R muted + update */
		0x05, 0x100, /* R5: DAC output L+R muted + update */
		0x06, 0x000, /* R6: DAC output phase normal */
		0x07, 0x091, /* R7: DAC enable zero cross detection,
				normal output */
		0x08, 0x000, /* R8: DAC soft mute off */
		0x09, 0x000, /* R9: no deemph, DAC zero detect disabled */
		0x0a, 0x022, /* R10: DAC I2C mode, std polarities, 24bit */
		0x0b, 0x022, /* R11: ADC I2C mode, std polarities, 24bit,
				highpass filter enabled */
		0x0c, 0x042, /* R12: ADC+DAC slave, ADC+DAC 44,1kHz */
		0x0d, 0x000, /* R13: all power up */
		0x0e, 0x100, /* R14: ADC left muted,
				enable zero cross detection */
		0x0f, 0x100, /* R15: ADC right muted,
				enable zero cross detection */
			     /* R16: ALC...*/
		0x11, 0x000, /* R17: disable ALC */
			     /* R18: ALC...*/
			     /* R19: noise gate...*/
		0x15, 0x000, /* R21: ADC input mux init, mute all inputs */
		0x16, 0x001, /* R22: output mux, select DAC */
		0xff, 0xff
	};

	const unsigned short *ptr;
	unsigned char reg;
	unsigned short data;

	wm->addr = addr;
	/* enable DAC output; mute bypass, aux & all inputs */
	wm->switch_bits = (1 << WM_SW_DAC);

	ptr = inits_wm8776;
	while (*ptr != 0xff) {
		reg = *ptr++;
		data = *ptr++;
		wm8776_write(ice, wm, reg, data);
	}
}


/*
 * change the rate on the WM8776 codecs.
 * this assumes that the VT17xx's rate is changed by the calling function.
 * NOTE: even though the WM8776's are running in slave mode and rate
 * selection is automatic, we need to call snd_wm8776_set_rate() here
 * to make sure some flags are set correctly.
 */
static void set_rate(struct snd_ice1712 *ice, unsigned int rate)
{
	struct snd_maya44 *chip = ice->spec;
	unsigned int ratio, adc_ratio, val;
	int i;

	switch (rate) {
	case 192000:
		ratio = WM8776_CLOCK_RATIO_128FS;
		break;
	case 176400:
		ratio = WM8776_CLOCK_RATIO_128FS;
		break;
	case 96000:
		ratio = WM8776_CLOCK_RATIO_256FS;
		break;
	case 88200:
		ratio = WM8776_CLOCK_RATIO_384FS;
		break;
	case 48000:
		ratio = WM8776_CLOCK_RATIO_512FS;
		break;
	case 44100:
		ratio = WM8776_CLOCK_RATIO_512FS;
		break;
	case 32000:
		ratio = WM8776_CLOCK_RATIO_768FS;
		break;
	case 0:
		/* no hint - S/PDIF input is master, simply return */
		return;
	default:
		snd_BUG();
		return;
	}

	/*
	 * this currently sets the same rate for ADC and DAC, but limits
	 * ADC rate to 256X (96kHz). For 256X mode (96kHz), this sets ADC
	 * oversampling to 64x, as recommended by WM8776 datasheet.
	 * Setting the rate is not really necessary in slave mode.
	 */
	adc_ratio = ratio;
	if (adc_ratio < WM8776_CLOCK_RATIO_256FS)
		adc_ratio = WM8776_CLOCK_RATIO_256FS;

	val = adc_ratio;
	if (adc_ratio == WM8776_CLOCK_RATIO_256FS)
		val |= 8;
	val |= ratio << 4;

	mutex_lock(&chip->mutex);
	for (i = 0; i < 2; i++)
		wm8776_write_bits(ice, &chip->wm[i],
				  WM8776_REG_MASTER_MODE_CONTROL,
				  0x180, val);
	mutex_unlock(&chip->mutex);
}

/*
 * supported sample rates (to override the default one)
 */

static const unsigned int rates[] = {
	32000, 44100, 48000, 64000, 88200, 96000, 176400, 192000
};

/* playback rates: 32..192 kHz */
static const struct snd_pcm_hw_constraint_list dac_rates = {
	.count = ARRAY_SIZE(rates),
	.list = rates,
	.mask = 0
};


/*
 * chip addresses on I2C bus
 */
static const unsigned char wm8776_addr[2] = {
	0x34, 0x36, /* codec 0 & 1 */
};

/*
 * initialize the chip
 */
static int maya44_init(struct snd_ice1712 *ice)
{
	int i;
	struct snd_maya44 *chip;

	chip = kzalloc(sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;
	mutex_init(&chip->mutex);
	chip->ice = ice;
	ice->spec = chip;

	/* initialise codecs */
	ice->num_total_dacs = 4;
	ice->num_total_adcs = 4;
	ice->akm_codecs = 0;

	for (i = 0; i < 2; i++) {
		wm8776_init(ice, &chip->wm[i], wm8776_addr[i]);
		wm8776_select_input(chip, i, MAYA_LINE_IN);
	}

	/* set card specific rates */
	ice->hw_rates = &dac_rates;

	/* register change rate notifier */
	ice->gpio.set_pro_rate = set_rate;

	/* RDMA1 (2nd input channel) is used for ADC by default */
	ice->force_rdma1 = 1;

	/* have an own routing control */
	ice->own_routing = 1;

	return 0;
}


/*
 * Maya44 boards don't provide the EEPROM data except for the vendor IDs.
 * hence the driver needs to sets up it properly.
 */

static const unsigned char maya44_eeprom[] = {
	[ICE_EEP2_SYSCONF]     = 0x45,
		/* clock xin1=49.152MHz, mpu401, 2 stereo ADCs+DACs */
	[ICE_EEP2_ACLINK]      = 0x80,
		/* I2S */
	[ICE_EEP2_I2S]         = 0xf8,
		/* vol, 96k, 24bit, 192k */
	[ICE_EEP2_SPDIF]       = 0xc3,
		/* enable spdif out, spdif out supp, spdif-in, ext spdif out */
	[ICE_EEP2_GPIO_DIR]    = 0xff,
	[ICE_EEP2_GPIO_DIR1]   = 0xff,
	[ICE_EEP2_GPIO_DIR2]   = 0xff,
	[ICE_EEP2_GPIO_MASK]   = 0/*0x9f*/,
	[ICE_EEP2_GPIO_MASK1]  = 0/*0xff*/,
	[ICE_EEP2_GPIO_MASK2]  = 0/*0x7f*/,
	[ICE_EEP2_GPIO_STATE]  = (1 << GPIO_PHANTOM_OFF) |
			(1 << GPIO_SPDIF_IN_INV),
	[ICE_EEP2_GPIO_STATE1] = 0x00,
	[ICE_EEP2_GPIO_STATE2] = 0x00,
};

/* entry point */
struct snd_ice1712_card_info snd_vt1724_maya44_cards[] = {
	{
		.subvendor = VT1724_SUBDEVICE_MAYA44,
		.name = "ESI Maya44",
		.model = "maya44",
		.chip_init = maya44_init,
		.build_controls = maya44_add_controls,
		.eeprom_size = sizeof(maya44_eeprom),
		.eeprom_data = maya44_eeprom,
	},
	{ } /* terminator */
};
