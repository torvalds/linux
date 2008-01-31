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

#include <asm/io.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <sound/core.h>

#include "ice1712.h"
#include "envy24ht.h"
#include "revo.h"

/* a non-standard I2C device for revo51 */
struct revo51_spec {
	struct snd_i2c_device *dev;
	struct snd_pt2258 *pt2258;
} revo51;

static void revo_i2s_mclk_changed(struct snd_ice1712 *ice)
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
static void revo_set_rate_val(struct snd_akm4xxx *ak, unsigned int rate)
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

	if (ak->type == SND_AK4355 || ak->type == SND_AK4358) {
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
 * I2C access to the PT2258 volume controller on GPIO 6/7 (Revolution 5.1)
 */

static void revo_i2c_start(struct snd_i2c_bus *bus)
{
	struct snd_ice1712 *ice = bus->private_data;
	snd_ice1712_save_gpio_status(ice);
}

static void revo_i2c_stop(struct snd_i2c_bus *bus)
{
	struct snd_ice1712 *ice = bus->private_data;
	snd_ice1712_restore_gpio_status(ice);
}

static void revo_i2c_direction(struct snd_i2c_bus *bus, int clock, int data)
{
	struct snd_ice1712 *ice = bus->private_data;
	unsigned int mask, val;

	val = 0;
	if (clock)
		val |= VT1724_REVO_I2C_CLOCK;	/* write SCL */
	if (data)
		val |= VT1724_REVO_I2C_DATA;	/* write SDA */
	mask = VT1724_REVO_I2C_CLOCK | VT1724_REVO_I2C_DATA;
	ice->gpio.direction &= ~mask;
	ice->gpio.direction |= val;
	snd_ice1712_gpio_set_dir(ice, ice->gpio.direction);
	snd_ice1712_gpio_set_mask(ice, ~mask);
}

static void revo_i2c_setlines(struct snd_i2c_bus *bus, int clk, int data)
{
	struct snd_ice1712 *ice = bus->private_data;
	unsigned int val = 0;

	if (clk)
		val |= VT1724_REVO_I2C_CLOCK;
	if (data)
		val |= VT1724_REVO_I2C_DATA;
	snd_ice1712_gpio_write_bits(ice,
				    VT1724_REVO_I2C_DATA |
				    VT1724_REVO_I2C_CLOCK, val);
	udelay(5);
}

static int revo_i2c_getdata(struct snd_i2c_bus *bus, int ack)
{
	struct snd_ice1712 *ice = bus->private_data;
	int bit;

	if (ack)
		udelay(5);
	bit = snd_ice1712_gpio_read_bits(ice, VT1724_REVO_I2C_DATA) ? 1 : 0;
	return bit;
}

static struct snd_i2c_bit_ops revo51_bit_ops = {
	.start = revo_i2c_start,
	.stop = revo_i2c_stop,
	.direction = revo_i2c_direction,
	.setlines = revo_i2c_setlines,
	.getdata = revo_i2c_getdata,
};

static int revo51_i2c_init(struct snd_ice1712 *ice,
			   struct snd_pt2258 *pt)
{
	struct revo51_spec *spec;
	int err;

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec)
		return -ENOMEM;
	ice->spec = spec;

	/* create the I2C bus */
	err = snd_i2c_bus_create(ice->card, "ICE1724 GPIO6", NULL, &ice->i2c);
	if (err < 0)
		return err;

	ice->i2c->private_data = ice;
	ice->i2c->hw_ops.bit = &revo51_bit_ops;

	/* create the I2C device */
	err = snd_i2c_device_create(ice->i2c, "PT2258", 0x40, &spec->dev);
	if (err < 0)
		return err;

	pt->card = ice->card;
	pt->i2c_bus = ice->i2c;
	pt->i2c_dev = spec->dev;
	spec->pt2258 = pt;

	snd_pt2258_reset(pt);

	return 0;
}

/*
 * initialize the chips on M-Audio Revolution cards
 */

#define AK_DAC(xname,xch) { .name = xname, .num_channels = xch }

static const struct snd_akm4xxx_dac_channel revo71_front[] = {
	{
		.name = "PCM Playback Volume",
		.num_channels = 2,
		/* front channels DAC supports muting */
		.switch_name = "PCM Playback Switch",
	},
};

static const struct snd_akm4xxx_dac_channel revo71_surround[] = {
	AK_DAC("PCM Center Playback Volume", 1),
	AK_DAC("PCM LFE Playback Volume", 1),
	AK_DAC("PCM Side Playback Volume", 2),
	AK_DAC("PCM Rear Playback Volume", 2),
};

