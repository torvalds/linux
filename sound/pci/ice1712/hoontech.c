/*
 *   ALSA driver for ICEnsemble ICE1712 (Envy24)
 *
 *   Lowlevel functions for Hoontech STDSP24
 *
 *	Copyright (c) 2000 Jaroslav Kysela <perex@perex.cz>
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
#include <linux/mutex.h>

#include <sound/core.h>

#include "ice1712.h"
#include "hoontech.h"

/* Hoontech-specific setting */
struct hoontech_spec {
	unsigned char boxbits[4];
	unsigned int config;
	unsigned short boxconfig[4];
};

static void __devinit snd_ice1712_stdsp24_gpio_write(struct snd_ice1712 *ice, unsigned char byte)
{
	byte |= ICE1712_STDSP24_CLOCK_BIT;
	udelay(100);
	snd_ice1712_write(ice, ICE1712_IREG_GPIO_DATA, byte);
	byte &= ~ICE1712_STDSP24_CLOCK_BIT;
	udelay(100);
	snd_ice1712_write(ice, ICE1712_IREG_GPIO_DATA, byte);
	byte |= ICE1712_STDSP24_CLOCK_BIT;
	udelay(100);
	snd_ice1712_write(ice, ICE1712_IREG_GPIO_DATA, byte);
}

static void __devinit snd_ice1712_stdsp24_darear(struct snd_ice1712 *ice, int activate)
{
	struct hoontech_spec *spec = ice->spec;
	mutex_lock(&ice->gpio_mutex);
	ICE1712_STDSP24_0_DAREAR(spec->boxbits, activate);
	snd_ice1712_stdsp24_gpio_write(ice, spec->boxbits[0]);
	mutex_unlock(&ice->gpio_mutex);
}

static void __devinit snd_ice1712_stdsp24_mute(struct snd_ice1712 *ice, int activate)
{
	struct hoontech_spec *spec = ice->spec;
	mutex_lock(&ice->gpio_mutex);
	ICE1712_STDSP24_3_MUTE(spec->boxbits, activate);
	snd_ice1712_stdsp24_gpio_write(ice, spec->boxbits[3]);
	mutex_unlock(&ice->gpio_mutex);
}

static void __devinit snd_ice1712_stdsp24_insel(struct snd_ice1712 *ice, int activate)
{
	struct hoontech_spec *spec = ice->spec;
	mutex_lock(&ice->gpio_mutex);
	ICE1712_STDSP24_3_INSEL(spec->boxbits, activate);
	snd_ice1712_stdsp24_gpio_write(ice, spec->boxbits[3]);
	mutex_unlock(&ice->gpio_mutex);
}

