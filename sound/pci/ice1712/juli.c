/*
 *   ALSA driver for ICEnsemble VT1724 (Envy24HT)
 *
 *   Lowlevel functions for ESI Juli@ cards
 *
 *	Copyright (c) 2004 Jaroslav Kysela <perex@suse.cz>
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
#include "juli.h"

/*
 * chip addresses on I2C bus
 */
#define AK4114_ADDR		0x20		/* S/PDIF receiver */
#define AK4358_ADDR		0x22		/* DAC */

/*
 * GPIO pins
 */
#define GPIO_FREQ_MASK		(3<<0)
#define GPIO_FREQ_32KHZ		(0<<0)
#define GPIO_FREQ_44KHZ		(1<<0)
#define GPIO_FREQ_48KHZ		(2<<0)
#define GPIO_MULTI_MASK		(3<<2)
#define GPIO_MULTI_4X		(0<<2)
#define GPIO_MULTI_2X		(1<<2)
#define GPIO_MULTI_1X		(2<<2)		/* also external */
#define GPIO_MULTI_HALF		(3<<2)
#define GPIO_INTERNAL_CLOCK	(1<<4)
#define GPIO_ANALOG_PRESENT	(1<<5)		/* RO only: 0 = present */
#define GPIO_RXMCLK_SEL		(1<<7)		/* must be 0 */
#define GPIO_AK5385A_CKS0	(1<<8)
#define GPIO_AK5385A_DFS0	(1<<9)		/* swapped with DFS1 according doc? */
#define GPIO_AK5385A_DFS1	(1<<10)
#define GPIO_DIGOUT_MONITOR	(1<<11)		/* 1 = active */
#define GPIO_DIGIN_MONITOR	(1<<12)		/* 1 = active */
#define GPIO_ANAIN_MONITOR	(1<<13)		/* 1 = active */
#define GPIO_AK5385A_MCLK	(1<<14)		/* must be 0 */
#define GPIO_MUTE_CONTROL	(1<<15)		/* 0 = off, 1 = on */

static void juli_ak4114_write(void *private_data, unsigned char reg, unsigned char val)
{
	snd_vt1724_write_i2c((struct snd_ice1712 *)private_data, AK4114_ADDR, reg, val);
}
        
static unsigned char juli_ak4114_read(void *private_data, unsigned char reg)
{
	return snd_vt1724_read_i2c((struct snd_ice1712 *)private_data, AK4114_ADDR, reg);
}

/*
 * AK4358 section
 */

static void juli_akm_lock(struct snd_akm4xxx *ak, int chip)
{
}

static void juli_akm_unlock(struct snd_akm4xxx *ak, int chip)
{
}

static void juli_akm_write(struct snd_akm4xxx *ak, int chip,
			   unsigned char addr, unsigned char data)
{
	struct snd_ice1712 *ice = ak->private_data[0];
	 
	snd_assert(chip == 0, return);
	snd_vt1724_write_i2c(ice, AK4358_ADDR, addr, data);
}

/*
 * change the rate of envy24HT, AK4358
 */
static void juli_akm_set_rate_val(struct snd_akm4xxx *ak, unsigned int rate)
{
	unsigned char old, tmp, dfs;

	if (rate == 0)  /* no hint - S/PDIF input is master, simply return */
		return;
	
	/* adjust DFS on codecs */
	if (rate > 96000) 
		dfs = 2;
	else if (rate > 48000)
		dfs = 1;
	else
		dfs = 0;
	
	tmp = snd_akm4xxx_get(ak, 0, 2);
	old = (tmp >> 4) & 0x03;
	if (old == dfs)
		return;
	/* reset DFS */
	snd_akm4xxx_reset(ak, 1);
	tmp = snd_akm4xxx_get(ak, 0, 2);
	tmp &= ~(0x03 << 4);
	tmp |= dfs << 4;
	snd_akm4xxx_set(ak, 0, 2, tmp);
	snd_akm4xxx_reset(ak, 0);
}

