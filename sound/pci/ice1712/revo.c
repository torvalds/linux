/*
 *   ALSA driver for ICEnsemble ICE1712 (Envy24)
 *
 *   Lowlevel functions for M-Audio Revolution 7.1
 *
 *	Copyright (c) 2003 Takashi Iwai <tiwai@suse.de>
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
#include "revo.h"

static void revo_i2s_mclk_changed(ice1712_t *ice)
{
	/* assert PRST# to converters; MT05 bit 7 */
	outb(inb(ICEMT1724(ice, AC97_CMD)) | 0x80, ICEMT1724(ice, AC97_CMD));
	mdelay(5);
	/* deassert PRST# */
	outb(inb(ICEMT1724(ice, AC97_CMD)) & ~0x80, ICEMT1724(ice, AC97_CMD));
}

/*
 * change the rate of envy24HT, AK4355 and AK4381
 */
static void revo_set_rate_val(akm4xxx_t *ak, unsigned int rate)
{
	unsigned char old, tmp, dfs;
	int reg, shift;

	if (rate == 0)	/* no hint - S/PDIF input is master, simply return */
		return;

	/* adjust DFS on codecs */
	if (rate > 96000)
		dfs = 2;
	else if (rate > 48000)
		dfs = 1;
	else
		dfs = 0;

	if (ak->type == SND_AK4355) {
		reg = 2;
		shift = 4;
	} else {
		reg = 1;
		shift = 3;
	}
	tmp = snd_akm4xxx_get(ak, 0, reg);
	old = (tmp >> shift) & 0x03;
	if (old == dfs)
		return;

	/* reset DFS */
	snd_akm4xxx_reset(ak, 1);
	tmp = snd_akm4xxx_get(ak, 0, reg);
	tmp &= ~(0x03 << shift);
	tmp |= dfs << shift;
	// snd_akm4xxx_write(ak, 0, reg, tmp);
	snd_akm4xxx_set(ak, 0, reg, tmp); /* the value is written in reset(0) */
	snd_akm4xxx_reset(ak, 0);
}

/*
 * initialize the chips on M-Audio Revolution cards
 */

static akm4xxx_t akm_revo_front __devinitdata = {
	.type = SND_AK4381,
	.num_dacs = 2,
	.ops = {
		.set_rate_val = revo_set_rate_val
	}
};

static struct snd_ak4xxx_private akm_revo_front_priv __devinitdata = {
	.caddr = 1,
	.cif = 0,
	.data_mask = VT1724_REVO_CDOUT,
	.clk_mask = VT1724_REVO_CCLK,
	.cs_mask = VT1724_REVO_CS0 | VT1724_REVO_CS1 | VT1724_REVO_CS2,
	.cs_addr = VT1724_REVO_CS0 | VT1724_REVO_CS2,
	.cs_none = VT1724_REVO_CS0 | VT1724_REVO_CS1 | VT1724_REVO_CS2,
	.add_flags = VT1724_REVO_CCLK, /* high at init */
	.mask_flags = 0,
};

static akm4xxx_t akm_revo_surround __devinitdata = {
	.type = SND_AK4355,
	.idx_offset = 1,
	.num_dacs = 6,
	.ops = {
		.set_rate_val = revo_set_rate_val
	}
};

static struct snd_ak4xxx_private akm_revo_surround_priv __devinitdata = {
	.caddr = 3,
	.cif = 0,
	.data_mask = VT1724_REVO_CDOUT,
	.clk_mask = VT1724_REVO_CCLK,
	.cs_mask = VT1724_REVO_CS0 | VT1724_REVO_CS1 | VT1724_REVO_CS2,
	.cs_addr = VT1724_REVO_CS0 | VT1724_REVO_CS1,
	.cs_none = VT1724_REVO_CS0 | VT1724_REVO_CS1 | VT1724_REVO_CS2,
	.add_flags = VT1724_REVO_CCLK, /* high at init */
	.mask_flags = 0,
};

static unsigned int rates[] = {
	32000, 44100, 48000, 64000, 88200, 96000,
	176400, 192000,
};

static snd_pcm_hw_constraint_list_t revo_rates = {
	.count = ARRAY_SIZE(rates),
	.list = rates,
	.mask = 0,
};

static int __devinit revo_init(ice1712_t *ice)
{
	akm4xxx_t *ak;
	int err;

	/* determine I2C, DACs and ADCs */
	switch (ice->eeprom.subvendor) {
	case VT1724_SUBDEVICE_REVOLUTION71:
		ice->num_total_dacs = 8;
		ice->num_total_adcs = 2;
		break;
	default:
		snd_BUG();
		return -EINVAL;
	}

	ice->gpio.i2s_mclk_changed = revo_i2s_mclk_changed;

	/* second stage of initialization, analog parts and others */
	ak = ice->akm = kcalloc(2, sizeof(akm4xxx_t), GFP_KERNEL);
	if (! ak)
		return -ENOMEM;
	ice->akm_codecs = 2;
	switch (ice->eeprom.subvendor) {
	case VT1724_SUBDEVICE_REVOLUTION71:
		if ((err = snd_ice1712_akm4xxx_init(ak, &akm_revo_front, &akm_revo_front_priv, ice)) < 0)
			return err;
		if ((err = snd_ice1712_akm4xxx_init(ak + 1, &akm_revo_surround, &akm_revo_surround_priv, ice)) < 0)
			return err;
		/* unmute all codecs */
		snd_ice1712_gpio_write_bits(ice, VT1724_REVO_MUTE, VT1724_REVO_MUTE);
		break;
	}

	ice->hw_rates = &revo_rates; /* AK codecs don't support lower than 32k */

	return 0;
}


static int __devinit revo_add_controls(ice1712_t *ice)
{
	int err;

	switch (ice->eeprom.subvendor) {
	case VT1724_SUBDEVICE_REVOLUTION71:
		err = snd_ice1712_akm4xxx_build_controls(ice);
		if (err < 0)
			return err;
	}
	return 0;
}

/* entry point */
struct snd_ice1712_card_info snd_vt1724_revo_cards[] __devinitdata = {
	{
		.subvendor = VT1724_SUBDEVICE_REVOLUTION71,
		.name = "M Audio Revolution-7.1",
		.model = "revo71",
		.chip_init = revo_init,
		.build_controls = revo_add_controls,
	},
	{ } /* terminator */
};