static void __devinit snd_ice1712_stdsp24_box_channel(struct snd_ice1712 *ice, int box, int chn, int activate)
{
	struct hoontech_spec *spec = ice->spec;

	mutex_lock(&ice->gpio_mutex);

	/* select box */
	ICE1712_STDSP24_0_BOX(spec->boxbits, box);
	snd_ice1712_stdsp24_gpio_write(ice, spec->boxbits[0]);

	/* prepare for write */
	if (chn == 3)
		ICE1712_STDSP24_2_CHN4(spec->boxbits, 0);
	ICE1712_STDSP24_2_MIDI1(spec->boxbits, activate);
	snd_ice1712_stdsp24_gpio_write(ice, spec->boxbits[2]);
	snd_ice1712_stdsp24_gpio_write(ice, spec->boxbits[3]);

	ICE1712_STDSP24_1_CHN1(spec->boxbits, 1);
	ICE1712_STDSP24_1_CHN2(spec->boxbits, 1);
	ICE1712_STDSP24_1_CHN3(spec->boxbits, 1);
	ICE1712_STDSP24_2_CHN4(spec->boxbits, 1);
	snd_ice1712_stdsp24_gpio_write(ice, spec->boxbits[1]);
	snd_ice1712_stdsp24_gpio_write(ice, spec->boxbits[2]);
	udelay(100);
	if (chn == 3) {
		ICE1712_STDSP24_2_CHN4(spec->boxbits, 0);
		snd_ice1712_stdsp24_gpio_write(ice, spec->boxbits[2]);
	} else {
		switch (chn) {
		case 0:	ICE1712_STDSP24_1_CHN1(spec->boxbits, 0); break;
		case 1:	ICE1712_STDSP24_1_CHN2(spec->boxbits, 0); break;
		case 2:	ICE1712_STDSP24_1_CHN3(spec->boxbits, 0); break;
		}
		snd_ice1712_stdsp24_gpio_write(ice, spec->boxbits[1]);
	}
	udelay(100);
	ICE1712_STDSP24_1_CHN1(spec->boxbits, 1);
	ICE1712_STDSP24_1_CHN2(spec->boxbits, 1);
	ICE1712_STDSP24_1_CHN3(spec->boxbits, 1);
	ICE1712_STDSP24_2_CHN4(spec->boxbits, 1);
	snd_ice1712_stdsp24_gpio_write(ice, spec->boxbits[1]);
	snd_ice1712_stdsp24_gpio_write(ice, spec->boxbits[2]);
	udelay(100);

	ICE1712_STDSP24_2_MIDI1(spec->boxbits, 0);
	snd_ice1712_stdsp24_gpio_write(ice, spec->boxbits[2]);

	mutex_unlock(&ice->gpio_mutex);
}

static void __devinit snd_ice1712_stdsp24_box_midi(struct snd_ice1712 *ice, int box, int master)
{
	struct hoontech_spec *spec = ice->spec;

	mutex_lock(&ice->gpio_mutex);

	/* select box */
	ICE1712_STDSP24_0_BOX(spec->boxbits, box);
	snd_ice1712_stdsp24_gpio_write(ice, spec->boxbits[0]);

	ICE1712_STDSP24_2_MIDIIN(spec->boxbits, 1);
	ICE1712_STDSP24_2_MIDI1(spec->boxbits, master);
	snd_ice1712_stdsp24_gpio_write(ice, spec->boxbits[2]);
	snd_ice1712_stdsp24_gpio_write(ice, spec->boxbits[3]);

	udelay(100);
	
	ICE1712_STDSP24_2_MIDIIN(spec->boxbits, 0);
	snd_ice1712_stdsp24_gpio_write(ice, spec->boxbits[2]);
	
	mdelay(10);
	
	ICE1712_STDSP24_2_MIDIIN(spec->boxbits, 1);
	snd_ice1712_stdsp24_gpio_write(ice, spec->boxbits[2]);

	mutex_unlock(&ice->gpio_mutex);
}

static void __devinit snd_ice1712_stdsp24_midi2(struct snd_ice1712 *ice, int activate)
{
	struct hoontech_spec *spec = ice->spec;
	mutex_lock(&ice->gpio_mutex);
	ICE1712_STDSP24_3_MIDI2(spec->boxbits, activate);
	snd_ice1712_stdsp24_gpio_write(ice, spec->boxbits[3]);
	mutex_unlock(&ice->gpio_mutex);
}