static struct snd_akm4xxx akm_juli_dac __devinitdata = {
	.type = SND_AK4358,
	.num_dacs = 2,
	.ops = {
		.lock = juli_akm_lock,
		.unlock = juli_akm_unlock,
		.write = juli_akm_write,
		.set_rate_val = juli_akm_set_rate_val
	}
};

static int __devinit juli_add_controls(struct snd_ice1712 *ice)
{
	return snd_ice1712_akm4xxx_build_controls(ice);
}

/*
 * initialize the chip
 */
static int __devinit juli_init(struct snd_ice1712 *ice)
{
	static unsigned char ak4114_init_vals[] = {
		/* AK4117_REG_PWRDN */	AK4114_RST | AK4114_PWN | AK4114_OCKS0 | AK4114_OCKS1,
		/* AK4114_REQ_FORMAT */	AK4114_DIF_I24I2S,
		/* AK4114_REG_IO0 */	AK4114_TX1E,
		/* AK4114_REG_IO1 */	AK4114_EFH_1024 | AK4114_DIT | AK4114_IPS(1),
		/* AK4114_REG_INT0_MASK */ 0,
		/* AK4114_REG_INT1_MASK */ 0
	};
	static unsigned char ak4114_init_txcsb[] = {
		0x41, 0x02, 0x2c, 0x00, 0x00
	};
	int err;
	struct snd_akm4xxx *ak;

#if 0
	for (err = 0; err < 0x20; err++)
		juli_ak4114_read(ice, err);
	juli_ak4114_write(ice, 0, 0x0f);
	juli_ak4114_read(ice, 0);
	juli_ak4114_read(ice, 1);
#endif
	err = snd_ak4114_create(ice->card,
				juli_ak4114_read,
				juli_ak4114_write,
				ak4114_init_vals, ak4114_init_txcsb,
				ice, &ice->spec.juli.ak4114);
	if (err < 0)
		return err;

#if 0
        /* it seems that the analog doughter board detection does not work
           reliably, so force the analog flag; it should be very rare
           to use Juli@ without the analog doughter board */
	ice->spec.juli.analog = (ice->gpio.get_data(ice) & GPIO_ANALOG_PRESENT) ? 0 : 1;
#else
        ice->spec.juli.analog = 1;
#endif

	if (ice->spec.juli.analog) {
		printk(KERN_INFO "juli@: analog I/O detected\n");
		ice->num_total_dacs = 2;
		ice->num_total_adcs = 2;

		ak = ice->akm = kzalloc(sizeof(struct snd_akm4xxx), GFP_KERNEL);
		if (! ak)
			return -ENOMEM;
		ice->akm_codecs = 1;
		if ((err = snd_ice1712_akm4xxx_init(ak, &akm_juli_dac, NULL, ice)) < 0)
			return err;
	}
	
	return 0;
}


/*
 * Juli@ boards don't provide the EEPROM data except for the vendor IDs.
 * hence the driver needs to sets up it properly.
 */

static unsigned char juli_eeprom[] __devinitdata = {
	0x20,	/* SYSCONF: clock 512, mpu401, 1xADC, 1xDACs */
	0x80,	/* ACLINK: I2S */
	0xf8,	/* I2S: vol, 96k, 24bit, 192k */
	0xc3,	/* SPDIF: out-en, out-int, spdif-in */
	0x9f,	/* GPIO_DIR */
	0xff,	/* GPIO_DIR1 */
	0x7f,	/* GPIO_DIR2 */
	0x9f,	/* GPIO_MASK */
	0xff,	/* GPIO_MASK1 */
	0x7f,	/* GPIO_MASK2 */
	0x16,	/* GPIO_STATE: internal clock, multiple 1x, 48kHz */
	0x80,	/* GPIO_STATE1: mute */
	0x00,	/* GPIO_STATE2 */
};

/* entry point */
struct snd_ice1712_card_info snd_vt1724_juli_cards[] __devinitdata = {
	{
		.subvendor = VT1724_SUBDEVICE_JULI,
		.name = "ESI Juli@",
		.model = "juli",
		.chip_init = juli_init,
		.build_controls = juli_add_controls,
		.eeprom_size = sizeof(juli_eeprom),
		.eeprom_data = juli_eeprom,
	},
	{ } /* terminator */
};
