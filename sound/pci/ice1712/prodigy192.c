// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *   ALSA driver for ICEnsemble VT1724 (Envy24HT)
 *
 *   Lowlevel functions for AudioTrak Prodigy 192 cards
 *   Supported IEC958 input from optional MI/ODI/O add-on card.
 *
 *   Specifics (SW, HW):
 *   -------------------
 *   	* 49.5MHz crystal
 *   	* SPDIF-OUT on the card:
 *  	  - coax (through isolation transformer)/toslink supplied by
 *          74HC04 gates - 3 in parallel
 *   	  - output switched between on-board CD drive dig-out connector
 *          and ice1724 SPDTX pin, using 74HC02 NOR gates, controlled
 *          by GPIO20 (0 = CD dig-out, 1 = SPDTX)
 *   	* SPDTX goes straight to MI/ODI/O card's SPDIF-OUT coax
 *
 *   	* MI/ODI/O card: AK4114 based, used for iec958 input only
 *   		- toslink input -> RX0
 *   		- coax input -> RX1
 *   		- 4wire protocol:
 *   			AK4114		ICE1724
 *   			------------------------------
 * 			CDTO (pin 32) -- GPIO11 pin 86
 * 			CDTI (pin 33) -- GPIO10 pin 77
 * 			CCLK (pin 34) -- GPIO9 pin 76
 * 			CSN  (pin 35) -- GPIO8 pin 75
 *   		- output data Mode 7 (24bit, I2S, slave)
 *		- both MCKO1 and MCKO2 of ak4114 are fed to FPGA, which
 *		  outputs master clock to SPMCLKIN of ice1724.
 *		  Experimentally I found out that only a combination of
 *		  OCKS0=1, OCKS1=1 (128fs, 64fs output) and ice1724 -
 *		  VT1724_MT_I2S_MCLK_128X=0 (256fs input) yields correct
 *		  sampling rate. That means that the FPGA doubles the
 *		  MCK01 rate.
 *
 *	Copyright (c) 2003 Takashi Iwai <tiwai@suse.de>
 *      Copyright (c) 2003 Dimitromanolakis Apostolos <apostol@cs.utoronto.ca>
 *      Copyright (c) 2004 Kouichi ONO <co2b@ceres.dti.ne.jp>
 */      

#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <sound/core.h>

#include "ice1712.h"
#include "envy24ht.h"
#include "prodigy192.h"
#include "stac946x.h"
#include <sound/tlv.h>

struct prodigy192_spec {
	struct ak4114 *ak4114;
	/* rate change needs atomic mute/unmute of all dacs*/
	struct mutex mute_mutex;
};

static inline void stac9460_put(struct snd_ice1712 *ice, int reg, unsigned char val)
{
	snd_vt1724_write_i2c(ice, PRODIGY192_STAC9460_ADDR, reg, val);
}

static inline unsigned char stac9460_get(struct snd_ice1712 *ice, int reg)
{
	return snd_vt1724_read_i2c(ice, PRODIGY192_STAC9460_ADDR, reg);
}

/*
 * DAC mute control
 */

/*
 * idx = STAC9460 volume register number, mute: 0 = mute, 1 = unmute
 */
static int stac9460_dac_mute(struct snd_ice1712 *ice, int idx,
		unsigned char mute)
{
	unsigned char new, old;
	int change;
	old = stac9460_get(ice, idx);
	new = (~mute << 7 & 0x80) | (old & ~0x80);
	change = (new != old);
	if (change)
		/* dev_dbg(ice->card->dev, "Volume register 0x%02x: 0x%02x\n", idx, new);*/
		stac9460_put(ice, idx, new);
	return change;
}

#define stac9460_dac_mute_info		snd_ctl_boolean_mono_info

static int stac9460_dac_mute_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
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