static int __devinit snd_ice1712_hoontech_init(struct snd_ice1712 *ice)
{
	struct hoontech_spec *spec;
	int box, chn;

	ice->num_total_dacs = 8;
	ice->num_total_adcs = 8;

	spec = kzalloc(sizeof(*spec), GFP_KERNEL);
	if (!spec)
		return -ENOMEM;
	ice->spec = spec;

	ICE1712_STDSP24_SET_ADDR(spec->boxbits, 0);
	ICE1712_STDSP24_CLOCK(spec->boxbits, 0, 1);
	ICE1712_STDSP24_0_BOX(spec->boxbits, 0);
	ICE1712_STDSP24_0_DAREAR(spec->boxbits, 0);

	ICE1712_STDSP24_SET_ADDR(spec->boxbits, 1);
	ICE1712_STDSP24_CLOCK(spec->boxbits, 1, 1);
	ICE1712_STDSP24_1_CHN1(spec->boxbits, 1);
	ICE1712_STDSP24_1_CHN2(spec->boxbits, 1);
	ICE1712_STDSP24_1_CHN3(spec->boxbits, 1);
	
	ICE1712_STDSP24_SET_ADDR(spec->boxbits, 2);
	ICE1712_STDSP24_CLOCK(spec->boxbits, 2, 1);
	ICE1712_STDSP24_2_CHN4(spec->boxbits, 1);
	ICE1712_STDSP24_2_MIDIIN(spec->boxbits, 1);
	ICE1712_STDSP24_2_MIDI1(spec->boxbits, 0);

	ICE1712_STDSP24_SET_ADDR(spec->boxbits, 3);
	ICE1712_STDSP24_CLOCK(spec->boxbits, 3, 1);
	ICE1712_STDSP24_3_MIDI2(spec->boxbits, 0);
	ICE1712_STDSP24_3_MUTE(spec->boxbits, 1);
	ICE1712_STDSP24_3_INSEL(spec->boxbits, 0);

	/* let's go - activate only functions in first box */
	spec->config = 0;
			    /* ICE1712_STDSP24_MUTE |
			       ICE1712_STDSP24_INSEL |
			       ICE1712_STDSP24_DAREAR; */
	/*  These boxconfigs have caused problems in the past.
	 *  The code is not optimal, but should now enable a working config to
	 *  be achieved.
	 *  ** MIDI IN can only be configured on one box **
	 *  ICE1712_STDSP24_BOX_MIDI1 needs to be set for that box.
	 *  Tests on a ADAC2000 box suggest the box config flags do not
	 *  work as would be expected, and the inputs are crossed.
	 *  Setting ICE1712_STDSP24_BOX_MIDI1 and ICE1712_STDSP24_BOX_MIDI2
	 *  on the same box connects MIDI-In to both 401 uarts; both outputs
	 *  are then active on all boxes.
	 *  The default config here sets up everything on the first box.
	 *  Alan Horstmann  5.2.2008
	 */
	spec->boxconfig[0] = ICE1712_STDSP24_BOX_CHN1 |
				     ICE1712_STDSP24_BOX_CHN2 |
				     ICE1712_STDSP24_BOX_CHN3 |
				     ICE1712_STDSP24_BOX_CHN4 |
				     ICE1712_STDSP24_BOX_MIDI1 |
				     ICE1712_STDSP24_BOX_MIDI2;
	spec->boxconfig[1] = 
	spec->boxconfig[2] = 
	spec->boxconfig[3] = 0;
	snd_ice1712_stdsp24_darear(ice,
		(spec->config & ICE1712_STDSP24_DAREAR) ? 1 : 0);
	snd_ice1712_stdsp24_mute(ice,
		(spec->config & ICE1712_STDSP24_MUTE) ? 1 : 0);
	snd_ice1712_stdsp24_insel(ice,
		(spec->config & ICE1712_STDSP24_INSEL) ? 1 : 0);
	for (box = 0; box < 4; box++) {
		if (spec->boxconfig[box] & ICE1712_STDSP24_BOX_MIDI2)
                        snd_ice1712_stdsp24_midi2(ice, 1);
		for (chn = 0; chn < 4; chn++)
			snd_ice1712_stdsp24_box_channel(ice, box, chn,
				(spec->boxconfig[box] & (1 << chn)) ? 1 : 0);
		if (spec->boxconfig[box] & ICE1712_STDSP24_BOX_MIDI1)
			snd_ice1712_stdsp24_box_midi(ice, box, 1);
	}

	return 0;
}

/*
 * AK4524 access
 */

/* start callback for STDSP24 with modified hardware */
static void stdsp24_ak4524_lock(struct snd_akm4xxx *ak, int chip)
{
	struct snd_ice1712 *ice = ak->private_data[0];
	unsigned char tmp;
	snd_ice1712_save_gpio_status(ice);
	tmp =	ICE1712_STDSP24_SERIAL_DATA |
		ICE1712_STDSP24_SERIAL_CLOCK |
		ICE1712_STDSP24_AK4524_CS;
	snd_ice1712_write(ice, ICE1712_IREG_GPIO_DIRECTION,
			  ice->gpio.direction | tmp);
	snd_ice1712_write(ice, ICE1712_IREG_GPIO_WRITE_MASK, ~tmp);
}

