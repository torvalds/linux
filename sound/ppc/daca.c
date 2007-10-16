/*
 * PMac DACA lowlevel functions
 *
 * Copyright (c) by Takashi Iwai <tiwai@suse.de>
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
 */


#include <sound/driver.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/kmod.h>
#include <linux/slab.h>
#include <sound/core.h>
#include "pmac.h"

/* i2c address */
#define DACA_I2C_ADDR	0x4d

/* registers */
#define DACA_REG_SR	0x01
#define DACA_REG_AVOL	0x02
#define DACA_REG_GCFG	0x03

/* maximum volume value */
#define DACA_VOL_MAX	0x38


struct pmac_daca {
	struct pmac_keywest i2c;
	int left_vol, right_vol;
	unsigned int deemphasis : 1;
	unsigned int amp_on : 1;
};


/*
 * initialize / detect DACA
 */
static int daca_init_client(struct pmac_keywest *i2c)
{
	unsigned short wdata = 0x00;
	/* SR: no swap, 1bit delay, 32-48kHz */
	/* GCFG: power amp inverted, DAC on */
	if (i2c_smbus_write_byte_data(i2c->client, DACA_REG_SR, 0x08) < 0 ||
	    i2c_smbus_write_byte_data(i2c->client, DACA_REG_GCFG, 0x05) < 0)
		return -EINVAL;
	return i2c_smbus_write_block_data(i2c->client, DACA_REG_AVOL,
					  2, (unsigned char*)&wdata);
}

/*
 * update volume
 */
static int daca_set_volume(struct pmac_daca *mix)
{
	unsigned char data[2];
  
	if (! mix->i2c.client)
		return -ENODEV;
  
	if (mix->left_vol > DACA_VOL_MAX)
		data[0] = DACA_VOL_MAX;
	else
		data[0] = mix->left_vol;
	if (mix->right_vol > DACA_VOL_MAX)
		data[1] = DACA_VOL_MAX;
	else
		data[1] = mix->right_vol;
	data[1] |= mix->deemphasis ? 0x40 : 0;
	if (i2c_smbus_write_block_data(mix->i2c.client, DACA_REG_AVOL,
				       2, data) < 0) {
		snd_printk("failed to set volume \n");
		return -EINVAL;
	}
	return 0;
}


/* deemphasis switch */
#define daca_info_deemphasis		snd_ctl_boolean_mono_info

static int daca_get_deemphasis(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_pmac *chip = snd_kcontrol_chip(kcontrol);
	struct pmac_daca *mix;
	if (! (mix = chip->mixer_data))
		return -ENODEV;
	ucontrol->value.integer.value[0] = mix->deemphasis ? 1 : 0;
	return 0;
}

static int daca_put_deemphasis(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_value *ucontrol)
{
	struct snd_pmac *chip = snd_kcontrol_chip(kcontrol);
	struct pmac_daca *mix;
	int change;

	if (! (mix = chip->mixer_data))
		return -ENODEV;
	change = mix->deemphasis != ucontrol->value.integer.value[0];
	if (change) {
		mix->deemphasis = ucontrol->value.integer.value[0];
		daca_set_volume(mix);
	}
	return change;
}

/* output volume */
static int daca_info_volume(struct snd_kcontrol *kcontrol,
			    struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = DACA_VOL_MAX;
	return 0;
}

static int daca_get_volume(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_pmac *chip = snd_kcontrol_chip(kcontrol);
	struct pmac_daca *mix;
	if (! (mix = chip->mixer_data))
		return -ENODEV;
	ucontrol->value.integer.value[0] = mix->left_vol;
	ucontrol->value.integer.value[1] = mix->right_vol;
	return 0;
}