static int stac9460_dac_mute_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	struct prodigy192_spec *spec = ice->spec;
	int idx, change;

	if (kcontrol->private_value)
		idx = STAC946X_MASTER_VOLUME;
	else
		idx  = snd_ctl_get_ioffidx(kcontrol, &ucontrol->id) + STAC946X_LF_VOLUME;
	/* due to possible conflicts with stac9460_set_rate_val, mutexing */
	mutex_lock(&spec->mute_mutex);
	/*
	dev_dbg(ice->card->dev, "Mute put: reg 0x%02x, ctrl value: 0x%02x\n", idx,
	       ucontrol->value.integer.value[0]);
	*/
	change = stac9460_dac_mute(ice, idx, ucontrol->value.integer.value[0]);
	mutex_unlock(&spec->mute_mutex);
	return change;
}

/*
 * DAC volume attenuation mixer control
 */
static int stac9460_dac_vol_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;			/* mute */
	uinfo->value.integer.max = 0x7f;		/* 0dB */
	return 0;
}

static int stac9460_dac_vol_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
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

static int stac9460_dac_vol_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
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
		ovol =  (0x7f - nvol) | (tmp & 0x80);
		/*
		dev_dbg(ice->card->dev, "DAC Volume: reg 0x%02x: 0x%02x\n",
		       idx, ovol);
		*/
		stac9460_put(ice, idx, (0x7f - nvol) | (tmp & 0x80));
	}
	return change;
}

/*
 * ADC mute control
 */
#define stac9460_adc_mute_info		snd_ctl_boolean_stereo_info

static int stac9460_adc_mute_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	unsigned char val;
	int i;

	for (i = 0; i < 2; ++i) {
		val = stac9460_get(ice, STAC946X_MIC_L_VOLUME + i);
		ucontrol->value.integer.value[i] = ~val>>7 & 0x1;
	}

	return 0;
}

static int stac9460_adc_mute_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
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
static int stac9460_adc_vol_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;		/* 0dB */
	uinfo->value.integer.max = 0x0f;	/* 22.5dB */
	return 0;
}

static int stac9460_adc_vol_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	int i, reg;
	unsigned char vol;

	for (i = 0; i < 2; ++i) {
		reg = STAC946X_MIC_L_VOLUME + i;
		vol = stac9460_get(ice, reg) & 0x0f;
		ucontrol->value.integer.value[i] = 0x0f - vol;
	}

	return 0;
}

static int stac9460_adc_vol_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	int i, reg;
	unsigned char ovol, nvol;
	int change;

	for (i = 0; i < 2; ++i) {
		reg = STAC946X_MIC_L_VOLUME + i;
		nvol = ucontrol->value.integer.value[i] & 0x0f;
		ovol = 0x0f - stac9460_get(ice, reg);
		change = ((ovol & 0x0f)  != nvol);
		if (change)
			stac9460_put(ice, reg, (0x0f - nvol) | (ovol & ~0x0f));
	}

	return change;
}

static int stac9460_mic_sw_info(struct snd_kcontrol *kcontrol,
	       			struct snd_ctl_elem_info *uinfo)
{
	static const char * const texts[2] = { "Line In", "Mic" };

	return snd_ctl_enum_info(uinfo, 1, 2, texts);
}


static int stac9460_mic_sw_get(struct snd_kcontrol *kcontrol,
	       		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	unsigned char val;
		
	val = stac9460_get(ice, STAC946X_GENERAL_PURPOSE);
	ucontrol->value.enumerated.item[0] = (val >> 7) & 0x1;
	return 0;
}

static int stac9460_mic_sw_put(struct snd_kcontrol *kcontrol,
	       		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	unsigned char new, old;
	int change;
	old = stac9460_get(ice, STAC946X_GENERAL_PURPOSE);
	new = (ucontrol->value.enumerated.item[0] << 7 & 0x80) | (old & ~0x80);
	change = (new != old);
	if (change)
		stac9460_put(ice, STAC946X_GENERAL_PURPOSE, new);
	return change;
}
/*
 * Handler for setting correct codec rate - called when rate change is detected
 */