static int __devinit snd_ice1712_value_init(struct snd_ice1712 *ice)
{
	/* Hoontech STDSP24 with modified hardware */
	static struct snd_akm4xxx akm_stdsp24_mv __devinitdata = {
		.num_adcs = 2,
		.num_dacs = 2,
		.type = SND_AK4524,
		.ops = {
			.lock = stdsp24_ak4524_lock
		}
	};

	static struct snd_ak4xxx_private akm_stdsp24_mv_priv __devinitdata = {
		.caddr = 2,
		.cif = 1, /* CIF high */
		.data_mask = ICE1712_STDSP24_SERIAL_DATA,
		.clk_mask = ICE1712_STDSP24_SERIAL_CLOCK,
		.cs_mask = ICE1712_STDSP24_AK4524_CS,
		.cs_addr = ICE1712_STDSP24_AK4524_CS,
		.cs_none = 0,
		.add_flags = 0,
	};

	int err;
	struct snd_akm4xxx *ak;

	/* set the analog DACs */
	ice->num_total_dacs = 2;

	/* set the analog ADCs */
	ice->num_total_adcs = 2;
	
	/* analog section */
	ak = ice->akm = kmalloc(sizeof(struct snd_akm4xxx), GFP_KERNEL);
	if (! ak)
		return -ENOMEM;
	ice->akm_codecs = 1;

	err = snd_ice1712_akm4xxx_init(ak, &akm_stdsp24_mv, &akm_stdsp24_mv_priv, ice);
	if (err < 0)
		return err;

	/* ak4524 controls */
	err = snd_ice1712_akm4xxx_build_controls(ice);
	if (err < 0)
		return err;

	return 0;
}

static int __devinit snd_ice1712_ez8_init(struct snd_ice1712 *ice)
{
	ice->gpio.write_mask = ice->eeprom.gpiomask;
	ice->gpio.direction = ice->eeprom.gpiodir;
	snd_ice1712_write(ice, ICE1712_IREG_GPIO_WRITE_MASK, ice->eeprom.gpiomask);
	snd_ice1712_write(ice, ICE1712_IREG_GPIO_DIRECTION, ice->eeprom.gpiodir);
	snd_ice1712_write(ice, ICE1712_IREG_GPIO_DATA, ice->eeprom.gpiostate);
	return 0;
}


/* entry point */
struct snd_ice1712_card_info snd_ice1712_hoontech_cards[] __devinitdata = {
	{
		.subvendor = ICE1712_SUBDEVICE_STDSP24,
		.name = "Hoontech SoundTrack Audio DSP24",
		.model = "dsp24",
		.chip_init = snd_ice1712_hoontech_init,
		.mpu401_1_name = "MIDI-1 Hoontech/STA DSP24",
		.mpu401_2_name = "MIDI-2 Hoontech/STA DSP24",
	},
	{
		.subvendor = ICE1712_SUBDEVICE_STDSP24_VALUE,	/* a dummy id */
		.name = "Hoontech SoundTrack Audio DSP24 Value",
		.model = "dsp24_value",
		.chip_init = snd_ice1712_value_init,
	},
	{
		.subvendor = ICE1712_SUBDEVICE_STDSP24_MEDIA7_1,
		.name = "Hoontech STA DSP24 Media 7.1",
		.model = "dsp24_71",
		.chip_init = snd_ice1712_hoontech_init,
	},
	{
		.subvendor = ICE1712_SUBDEVICE_EVENT_EZ8,	/* a dummy id */
		.name = "Event Electronics EZ8",
		.model = "ez8",
		.chip_init = snd_ice1712_ez8_init,
	},
	{ } /* terminator */
};