static int daca_put_volume(struct snd_kcontrol *kcontrol,
			   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_pmac *chip = snd_kcontrol_chip(kcontrol);
	struct pmac_daca *mix;
	int change;

	if (! (mix = chip->mixer_data))
		return -ENODEV;
	change = mix->left_vol != ucontrol->value.integer.value[0] ||
		mix->right_vol != ucontrol->value.integer.value[1];
	if (change) {
		mix->left_vol = ucontrol->value.integer.value[0];
		mix->right_vol = ucontrol->value.integer.value[1];
		daca_set_volume(mix);
	}
	return change;
}

/* amplifier switch */
#define daca_info_amp	daca_info_deemphasis

static int daca_get_amp(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct snd_pmac *chip = snd_kcontrol_chip(kcontrol);
	struct pmac_daca *mix;
	if (! (mix = chip->mixer_data))
		return -ENODEV;
	ucontrol->value.integer.value[0] = mix->amp_on ? 1 : 0;
	return 0;
}

static int daca_put_amp(struct snd_kcontrol *kcontrol,
			struct snd_ctl_elem_value *ucontrol)
{
	struct snd_pmac *chip = snd_kcontrol_chip(kcontrol);
	struct pmac_daca *mix;
	int change;

	if (! (mix = chip->mixer_data))
		return -ENODEV;
	change = mix->amp_on != ucontrol->value.integer.value[0];
	if (change) {
		mix->amp_on = ucontrol->value.integer.value[0];
		i2c_smbus_write_byte_data(mix->i2c.client, DACA_REG_GCFG,
					  mix->amp_on ? 0x05 : 0x04);
	}
	return change;
}

static struct snd_kcontrol_new daca_mixers[] = {
	{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	  .name = "Deemphasis Switch",
	  .info = daca_info_deemphasis,
	  .get = daca_get_deemphasis,
	  .put = daca_put_deemphasis
	},
	{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	  .name = "Master Playback Volume",
	  .info = daca_info_volume,
	  .get = daca_get_volume,
	  .put = daca_put_volume
	},
	{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	  .name = "Power Amplifier Switch",
	  .info = daca_info_amp,
	  .get = daca_get_amp,
	  .put = daca_put_amp
	},
};


#ifdef CONFIG_PM
static void daca_resume(struct snd_pmac *chip)
{
	struct pmac_daca *mix = chip->mixer_data;
	i2c_smbus_write_byte_data(mix->i2c.client, DACA_REG_SR, 0x08);
	i2c_smbus_write_byte_data(mix->i2c.client, DACA_REG_GCFG,
				  mix->amp_on ? 0x05 : 0x04);
	daca_set_volume(mix);
}
#endif /* CONFIG_PM */


static void daca_cleanup(struct snd_pmac *chip)
{
	struct pmac_daca *mix = chip->mixer_data;
	if (! mix)
		return;
	snd_pmac_keywest_cleanup(&mix->i2c);
	kfree(mix);
	chip->mixer_data = NULL;
}

/* exported */
int __init snd_pmac_daca_init(struct snd_pmac *chip)
{
	int i, err;
	struct pmac_daca *mix;

#ifdef CONFIG_KMOD
	if (current->fs->root)
		request_module("i2c-powermac");
#endif /* CONFIG_KMOD */	

	mix = kzalloc(sizeof(*mix), GFP_KERNEL);
	if (! mix)
		return -ENOMEM;
	chip->mixer_data = mix;
	chip->mixer_free = daca_cleanup;
	mix->amp_on = 1; /* default on */

	mix->i2c.addr = DACA_I2C_ADDR;
	mix->i2c.init_client = daca_init_client;
	mix->i2c.name = "DACA";
	if ((err = snd_pmac_keywest_init(&mix->i2c)) < 0)
		return err;

	/*
	 * build mixers
	 */
	strcpy(chip->card->mixername, "PowerMac DACA");

	for (i = 0; i < ARRAY_SIZE(daca_mixers); i++) {
		if ((err = snd_ctl_add(chip->card, snd_ctl_new1(&daca_mixers[i], chip))) < 0)
			return err;
	}

#ifdef CONFIG_PM
	chip->resume = daca_resume;
#endif

	return 0;
}