static void stac9460_set_rate_val(struct snd_ice1712 *ice, unsigned int rate)
{
	unsigned char old, new;
	int idx;
	unsigned char changed[7];
	struct prodigy192_spec *spec = ice->spec;

	if (rate == 0)  /* no hint - S/PDIF input is master, simply return */
		return;
	else if (rate <= 48000)
		new = 0x08;	/* 256x, base rate mode */
	else if (rate <= 96000)
		new = 0x11;	/* 256x, mid rate mode */
	else
		new = 0x12;	/* 128x, high rate mode */
	old = stac9460_get(ice, STAC946X_MASTER_CLOCKING);
	if (old == new)
		return;
	/* change detected, setting master clock, muting first */
	/* due to possible conflicts with mute controls - mutexing */
	mutex_lock(&spec->mute_mutex);
	/* we have to remember current mute status for each DAC */
	for (idx = 0; idx < 7 ; ++idx)
		changed[idx] = stac9460_dac_mute(ice,
				STAC946X_MASTER_VOLUME + idx, 0);
	/*dev_dbg(ice->card->dev, "Rate change: %d, new MC: 0x%02x\n", rate, new);*/
	stac9460_put(ice, STAC946X_MASTER_CLOCKING, new);
	udelay(10);
	/* unmuting - only originally unmuted dacs -
	 * i.e. those changed when muting */
	for (idx = 0; idx < 7 ; ++idx) {
		if (changed[idx])
			stac9460_dac_mute(ice, STAC946X_MASTER_VOLUME + idx, 1);
	}
	mutex_unlock(&spec->mute_mutex);
}


static const DECLARE_TLV_DB_SCALE(db_scale_dac, -19125, 75, 0);
static const DECLARE_TLV_DB_SCALE(db_scale_adc, 0, 150, 0);

/*
 * mixers
 */

static const struct snd_kcontrol_new stac_controls[] = {
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Master Playback Switch",
		.info = stac9460_dac_mute_info,
		.get = stac9460_dac_mute_get,
		.put = stac9460_dac_mute_put,
		.private_value = 1,
		.tlv = { .p = db_scale_dac }
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.access = (SNDRV_CTL_ELEM_ACCESS_READWRITE |
			   SNDRV_CTL_ELEM_ACCESS_TLV_READ),
		.name = "Master Playback Volume",
		.info = stac9460_dac_vol_info,
		.get = stac9460_dac_vol_get,
		.put = stac9460_dac_vol_put,
		.private_value = 1,
		.tlv = { .p = db_scale_dac }
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
		.access = (SNDRV_CTL_ELEM_ACCESS_READWRITE |
			   SNDRV_CTL_ELEM_ACCESS_TLV_READ),
		.name = "DAC Volume",
		.count = 6,
		.info = stac9460_dac_vol_info,
		.get = stac9460_dac_vol_get,
		.put = stac9460_dac_vol_put,
		.tlv = { .p = db_scale_dac }
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "ADC Capture Switch",
		.count = 1,
		.info = stac9460_adc_mute_info,
		.get = stac9460_adc_mute_get,
		.put = stac9460_adc_mute_put,

	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.access = (SNDRV_CTL_ELEM_ACCESS_READWRITE |
			   SNDRV_CTL_ELEM_ACCESS_TLV_READ),
		.name = "ADC Capture Volume",
		.count = 1,
		.info = stac9460_adc_vol_info,
		.get = stac9460_adc_vol_get,
		.put = stac9460_adc_vol_put,
		.tlv = { .p = db_scale_adc }
	},
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "Analog Capture Input",
		.info = stac9460_mic_sw_info,
		.get = stac9460_mic_sw_get,
		.put = stac9460_mic_sw_put,

	},
};

/* AK4114 - ICE1724 connections on Prodigy192 + MI/ODI/O */
/* CDTO (pin 32) -- GPIO11 pin 86
 * CDTI (pin 33) -- GPIO10 pin 77
 * CCLK (pin 34) -- GPIO9 pin 76
 * CSN  (pin 35) -- GPIO8 pin 75
 */
#define AK4114_ADDR	0x00 /* C1-C0: Chip Address
			      * (According to datasheet fixed to “00”)
			      */

/*
 * 4wire ak4114 protocol - writing data
 */