static const struct snd_akm4xxx_dac_channel revo51_dac[] = {
	AK_DAC("PCM Playback Volume", 2),
	AK_DAC("PCM Center Playback Volume", 1),
	AK_DAC("PCM LFE Playback Volume", 1),
	AK_DAC("PCM Rear Playback Volume", 2),
};

static const char *revo51_adc_input_names[] = {
	"Mic",
	"Line",
	"CD",
	NULL
};

static const struct snd_akm4xxx_adc_channel revo51_adc[] = {
	{
		.name = "PCM Capture Volume",
		.switch_name = "PCM Capture Switch",
		.num_channels = 2,
		.input_names = revo51_adc_input_names
	},
};

static struct snd_akm4xxx akm_revo_front __devinitdata = {
	.type = SND_AK4381,
	.num_dacs = 2,
	.ops = {
		.set_rate_val = revo_set_rate_val
	},
	.dac_info = revo71_front,
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

static struct snd_akm4xxx akm_revo_surround __devinitdata = {
	.type = SND_AK4355,
	.idx_offset = 1,
	.num_dacs = 6,
	.ops = {
		.set_rate_val = revo_set_rate_val
	},
	.dac_info = revo71_surround,
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

static struct snd_akm4xxx akm_revo51 __devinitdata = {
	.type = SND_AK4358,
	.num_dacs = 6,
	.ops = {
		.set_rate_val = revo_set_rate_val
	},
	.dac_info = revo51_dac,
};

static struct snd_ak4xxx_private akm_revo51_priv __devinitdata = {
	.caddr = 2,
	.cif = 0,
	.data_mask = VT1724_REVO_CDOUT,
	.clk_mask = VT1724_REVO_CCLK,
	.cs_mask = VT1724_REVO_CS0 | VT1724_REVO_CS1,
	.cs_addr = VT1724_REVO_CS1,
	.cs_none = VT1724_REVO_CS0 | VT1724_REVO_CS1,
	.add_flags = VT1724_REVO_CCLK, /* high at init */
	.mask_flags = 0,
};

static struct snd_akm4xxx akm_revo51_adc __devinitdata = {
	.type = SND_AK5365,
	.num_adcs = 2,
	.adc_info = revo51_adc,
};

static struct snd_ak4xxx_private akm_revo51_adc_priv __devinitdata = {
	.caddr = 2,
	.cif = 0,
	.data_mask = VT1724_REVO_CDOUT,
	.clk_mask = VT1724_REVO_CCLK,
	.cs_mask = VT1724_REVO_CS0 | VT1724_REVO_CS1,
	.cs_addr = VT1724_REVO_CS0,
	.cs_none = VT1724_REVO_CS0 | VT1724_REVO_CS1,
	.add_flags = VT1724_REVO_CCLK, /* high at init */
	.mask_flags = 0,
};

static struct snd_pt2258 ptc_revo51_volume;

/* AK4358 for AP192 DAC, AK5385A for ADC */
static void ap192_set_rate_val(struct snd_akm4xxx *ak, unsigned int rate)
{
	struct snd_ice1712 *ice = ak->private_data[0];

	revo_set_rate_val(ak, rate);

#if 1 /* FIXME: do we need this procedure? */
	/* reset DFS pin of AK5385A for ADC, too */
	/* DFS0 (pin 18) -- GPIO10 pin 77 */
	snd_ice1712_save_gpio_status(ice);
	snd_ice1712_gpio_write_bits(ice, 1 << 10,
				    rate > 48000 ? (1 << 10) : 0);
	snd_ice1712_restore_gpio_status(ice);
#endif
}

static const struct snd_akm4xxx_dac_channel ap192_dac[] = {
	AK_DAC("PCM Playback Volume", 2)
};

static struct snd_akm4xxx akm_ap192 __devinitdata = {
	.type = SND_AK4358,
	.num_dacs = 2,
	.ops = {
		.set_rate_val = ap192_set_rate_val
	},
	.dac_info = ap192_dac,
};

static struct snd_ak4xxx_private akm_ap192_priv __devinitdata = {
	.caddr = 2,
	.cif = 0,
	.data_mask = VT1724_REVO_CDOUT,
	.clk_mask = VT1724_REVO_CCLK,
	.cs_mask = VT1724_REVO_CS0 | VT1724_REVO_CS3,
	.cs_addr = VT1724_REVO_CS3,
	.cs_none = VT1724_REVO_CS0 | VT1724_REVO_CS3,
	.add_flags = VT1724_REVO_CCLK, /* high at init */
	.mask_flags = 0,
};

#if 0
/* FIXME: ak4114 makes the sound much lower due to some confliction,
 *        so let's disable it right now...
 */
#define BUILD_AK4114_AP192
#endif

#ifdef BUILD_AK4114_AP192
/* AK4114 support on Audiophile 192 */
/* CDTO (pin 32) -- GPIO2 pin 52
 * CDTI (pin 33) -- GPIO3 pin 53 (shared with AK4358)
 * CCLK (pin 34) -- GPIO1 pin 51 (shared with AK4358)
 * CSN  (pin 35) -- GPIO7 pin 59
 */
#define AK4114_ADDR	0x00

static void write_data(struct snd_ice1712 *ice, unsigned int gpio,
		       unsigned int data, int idx)
{
	for (; idx >= 0; idx--) {
		/* drop clock */
		gpio &= ~VT1724_REVO_CCLK;
		snd_ice1712_gpio_write(ice, gpio);
		udelay(1);
		/* set data */
		if (data & (1 << idx))
			gpio |= VT1724_REVO_CDOUT;
		else
			gpio &= ~VT1724_REVO_CDOUT;
		snd_ice1712_gpio_write(ice, gpio);
		udelay(1);
		/* raise clock */
		gpio |= VT1724_REVO_CCLK;
		snd_ice1712_gpio_write(ice, gpio);
		udelay(1);
	}
}

static unsigned char read_data(struct snd_ice1712 *ice, unsigned int gpio,
			       int idx)
{
	unsigned char data = 0;

	for (; idx >= 0; idx--) {
		/* drop clock */
		gpio &= ~VT1724_REVO_CCLK;
		snd_ice1712_gpio_write(ice, gpio);
		udelay(1);
		/* read data */
		if (snd_ice1712_gpio_read(ice) & VT1724_REVO_CDIN)
			data |= (1 << idx);
		udelay(1);
		/* raise clock */
		gpio |= VT1724_REVO_CCLK;
		snd_ice1712_gpio_write(ice, gpio);
		udelay(1);
	}
	return data;
}

static unsigned int ap192_4wire_start(struct snd_ice1712 *ice)
{
	unsigned int tmp;

	snd_ice1712_save_gpio_status(ice);
	tmp = snd_ice1712_gpio_read(ice);
	tmp |= VT1724_REVO_CCLK; /* high at init */
	tmp |= VT1724_REVO_CS0;
	tmp &= ~VT1724_REVO_CS3;
	snd_ice1712_gpio_write(ice, tmp);
	udelay(1);
	return tmp;
}

static void ap192_4wire_finish(struct snd_ice1712 *ice, unsigned int tmp)
{
	tmp |= VT1724_REVO_CS3;
	tmp |= VT1724_REVO_CS0;
	snd_ice1712_gpio_write(ice, tmp);
	udelay(1);
	snd_ice1712_restore_gpio_status(ice);
}

static void ap192_ak4114_write(void *private_data, unsigned char addr,
			       unsigned char data)
{
	struct snd_ice1712 *ice = private_data;
	unsigned int tmp, addrdata;

	tmp = ap192_4wire_start(ice);
	addrdata = (AK4114_ADDR << 6) | 0x20 | (addr & 0x1f);
	addrdata = (addrdata << 8) | data;
	write_data(ice, tmp, addrdata, 15);
	ap192_4wire_finish(ice, tmp);
}

static unsigned char ap192_ak4114_read(void *private_data, unsigned char addr)
{
	struct snd_ice1712 *ice = private_data;
	unsigned int tmp;
	unsigned char data;

	tmp = ap192_4wire_start(ice);
	write_data(ice, tmp, (AK4114_ADDR << 6) | (addr & 0x1f), 7);
	data = read_data(ice, tmp, 7);
	ap192_4wire_finish(ice, tmp);
	return data;
}

static int __devinit ap192_ak4114_init(struct snd_ice1712 *ice)
{
	static const unsigned char ak4114_init_vals[] = {
		AK4114_RST | AK4114_PWN | AK4114_OCKS0 | AK4114_OCKS1,
		AK4114_DIF_I24I2S,
		AK4114_TX1E,
		AK4114_EFH_1024 | AK4114_DIT | AK4114_IPS(1),
		0,
		0
	};
	static const unsigned char ak4114_init_txcsb[] = {
		0x41, 0x02, 0x2c, 0x00, 0x00
	};
	struct ak4114 *ak;
	int err;

	return snd_ak4114_create(ice->card,
				 ap192_ak4114_read,
				 ap192_ak4114_write,
				 ak4114_init_vals, ak4114_init_txcsb,
				 ice, &ak);
}
#endif /* BUILD_AK4114_AP192 */

static int __devinit revo_init(struct snd_ice1712 *ice)
{
	struct snd_akm4xxx *ak;
	int err;

	/* determine I2C, DACs and ADCs */
	switch (ice->eeprom.subvendor) {
	case VT1724_SUBDEVICE_REVOLUTION71:
		ice->num_total_dacs = 8;
		ice->num_total_adcs = 2;
		ice->gpio.i2s_mclk_changed = revo_i2s_mclk_changed;
		break;
	case VT1724_SUBDEVICE_REVOLUTION51:
		ice->num_total_dacs = 6;
		ice->num_total_adcs = 2;
		break;
	case VT1724_SUBDEVICE_AUDIOPHILE192:
		ice->num_total_dacs = 2;
		ice->num_total_adcs = 2;
		break;
	default:
		snd_BUG();
		return -EINVAL;
	}

	/* second stage of initialization, analog parts and others */
	ak = ice->akm = kcalloc(2, sizeof(struct snd_akm4xxx), GFP_KERNEL);
	if (! ak)
		return -ENOMEM;
	ice->akm_codecs = 2;
	switch (ice->eeprom.subvendor) {
	case VT1724_SUBDEVICE_REVOLUTION71:
		ice->akm_codecs = 2;
		if ((err = snd_ice1712_akm4xxx_init(ak, &akm_revo_front, &akm_revo_front_priv, ice)) < 0)
			return err;
		if ((err = snd_ice1712_akm4xxx_init(ak + 1, &akm_revo_surround, &akm_revo_surround_priv, ice)) < 0)
			return err;
		/* unmute all codecs */
		snd_ice1712_gpio_write_bits(ice, VT1724_REVO_MUTE, VT1724_REVO_MUTE);
		break;
	case VT1724_SUBDEVICE_REVOLUTION51:
		ice->akm_codecs = 2;
		err = snd_ice1712_akm4xxx_init(ak, &akm_revo51,
					       &akm_revo51_priv, ice);
		if (err < 0)
			return err;
		err = snd_ice1712_akm4xxx_init(ak+1, &akm_revo51_adc,
					       &akm_revo51_adc_priv, ice);
		if (err < 0)
			return err;
		err = revo51_i2c_init(ice, &ptc_revo51_volume);
		if (err < 0)
			return err;
		/* unmute all codecs */
		snd_ice1712_gpio_write_bits(ice, VT1724_REVO_MUTE,
					    VT1724_REVO_MUTE);
		break;
	case VT1724_SUBDEVICE_AUDIOPHILE192:
		ice->akm_codecs = 1;
		err = snd_ice1712_akm4xxx_init(ak, &akm_ap192, &akm_ap192_priv,
					       ice);
		if (err < 0)
			return err;
		
		break;
	}

	return 0;
}


static int __devinit revo_add_controls(struct snd_ice1712 *ice)
{
	struct revo51_spec *spec;
	int err;

	switch (ice->eeprom.subvendor) {
	case VT1724_SUBDEVICE_REVOLUTION71:
		err = snd_ice1712_akm4xxx_build_controls(ice);
		if (err < 0)
			return err;
		break;
	case VT1724_SUBDEVICE_REVOLUTION51:
		err = snd_ice1712_akm4xxx_build_controls(ice);
		if (err < 0)
			return err;
		spec = ice->spec;
		err = snd_pt2258_build_controls(spec->pt2258);
		if (err < 0)
			return err;
		break;
	case VT1724_SUBDEVICE_AUDIOPHILE192:
		err = snd_ice1712_akm4xxx_build_controls(ice);
		if (err < 0)
			return err;
#ifdef BUILD_AK4114_AP192
		err = ap192_ak4114_init(ice);
		if (err < 0)
			return err;
#endif
		break;
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
	{
		.subvendor = VT1724_SUBDEVICE_REVOLUTION51,
		.name = "M Audio Revolution-5.1",
		.model = "revo51",
		.chip_init = revo_init,
		.build_controls = revo_add_controls,
	},
	{
		.subvendor = VT1724_SUBDEVICE_AUDIOPHILE192,
		.name = "M Audio Audiophile192",
		.model = "ap192",
		.chip_init = revo_init,
		.build_controls = revo_add_controls,
	},
	{ } /* terminator */
};