static void write_data(struct snd_ice1712 *ice, unsigned int gpio,
		       unsigned int data, int idx)
{
	for (; idx >= 0; idx--) {
		/* drop clock */
		gpio &= ~VT1724_PRODIGY192_CCLK;
		snd_ice1712_gpio_write(ice, gpio);
		udelay(1);
		/* set data */
		if (data & (1 << idx))
			gpio |= VT1724_PRODIGY192_CDOUT;
		else
			gpio &= ~VT1724_PRODIGY192_CDOUT;
		snd_ice1712_gpio_write(ice, gpio);
		udelay(1);
		/* raise clock */
		gpio |= VT1724_PRODIGY192_CCLK;
		snd_ice1712_gpio_write(ice, gpio);
		udelay(1);
	}
}

/*
 * 4wire ak4114 protocol - reading data
 */
static unsigned char read_data(struct snd_ice1712 *ice, unsigned int gpio,
			       int idx)
{
	unsigned char data = 0;

	for (; idx >= 0; idx--) {
		/* drop clock */
		gpio &= ~VT1724_PRODIGY192_CCLK;
		snd_ice1712_gpio_write(ice, gpio);
		udelay(1);
		/* read data */
		if (snd_ice1712_gpio_read(ice) & VT1724_PRODIGY192_CDIN)
			data |= (1 << idx);
		udelay(1);
		/* raise clock */
		gpio |= VT1724_PRODIGY192_CCLK;
		snd_ice1712_gpio_write(ice, gpio);
		udelay(1);
	}
	return data;
}
/*
 * 4wire ak4114 protocol - starting sequence
 */
static unsigned int prodigy192_4wire_start(struct snd_ice1712 *ice)
{
	unsigned int tmp;

	snd_ice1712_save_gpio_status(ice);
	tmp = snd_ice1712_gpio_read(ice);

	tmp |= VT1724_PRODIGY192_CCLK; /* high at init */
	tmp &= ~VT1724_PRODIGY192_CS; /* drop chip select */
	snd_ice1712_gpio_write(ice, tmp);
	udelay(1);
	return tmp;
}

/*
 * 4wire ak4114 protocol - final sequence
 */
static void prodigy192_4wire_finish(struct snd_ice1712 *ice, unsigned int tmp)
{
	tmp |= VT1724_PRODIGY192_CS; /* raise chip select */
	snd_ice1712_gpio_write(ice, tmp);
	udelay(1);
	snd_ice1712_restore_gpio_status(ice);
}

/*
 * Write data to addr register of ak4114
 */
static void prodigy192_ak4114_write(void *private_data, unsigned char addr,
			       unsigned char data)
{
	struct snd_ice1712 *ice = private_data;
	unsigned int tmp, addrdata;
	tmp = prodigy192_4wire_start(ice);
	addrdata = (AK4114_ADDR << 6) | 0x20 | (addr & 0x1f);
	addrdata = (addrdata << 8) | data;
	write_data(ice, tmp, addrdata, 15);
	prodigy192_4wire_finish(ice, tmp);
}

/*
 * Read data from addr register of ak4114
 */
static unsigned char prodigy192_ak4114_read(void *private_data,
					    unsigned char addr)
{
	struct snd_ice1712 *ice = private_data;
	unsigned int tmp;
	unsigned char data;

	tmp = prodigy192_4wire_start(ice);
	write_data(ice, tmp, (AK4114_ADDR << 6) | (addr & 0x1f), 7);
	data = read_data(ice, tmp, 7);
	prodigy192_4wire_finish(ice, tmp);
	return data;
}


static int ak4114_input_sw_info(struct snd_kcontrol *kcontrol,
	       			struct snd_ctl_elem_info *uinfo)
{
	static const char * const texts[2] = { "Toslink", "Coax" };

	return snd_ctl_enum_info(uinfo, 1, 2, texts);
}


static int ak4114_input_sw_get(struct snd_kcontrol *kcontrol,
	       		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	unsigned char val;
		
	val = prodigy192_ak4114_read(ice, AK4114_REG_IO1);
	/* AK4114_IPS0 bit = 0 -> RX0 = Toslink
	 * AK4114_IPS0 bit = 1 -> RX1 = Coax
	 */
	ucontrol->value.enumerated.item[0] = (val & AK4114_IPS0) ? 1 : 0;
	return 0;
}

static int ak4114_input_sw_put(struct snd_kcontrol *kcontrol,
	       		struct snd_ctl_elem_value *ucontrol)
{
	struct snd_ice1712 *ice = snd_kcontrol_chip(kcontrol);
	unsigned char new, old, itemvalue;
	int change;

	old = prodigy192_ak4114_read(ice, AK4114_REG_IO1);
	/* AK4114_IPS0 could be any bit */
	itemvalue = (ucontrol->value.enumerated.item[0]) ? 0xff : 0x00;

	new = (itemvalue & AK4114_IPS0) | (old & ~AK4114_IPS0);
	change = (new != old);
	if (change)
		prodigy192_ak4114_write(ice, AK4114_REG_IO1, new);
	return change;
}


static const struct snd_kcontrol_new ak4114_controls[] = {
	{
		.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
		.name = "MIODIO IEC958 Capture Input",
		.info = ak4114_input_sw_info,
		.get = ak4114_input_sw_get,
		.put = ak4114_input_sw_put,

	}
};


static int prodigy192_ak4114_init(struct snd_ice1712 *ice)
{
	static const unsigned char ak4114_init_vals[] = {
		AK4114_RST | AK4114_PWN | AK4114_OCKS0 | AK4114_OCKS1,
		/* ice1724 expects I2S and provides clock,
		 * DEM0 disables the deemphasis filter
		 */
		AK4114_DIF_I24I2S | AK4114_DEM0 ,
		AK4114_TX1E,
		AK4114_EFH_1024 | AK4114_DIT, /* default input RX0 */
		0,
		0
	};
	static const unsigned char ak4114_init_txcsb[] = {
		0x41, 0x02, 0x2c, 0x00, 0x00
	};
	struct prodigy192_spec *spec = ice->spec;
	int err;

	err = snd_ak4114_create(ice->card,
				 prodigy192_ak4114_read,
				 prodigy192_ak4114_write,
				 ak4114_init_vals, ak4114_init_txcsb,
				 ice, &spec->ak4114);
	if (err < 0)
		return err;
	/* AK4114 in Prodigy192 cannot detect external rate correctly.
	 * No reason to stop capture stream due to incorrect checks */
	spec->ak4114->check_flags = AK4114_CHECK_NO_RATE;
	return 0;
}

static void stac9460_proc_regs_read(struct snd_info_entry *entry,
		struct snd_info_buffer *buffer)
{
	struct snd_ice1712 *ice = entry->private_data;
	int reg, val;
	/* registers 0x0 - 0x14 */
	for (reg = 0; reg <= 0x15; reg++) {
		val = stac9460_get(ice, reg);
		snd_iprintf(buffer, "0x%02x = 0x%02x\n", reg, val);
	}
}


static void stac9460_proc_init(struct snd_ice1712 *ice)
{
	snd_card_ro_proc_new(ice->card, "stac9460_codec", ice,
			     stac9460_proc_regs_read);
}


static int prodigy192_add_controls(struct snd_ice1712 *ice)
{
	struct prodigy192_spec *spec = ice->spec;
	unsigned int i;
	int err;

	for (i = 0; i < ARRAY_SIZE(stac_controls); i++) {
		err = snd_ctl_add(ice->card,
				  snd_ctl_new1(&stac_controls[i], ice));
		if (err < 0)
			return err;
	}
	if (spec->ak4114) {
		/* ak4114 is connected */
		for (i = 0; i < ARRAY_SIZE(ak4114_controls); i++) {
			err = snd_ctl_add(ice->card,
					  snd_ctl_new1(&ak4114_controls[i],
						       ice));
			if (err < 0)
				return err;
		}
		err = snd_ak4114_build(spec->ak4114,
				NULL, /* ak4114 in MIO/DI/O handles no IEC958 output */
				ice->pcm->streams[SNDRV_PCM_STREAM_CAPTURE].substream);
		if (err < 0)
			return err;
	}
	stac9460_proc_init(ice);
	return 0;
}

/*
 * check for presence of MI/ODI/O add-on card with digital inputs
 */
static int prodigy192_miodio_exists(struct snd_ice1712 *ice)
{

	unsigned char orig_value;
	const unsigned char test_data = 0xd1;	/* random value */
	unsigned char addr = AK4114_REG_INT0_MASK; /* random SAFE address */
	int exists = 0;

	orig_value = prodigy192_ak4114_read(ice, addr);
	prodigy192_ak4114_write(ice, addr, test_data);
	if (prodigy192_ak4114_read(ice, addr) == test_data) {
		/* ak4114 seems to communicate, apparently exists */
		/* writing back original value */
		prodigy192_ak4114_write(ice, addr, orig_value);
		exists = 1;
	}
	return exists;
}

/*
 * initialize the chip
 */
static int prodigy192_init(struct snd_ice1712 *ice)
{
	static const unsigned short stac_inits_prodigy[] = {
		STAC946X_RESET, 0,
		STAC946X_MASTER_CLOCKING, 0x11,
/*		STAC946X_MASTER_VOLUME, 0,
		STAC946X_LF_VOLUME, 0,
		STAC946X_RF_VOLUME, 0,
		STAC946X_LR_VOLUME, 0,
		STAC946X_RR_VOLUME, 0,
		STAC946X_CENTER_VOLUME, 0,
		STAC946X_LFE_VOLUME, 0,*/
		(unsigned short)-1
	};
	const unsigned short *p;
	int err = 0;
	struct prodigy192_spec *spec;

	/* prodigy 192 */
	ice->num_total_dacs = 6;
	ice->num_total_adcs = 2;
	ice->vt1720 = 0;  /* ice1724, e.g. 23 GPIOs */
	
	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec)
		return -ENOMEM;
	ice->spec = spec;
	mutex_init(&spec->mute_mutex);

	/* initialize codec */
	p = stac_inits_prodigy;
	for (; *p != (unsigned short)-1; p += 2)
		stac9460_put(ice, p[0], p[1]);
	ice->gpio.set_pro_rate = stac9460_set_rate_val;

	/* MI/ODI/O add on card with AK4114 */
	if (prodigy192_miodio_exists(ice)) {
		err = prodigy192_ak4114_init(ice);
		/* from this moment if err = 0 then
		 * spec->ak4114 should not be null
		 */
		dev_dbg(ice->card->dev,
			"AK4114 initialized with status %d\n", err);
	} else
		dev_dbg(ice->card->dev, "AK4114 not found\n");

	return err;
}


/*
 * Aureon boards don't provide the EEPROM data except for the vendor IDs.
 * hence the driver needs to sets up it properly.
 */

static const unsigned char prodigy71_eeprom[] = {
	[ICE_EEP2_SYSCONF]     = 0x6a,	/* 49MHz crystal, mpu401,
					 * spdif-in+ 1 stereo ADC,
					 * 3 stereo DACs
					 */
	[ICE_EEP2_ACLINK]      = 0x80,	/* I2S */
	[ICE_EEP2_I2S]         = 0xf8,	/* vol, 96k, 24bit, 192k */
	[ICE_EEP2_SPDIF]       = 0xc3,	/* out-en, out-int, spdif-in */
	[ICE_EEP2_GPIO_DIR]    = 0xff,
	[ICE_EEP2_GPIO_DIR1]   = ~(VT1724_PRODIGY192_CDIN >> 8) ,
	[ICE_EEP2_GPIO_DIR2]   = 0xbf,
	[ICE_EEP2_GPIO_MASK]   = 0x00,
	[ICE_EEP2_GPIO_MASK1]  = 0x00,
	[ICE_EEP2_GPIO_MASK2]  = 0x00,
	[ICE_EEP2_GPIO_STATE]  = 0x00,
	[ICE_EEP2_GPIO_STATE1] = 0x00,
	[ICE_EEP2_GPIO_STATE2] = 0x10,  /* GPIO20: 0 = CD drive dig. input
					 * passthrough,
					 * 1 = SPDIF-OUT from ice1724
					 */
};


/* entry point */
struct snd_ice1712_card_info snd_vt1724_prodigy192_cards[] = {
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
