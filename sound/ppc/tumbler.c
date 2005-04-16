/*
 * PMac Tumbler/Snapper lowlevel functions
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
 *
 *   Rene Rebe <rene.rebe@gmx.net>:
 *     * update from shadow registers on wakeup and headphone plug
 *     * automatically toggle DRC on headphone plug
 *	
 */


#include <sound/driver.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <linux/kmod.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <sound/core.h>
#include <asm/io.h>
#include <asm/irq.h>
#ifdef CONFIG_PPC_HAS_FEATURE_CALLS
#include <asm/pmac_feature.h>
#endif
#include "pmac.h"
#include "tumbler_volume.h"

/* i2c address for tumbler */
#define TAS_I2C_ADDR	0x34

/* registers */
#define TAS_REG_MCS	0x01	/* main control */
#define TAS_REG_DRC	0x02
#define TAS_REG_VOL	0x04
#define TAS_REG_TREBLE	0x05
#define TAS_REG_BASS	0x06
#define TAS_REG_INPUT1	0x07
#define TAS_REG_INPUT2	0x08

/* tas3001c */
#define TAS_REG_PCM	TAS_REG_INPUT1
 
/* tas3004 */
#define TAS_REG_LMIX	TAS_REG_INPUT1
#define TAS_REG_RMIX	TAS_REG_INPUT2
#define TAS_REG_MCS2	0x43		/* main control 2 */
#define TAS_REG_ACS	0x40		/* analog control */

/* mono volumes for tas3001c/tas3004 */
enum {
	VOL_IDX_PCM_MONO, /* tas3001c only */
	VOL_IDX_BASS, VOL_IDX_TREBLE,
	VOL_IDX_LAST_MONO
};

/* stereo volumes for tas3004 */
enum {
	VOL_IDX_PCM, VOL_IDX_PCM2, VOL_IDX_ADC,
	VOL_IDX_LAST_MIX
};

typedef struct pmac_gpio {
#ifdef CONFIG_PPC_HAS_FEATURE_CALLS
	unsigned int addr;
#else
	void __iomem *addr;
#endif
	int active_state;
} pmac_gpio_t;

typedef struct pmac_tumbler_t {
	pmac_keywest_t i2c;
	pmac_gpio_t audio_reset;
	pmac_gpio_t amp_mute;
	pmac_gpio_t hp_mute;
	pmac_gpio_t hp_detect;
	int headphone_irq;
	unsigned int master_vol[2];
	unsigned int master_switch[2];
	unsigned int mono_vol[VOL_IDX_LAST_MONO];
	unsigned int mix_vol[VOL_IDX_LAST_MIX][2]; /* stereo volumes for tas3004 */
	int drc_range;
	int drc_enable;
	int capture_source;
} pmac_tumbler_t;


/*
 */

static int send_init_client(pmac_keywest_t *i2c, unsigned int *regs)
{
	while (*regs > 0) {
		int err, count = 10;
		do {
			err = i2c_smbus_write_byte_data(i2c->client,
							regs[0], regs[1]);
			if (err >= 0)
				break;
			mdelay(10);
		} while (count--);
		if (err < 0)
			return -ENXIO;
		regs += 2;
	}
	return 0;
}


static int tumbler_init_client(pmac_keywest_t *i2c)
{
	static unsigned int regs[] = {
		/* normal operation, SCLK=64fps, i2s output, i2s input, 16bit width */
		TAS_REG_MCS, (1<<6)|(2<<4)|(2<<2)|0,
		0, /* terminator */
	};
	return send_init_client(i2c, regs);
}

static int snapper_init_client(pmac_keywest_t *i2c)
{
	static unsigned int regs[] = {
		/* normal operation, SCLK=64fps, i2s output, 16bit width */
		TAS_REG_MCS, (1<<6)|(2<<4)|0,
		/* normal operation, all-pass mode */
		TAS_REG_MCS2, (1<<1),
		/* normal output, no deemphasis, A input, power-up, line-in */
		TAS_REG_ACS, 0,
		0, /* terminator */
	};
	return send_init_client(i2c, regs);
}
	
/*
 * gpio access
 */
#ifdef CONFIG_PPC_HAS_FEATURE_CALLS
#define do_gpio_write(gp, val) \
	pmac_call_feature(PMAC_FTR_WRITE_GPIO, NULL, (gp)->addr, val)
#define do_gpio_read(gp) \
	pmac_call_feature(PMAC_FTR_READ_GPIO, NULL, (gp)->addr, 0)
#define tumbler_gpio_free(gp) /* NOP */
#else
#define do_gpio_write(gp, val)	writeb(val, (gp)->addr)
#define do_gpio_read(gp)	readb((gp)->addr)
static inline void tumbler_gpio_free(pmac_gpio_t *gp)
{
	if (gp->addr) {
		iounmap(gp->addr);
		gp->addr = NULL;
	}
}
#endif /* CONFIG_PPC_HAS_FEATURE_CALLS */

static void write_audio_gpio(pmac_gpio_t *gp, int active)
{
	if (! gp->addr)
		return;
	active = active ? gp->active_state : !gp->active_state;
	do_gpio_write(gp, active ? 0x05 : 0x04);
}

static int read_audio_gpio(pmac_gpio_t *gp)
{
	int ret;
	if (! gp->addr)
		return 0;
	ret = ((do_gpio_read(gp) & 0x02) !=0);
	return ret == gp->active_state;
}

/*
 * update master volume
 */
static int tumbler_set_master_volume(pmac_tumbler_t *mix)
{
	unsigned char block[6];
	unsigned int left_vol, right_vol;
  
	if (! mix->i2c.client)
		return -ENODEV;
  
	if (! mix->master_switch[0])
		left_vol = 0;
	else {
		left_vol = mix->master_vol[0];
		if (left_vol >= ARRAY_SIZE(master_volume_table))
			left_vol = ARRAY_SIZE(master_volume_table) - 1;
		left_vol = master_volume_table[left_vol];
	}
	if (! mix->master_switch[1])
		right_vol = 0;
	else {
		right_vol = mix->master_vol[1];
		if (right_vol >= ARRAY_SIZE(master_volume_table))
			right_vol = ARRAY_SIZE(master_volume_table) - 1;
		right_vol = master_volume_table[right_vol];
	}

	block[0] = (left_vol >> 16) & 0xff;
	block[1] = (left_vol >> 8)  & 0xff;
	block[2] = (left_vol >> 0)  & 0xff;

	block[3] = (right_vol >> 16) & 0xff;
	block[4] = (right_vol >> 8)  & 0xff;
	block[5] = (right_vol >> 0)  & 0xff;
  
	if (i2c_smbus_write_block_data(mix->i2c.client, TAS_REG_VOL,
				       6, block) < 0) {
		snd_printk("failed to set volume \n");
		return -EINVAL;
	}
	return 0;
}


/* output volume */
static int tumbler_info_master_volume(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = ARRAY_SIZE(master_volume_table) - 1;
	return 0;
}

static int tumbler_get_master_volume(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	pmac_t *chip = snd_kcontrol_chip(kcontrol);
	pmac_tumbler_t *mix = chip->mixer_data;
	snd_assert(mix, return -ENODEV);
	ucontrol->value.integer.value[0] = mix->master_vol[0];
	ucontrol->value.integer.value[1] = mix->master_vol[1];
	return 0;
}

static int tumbler_put_master_volume(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	pmac_t *chip = snd_kcontrol_chip(kcontrol);
	pmac_tumbler_t *mix = chip->mixer_data;
	int change;

	snd_assert(mix, return -ENODEV);
	change = mix->master_vol[0] != ucontrol->value.integer.value[0] ||
		mix->master_vol[1] != ucontrol->value.integer.value[1];
	if (change) {
		mix->master_vol[0] = ucontrol->value.integer.value[0];
		mix->master_vol[1] = ucontrol->value.integer.value[1];
		tumbler_set_master_volume(mix);
	}
	return change;
}

/* output switch */
static int tumbler_get_master_switch(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	pmac_t *chip = snd_kcontrol_chip(kcontrol);
	pmac_tumbler_t *mix = chip->mixer_data;
	snd_assert(mix, return -ENODEV);
	ucontrol->value.integer.value[0] = mix->master_switch[0];
	ucontrol->value.integer.value[1] = mix->master_switch[1];
	return 0;
}

static int tumbler_put_master_switch(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	pmac_t *chip = snd_kcontrol_chip(kcontrol);
	pmac_tumbler_t *mix = chip->mixer_data;
	int change;

	snd_assert(mix, return -ENODEV);
	change = mix->master_switch[0] != ucontrol->value.integer.value[0] ||
		mix->master_switch[1] != ucontrol->value.integer.value[1];
	if (change) {
		mix->master_switch[0] = !!ucontrol->value.integer.value[0];
		mix->master_switch[1] = !!ucontrol->value.integer.value[1];
		tumbler_set_master_volume(mix);
	}
	return change;
}


/*
 * TAS3001c dynamic range compression
 */

#define TAS3001_DRC_MAX		0x5f

static int tumbler_set_drc(pmac_tumbler_t *mix)
{
	unsigned char val[2];

	if (! mix->i2c.client)
		return -ENODEV;
  
	if (mix->drc_enable) {
		val[0] = 0xc1; /* enable, 3:1 compression */
		if (mix->drc_range > TAS3001_DRC_MAX)
			val[1] = 0xf0;
		else if (mix->drc_range < 0)
			val[1] = 0x91;
		else
			val[1] = mix->drc_range + 0x91;
	} else {
		val[0] = 0;
		val[1] = 0;
	}

	if (i2c_smbus_write_block_data(mix->i2c.client, TAS_REG_DRC,
				       2, val) < 0) {
		snd_printk("failed to set DRC\n");
		return -EINVAL;
	}
	return 0;
}

/*
 * TAS3004
 */

#define TAS3004_DRC_MAX		0xef

static int snapper_set_drc(pmac_tumbler_t *mix)
{
	unsigned char val[6];

	if (! mix->i2c.client)
		return -ENODEV;
  
	if (mix->drc_enable)
		val[0] = 0x50; /* 3:1 above threshold */
	else
		val[0] = 0x51; /* disabled */
	val[1] = 0x02; /* 1:1 below threshold */
	if (mix->drc_range > 0xef)
		val[2] = 0xef;
	else if (mix->drc_range < 0)
		val[2] = 0x00;
	else
		val[2] = mix->drc_range;
	val[3] = 0xb0;
	val[4] = 0x60;
	val[5] = 0xa0;

	if (i2c_smbus_write_block_data(mix->i2c.client, TAS_REG_DRC,
				       6, val) < 0) {
		snd_printk("failed to set DRC\n");
		return -EINVAL;
	}
	return 0;
}

static int tumbler_info_drc_value(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t *uinfo)
{
	pmac_t *chip = snd_kcontrol_chip(kcontrol);
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max =
		chip->model == PMAC_TUMBLER ? TAS3001_DRC_MAX : TAS3004_DRC_MAX;
	return 0;
}

static int tumbler_get_drc_value(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	pmac_t *chip = snd_kcontrol_chip(kcontrol);
	pmac_tumbler_t *mix;
	if (! (mix = chip->mixer_data))
		return -ENODEV;
	ucontrol->value.integer.value[0] = mix->drc_range;
	return 0;
}

static int tumbler_put_drc_value(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	pmac_t *chip = snd_kcontrol_chip(kcontrol);
	pmac_tumbler_t *mix;
	int change;

	if (! (mix = chip->mixer_data))
		return -ENODEV;
	change = mix->drc_range != ucontrol->value.integer.value[0];
	if (change) {
		mix->drc_range = ucontrol->value.integer.value[0];
		if (chip->model == PMAC_TUMBLER)
			tumbler_set_drc(mix);
		else
			snapper_set_drc(mix);
	}
	return change;
}

static int tumbler_get_drc_switch(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	pmac_t *chip = snd_kcontrol_chip(kcontrol);
	pmac_tumbler_t *mix;
	if (! (mix = chip->mixer_data))
		return -ENODEV;
	ucontrol->value.integer.value[0] = mix->drc_enable;
	return 0;
}

static int tumbler_put_drc_switch(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	pmac_t *chip = snd_kcontrol_chip(kcontrol);
	pmac_tumbler_t *mix;
	int change;

	if (! (mix = chip->mixer_data))
		return -ENODEV;
	change = mix->drc_enable != ucontrol->value.integer.value[0];
	if (change) {
		mix->drc_enable = !!ucontrol->value.integer.value[0];
		if (chip->model == PMAC_TUMBLER)
			tumbler_set_drc(mix);
		else
			snapper_set_drc(mix);
	}
	return change;
}


/*
 * mono volumes
 */

struct tumbler_mono_vol {
	int index;
	int reg;
	int bytes;
	unsigned int max;
	unsigned int *table;
};

static int tumbler_set_mono_volume(pmac_tumbler_t *mix, struct tumbler_mono_vol *info)
{
	unsigned char block[4];
	unsigned int vol;
	int i;
  
	if (! mix->i2c.client)
		return -ENODEV;
  
	vol = mix->mono_vol[info->index];
	if (vol >= info->max)
		vol = info->max - 1;
	vol = info->table[vol];
	for (i = 0; i < info->bytes; i++)
		block[i] = (vol >> ((info->bytes - i - 1) * 8)) & 0xff;
	if (i2c_smbus_write_block_data(mix->i2c.client, info->reg,
				       info->bytes, block) < 0) {
		snd_printk("failed to set mono volume %d\n", info->index);
		return -EINVAL;
	}
	return 0;
}

static int tumbler_info_mono(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t *uinfo)
{
	struct tumbler_mono_vol *info = (struct tumbler_mono_vol *)kcontrol->private_value;

	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 1;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = info->max - 1;
	return 0;
}

static int tumbler_get_mono(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	struct tumbler_mono_vol *info = (struct tumbler_mono_vol *)kcontrol->private_value;
	pmac_t *chip = snd_kcontrol_chip(kcontrol);
	pmac_tumbler_t *mix;
	if (! (mix = chip->mixer_data))
		return -ENODEV;
	ucontrol->value.integer.value[0] = mix->mono_vol[info->index];
	return 0;
}

static int tumbler_put_mono(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	struct tumbler_mono_vol *info = (struct tumbler_mono_vol *)kcontrol->private_value;
	pmac_t *chip = snd_kcontrol_chip(kcontrol);
	pmac_tumbler_t *mix;
	int change;

	if (! (mix = chip->mixer_data))
		return -ENODEV;
	change = mix->mono_vol[info->index] != ucontrol->value.integer.value[0];
	if (change) {
		mix->mono_vol[info->index] = ucontrol->value.integer.value[0];
		tumbler_set_mono_volume(mix, info);
	}
	return change;
}

/* TAS3001c mono volumes */
static struct tumbler_mono_vol tumbler_pcm_vol_info = {
	.index = VOL_IDX_PCM_MONO,
	.reg = TAS_REG_PCM,
	.bytes = 3,
	.max = ARRAY_SIZE(mixer_volume_table),
	.table = mixer_volume_table,
};

static struct tumbler_mono_vol tumbler_bass_vol_info = {
	.index = VOL_IDX_BASS,
	.reg = TAS_REG_BASS,
	.bytes = 1,
	.max = ARRAY_SIZE(bass_volume_table),
	.table = bass_volume_table,
};

static struct tumbler_mono_vol tumbler_treble_vol_info = {
	.index = VOL_IDX_TREBLE,
	.reg = TAS_REG_TREBLE,
	.bytes = 1,
	.max = ARRAY_SIZE(treble_volume_table),
	.table = treble_volume_table,
};

/* TAS3004 mono volumes */
static struct tumbler_mono_vol snapper_bass_vol_info = {
	.index = VOL_IDX_BASS,
	.reg = TAS_REG_BASS,
	.bytes = 1,
	.max = ARRAY_SIZE(snapper_bass_volume_table),
	.table = snapper_bass_volume_table,
};

static struct tumbler_mono_vol snapper_treble_vol_info = {
	.index = VOL_IDX_TREBLE,
	.reg = TAS_REG_TREBLE,
	.bytes = 1,
	.max = ARRAY_SIZE(snapper_treble_volume_table),
	.table = snapper_treble_volume_table,
};


#define DEFINE_MONO(xname,type) { \
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,\
	.name = xname, \
	.info = tumbler_info_mono, \
	.get = tumbler_get_mono, \
	.put = tumbler_put_mono, \
	.private_value = (unsigned long)(&tumbler_##type##_vol_info), \
}

#define DEFINE_SNAPPER_MONO(xname,type) { \
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,\
	.name = xname, \
	.info = tumbler_info_mono, \
	.get = tumbler_get_mono, \
	.put = tumbler_put_mono, \
	.private_value = (unsigned long)(&snapper_##type##_vol_info), \
}


/*
 * snapper mixer volumes
 */

static int snapper_set_mix_vol1(pmac_tumbler_t *mix, int idx, int ch, int reg)
{
	int i, j, vol;
	unsigned char block[9];

	vol = mix->mix_vol[idx][ch];
	if (vol >= ARRAY_SIZE(mixer_volume_table)) {
		vol = ARRAY_SIZE(mixer_volume_table) - 1;
		mix->mix_vol[idx][ch] = vol;
	}

	for (i = 0; i < 3; i++) {
		vol = mix->mix_vol[i][ch];
		vol = mixer_volume_table[vol];
		for (j = 0; j < 3; j++)
			block[i * 3 + j] = (vol >> ((2 - j) * 8)) & 0xff;
	}
	if (i2c_smbus_write_block_data(mix->i2c.client, reg, 9, block) < 0) {
		snd_printk("failed to set mono volume %d\n", reg);
		return -EINVAL;
	}
	return 0;
}

static int snapper_set_mix_vol(pmac_tumbler_t *mix, int idx)
{
	if (! mix->i2c.client)
		return -ENODEV;
	if (snapper_set_mix_vol1(mix, idx, 0, TAS_REG_LMIX) < 0 ||
	    snapper_set_mix_vol1(mix, idx, 1, TAS_REG_RMIX) < 0)
		return -EINVAL;
	return 0;
}

static int snapper_info_mix(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = ARRAY_SIZE(mixer_volume_table) - 1;
	return 0;
}

static int snapper_get_mix(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	int idx = (int)kcontrol->private_value;
	pmac_t *chip = snd_kcontrol_chip(kcontrol);
	pmac_tumbler_t *mix;
	if (! (mix = chip->mixer_data))
		return -ENODEV;
	ucontrol->value.integer.value[0] = mix->mix_vol[idx][0];
	ucontrol->value.integer.value[1] = mix->mix_vol[idx][1];
	return 0;
}

static int snapper_put_mix(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	int idx = (int)kcontrol->private_value;
	pmac_t *chip = snd_kcontrol_chip(kcontrol);
	pmac_tumbler_t *mix;
	int change;

	if (! (mix = chip->mixer_data))
		return -ENODEV;
	change = mix->mix_vol[idx][0] != ucontrol->value.integer.value[0] ||
		mix->mix_vol[idx][1] != ucontrol->value.integer.value[1];
	if (change) {
		mix->mix_vol[idx][0] = ucontrol->value.integer.value[0];
		mix->mix_vol[idx][1] = ucontrol->value.integer.value[1];
		snapper_set_mix_vol(mix, idx);
	}
	return change;
}


/*
 * mute switches
 */

enum { TUMBLER_MUTE_HP, TUMBLER_MUTE_AMP };

static int tumbler_get_mute_switch(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	pmac_t *chip = snd_kcontrol_chip(kcontrol);
	pmac_tumbler_t *mix;
	pmac_gpio_t *gp;
	if (! (mix = chip->mixer_data))
		return -ENODEV;
	gp = (kcontrol->private_value == TUMBLER_MUTE_HP) ? &mix->hp_mute : &mix->amp_mute;
	ucontrol->value.integer.value[0] = ! read_audio_gpio(gp);
	return 0;
}

static int tumbler_put_mute_switch(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	pmac_t *chip = snd_kcontrol_chip(kcontrol);
	pmac_tumbler_t *mix;
	pmac_gpio_t *gp;
	int val;
#ifdef PMAC_SUPPORT_AUTOMUTE
	if (chip->update_automute && chip->auto_mute)
		return 0; /* don't touch in the auto-mute mode */
#endif	
	if (! (mix = chip->mixer_data))
		return -ENODEV;
	gp = (kcontrol->private_value == TUMBLER_MUTE_HP) ? &mix->hp_mute : &mix->amp_mute;
	val = ! read_audio_gpio(gp);
	if (val != ucontrol->value.integer.value[0]) {
		write_audio_gpio(gp, ! ucontrol->value.integer.value[0]);
		return 1;
	}
	return 0;
}

static int snapper_set_capture_source(pmac_tumbler_t *mix)
{
	if (! mix->i2c.client)
		return -ENODEV;
	return i2c_smbus_write_byte_data(mix->i2c.client, TAS_REG_ACS,
					 mix->capture_source ? 2 : 0);
}

static int snapper_info_capture_source(snd_kcontrol_t *kcontrol, snd_ctl_elem_info_t *uinfo)
{
	static char *texts[2] = {
		"Line", "Mic"
	};
	uinfo->type = SNDRV_CTL_ELEM_TYPE_ENUMERATED;
	uinfo->count = 1;
	uinfo->value.enumerated.items = 2;
	if (uinfo->value.enumerated.item > 1)
		uinfo->value.enumerated.item = 1;
	strcpy(uinfo->value.enumerated.name, texts[uinfo->value.enumerated.item]);
	return 0;
}

static int snapper_get_capture_source(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	pmac_t *chip = snd_kcontrol_chip(kcontrol);
	pmac_tumbler_t *mix = chip->mixer_data;

	snd_assert(mix, return -ENODEV);
	ucontrol->value.integer.value[0] = mix->capture_source;
	return 0;
}

static int snapper_put_capture_source(snd_kcontrol_t *kcontrol, snd_ctl_elem_value_t *ucontrol)
{
	pmac_t *chip = snd_kcontrol_chip(kcontrol);
	pmac_tumbler_t *mix = chip->mixer_data;
	int change;

	snd_assert(mix, return -ENODEV);
	change = ucontrol->value.integer.value[0] != mix->capture_source;
	if (change) {
		mix->capture_source = !!ucontrol->value.integer.value[0];
		snapper_set_capture_source(mix);
	}
	return change;
}

#define DEFINE_SNAPPER_MIX(xname,idx,ofs) { \
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,\
	.name = xname, \
	.info = snapper_info_mix, \
	.get = snapper_get_mix, \
	.put = snapper_put_mix, \
	.index = idx,\
	.private_value = ofs, \
}


/*
 */
static snd_kcontrol_new_t tumbler_mixers[] __initdata = {
	{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	  .name = "Master Playback Volume",
	  .info = tumbler_info_master_volume,
	  .get = tumbler_get_master_volume,
	  .put = tumbler_put_master_volume
	},
	{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	  .name = "Master Playback Switch",
	  .info = snd_pmac_boolean_stereo_info,
	  .get = tumbler_get_master_switch,
	  .put = tumbler_put_master_switch
	},
	DEFINE_MONO("Tone Control - Bass", bass),
	DEFINE_MONO("Tone Control - Treble", treble),
	DEFINE_MONO("PCM Playback Volume", pcm),
	{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	  .name = "DRC Range",
	  .info = tumbler_info_drc_value,
	  .get = tumbler_get_drc_value,
	  .put = tumbler_put_drc_value
	},
};

static snd_kcontrol_new_t snapper_mixers[] __initdata = {
	{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	  .name = "Master Playback Volume",
	  .info = tumbler_info_master_volume,
	  .get = tumbler_get_master_volume,
	  .put = tumbler_put_master_volume
	},
	{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	  .name = "Master Playback Switch",
	  .info = snd_pmac_boolean_stereo_info,
	  .get = tumbler_get_master_switch,
	  .put = tumbler_put_master_switch
	},
	DEFINE_SNAPPER_MIX("PCM Playback Volume", 0, VOL_IDX_PCM),
	DEFINE_SNAPPER_MIX("PCM Playback Volume", 1, VOL_IDX_PCM2),
	DEFINE_SNAPPER_MIX("Monitor Mix Volume", 0, VOL_IDX_ADC),
	DEFINE_SNAPPER_MONO("Tone Control - Bass", bass),
	DEFINE_SNAPPER_MONO("Tone Control - Treble", treble),
	{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	  .name = "DRC Range",
	  .info = tumbler_info_drc_value,
	  .get = tumbler_get_drc_value,
	  .put = tumbler_put_drc_value
	},
	{ .iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	  .name = "Input Source", /* FIXME: "Capture Source" doesn't work properly */
	  .info = snapper_info_capture_source,
	  .get = snapper_get_capture_source,
	  .put = snapper_put_capture_source
	},
};

static snd_kcontrol_new_t tumbler_hp_sw __initdata = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Headphone Playback Switch",
	.info = snd_pmac_boolean_mono_info,
	.get = tumbler_get_mute_switch,
	.put = tumbler_put_mute_switch,
	.private_value = TUMBLER_MUTE_HP,
};
static snd_kcontrol_new_t tumbler_speaker_sw __initdata = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "PC Speaker Playback Switch",
	.info = snd_pmac_boolean_mono_info,
	.get = tumbler_get_mute_switch,
	.put = tumbler_put_mute_switch,
	.private_value = TUMBLER_MUTE_AMP,
};
static snd_kcontrol_new_t tumbler_drc_sw __initdata = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "DRC Switch",
	.info = snd_pmac_boolean_mono_info,
	.get = tumbler_get_drc_switch,
	.put = tumbler_put_drc_switch
};


#ifdef PMAC_SUPPORT_AUTOMUTE
/*
 * auto-mute stuffs
 */
static int tumbler_detect_headphone(pmac_t *chip)
{
	pmac_tumbler_t *mix = chip->mixer_data;
	return read_audio_gpio(&mix->hp_detect);
}

static void check_mute(pmac_t *chip, pmac_gpio_t *gp, int val, int do_notify, snd_kcontrol_t *sw)
{
	//pmac_tumbler_t *mix = chip->mixer_data;
	if (val != read_audio_gpio(gp)) {
		write_audio_gpio(gp, val);
		if (do_notify)
			snd_ctl_notify(chip->card, SNDRV_CTL_EVENT_MASK_VALUE, &sw->id);
	}
}

static struct work_struct device_change;

static void
device_change_handler(void *self)
{
	pmac_t *chip = (pmac_t*) self;
	pmac_tumbler_t *mix;

	if (!chip)
		return;

	mix = chip->mixer_data;

	/* first set the DRC so the speaker do not explode -ReneR */
	if (chip->model == PMAC_TUMBLER)
		tumbler_set_drc(mix);
	else
		snapper_set_drc(mix);

	/* reset the master volume so the correct amplification is applied */
	tumbler_set_master_volume(mix);
}

static void tumbler_update_automute(pmac_t *chip, int do_notify)
{
	if (chip->auto_mute) {
		pmac_tumbler_t *mix = chip->mixer_data;
		snd_assert(mix, return);
		if (tumbler_detect_headphone(chip)) {
			/* mute speaker */
			check_mute(chip, &mix->amp_mute, 1, do_notify, chip->speaker_sw_ctl);
			check_mute(chip, &mix->hp_mute, 0, do_notify, chip->master_sw_ctl);
			mix->drc_enable = 0;

		} else {
			/* unmute speaker */
			check_mute(chip, &mix->amp_mute, 0, do_notify, chip->speaker_sw_ctl);
			check_mute(chip, &mix->hp_mute, 1, do_notify, chip->master_sw_ctl);
			mix->drc_enable = 1;
		}
		if (do_notify) {
			snd_ctl_notify(chip->card, SNDRV_CTL_EVENT_MASK_VALUE,
				       &chip->hp_detect_ctl->id);
			snd_ctl_notify(chip->card, SNDRV_CTL_EVENT_MASK_VALUE,
			               &chip->drc_sw_ctl->id);
		}

		/* finally we need to schedule an update of the mixer values
		   (master and DRC are enough for now) -ReneR */
		schedule_work(&device_change);

	}
}
#endif /* PMAC_SUPPORT_AUTOMUTE */


/* interrupt - headphone plug changed */
static irqreturn_t headphone_intr(int irq, void *devid, struct pt_regs *regs)
{
	pmac_t *chip = devid;
	if (chip->update_automute && chip->initialized) {
		chip->update_automute(chip, 1);
		return IRQ_HANDLED;
	}
	return IRQ_NONE;
}

/* look for audio-gpio device */
static struct device_node *find_audio_device(const char *name)
{
	struct device_node *np;
  
	if (! (np = find_devices("gpio")))
		return NULL;
  
	for (np = np->child; np; np = np->sibling) {
		char *property = get_property(np, "audio-gpio", NULL);
		if (property && strcmp(property, name) == 0)
			return np;
	}  
	return NULL;
}

/* look for audio-gpio device */
static struct device_node *find_compatible_audio_device(const char *name)
{
	struct device_node *np;
  
	if (! (np = find_devices("gpio")))
		return NULL;
  
	for (np = np->child; np; np = np->sibling) {
		if (device_is_compatible(np, name))
			return np;
	}  
	return NULL;
}

/* find an audio device and get its address */
static unsigned long tumbler_find_device(const char *device, pmac_gpio_t *gp, int is_compatible)
{
	struct device_node *node;
	u32 *base;

	if (is_compatible)
		node = find_compatible_audio_device(device);
	else
		node = find_audio_device(device);
	if (! node) {
		snd_printdd("cannot find device %s\n", device);
		return -ENODEV;
	}

	base = (u32 *)get_property(node, "AAPL,address", NULL);
	if (! base) {
		snd_printd("cannot find address for device %s\n", device);
		return -ENODEV;
	}

#ifdef CONFIG_PPC_HAS_FEATURE_CALLS
	gp->addr = (*base) & 0x0000ffff;
#else
	gp->addr = ioremap((unsigned long)(*base), 1);
#endif
	base = (u32 *)get_property(node, "audio-gpio-active-state", NULL);
	if (base)
		gp->active_state = *base;
	else
		gp->active_state = 1;


	return (node->n_intrs > 0) ? node->intrs[0].line : 0;
}

/* reset audio */
static void tumbler_reset_audio(pmac_t *chip)
{
	pmac_tumbler_t *mix = chip->mixer_data;

	write_audio_gpio(&mix->audio_reset, 0);
	big_mdelay(200);
	write_audio_gpio(&mix->audio_reset, 1);
	big_mdelay(100);
	write_audio_gpio(&mix->audio_reset, 0);
	big_mdelay(100);
}

#ifdef CONFIG_PMAC_PBOOK
/* resume mixer */
static void tumbler_resume(pmac_t *chip)
{
	pmac_tumbler_t *mix = chip->mixer_data;

	snd_assert(mix, return);

	tumbler_reset_audio(chip);
	if (mix->i2c.client && mix->i2c.init_client) {
		if (mix->i2c.init_client(&mix->i2c) < 0)
			printk(KERN_ERR "tumbler_init_client error\n");
	} else
		printk(KERN_ERR "tumbler: i2c is not initialized\n");
	if (chip->model == PMAC_TUMBLER) {
		tumbler_set_mono_volume(mix, &tumbler_pcm_vol_info);
		tumbler_set_mono_volume(mix, &tumbler_bass_vol_info);
		tumbler_set_mono_volume(mix, &tumbler_treble_vol_info);
		tumbler_set_drc(mix);
	} else {
		snapper_set_mix_vol(mix, VOL_IDX_PCM);
		snapper_set_mix_vol(mix, VOL_IDX_PCM2);
		snapper_set_mix_vol(mix, VOL_IDX_ADC);
		tumbler_set_mono_volume(mix, &snapper_bass_vol_info);
		tumbler_set_mono_volume(mix, &snapper_treble_vol_info);
		snapper_set_drc(mix);
		snapper_set_capture_source(mix);
	}
	tumbler_set_master_volume(mix);
	if (chip->update_automute)
		chip->update_automute(chip, 0);
}
#endif

/* initialize tumbler */
static int __init tumbler_init(pmac_t *chip)
{
	int irq, err;
	pmac_tumbler_t *mix = chip->mixer_data;
	snd_assert(mix, return -EINVAL);

	tumbler_find_device("audio-hw-reset", &mix->audio_reset, 0);
	tumbler_find_device("amp-mute", &mix->amp_mute, 0);
	tumbler_find_device("headphone-mute", &mix->hp_mute, 0);
	irq = tumbler_find_device("headphone-detect", &mix->hp_detect, 0);
	if (irq < 0)
		irq = tumbler_find_device("keywest-gpio15", &mix->hp_detect, 1);

	tumbler_reset_audio(chip);

	/* activate headphone status interrupts */
  	if (irq >= 0) {
		unsigned char val;
		if ((err = request_irq(irq, headphone_intr, 0,
				       "Tumbler Headphone Detection", chip)) < 0)
			return err;
		/* activate headphone status interrupts */
		val = do_gpio_read(&mix->hp_detect);
		do_gpio_write(&mix->hp_detect, val | 0x80);
	}
	mix->headphone_irq = irq;
  
	return 0;
}

static void tumbler_cleanup(pmac_t *chip)
{
	pmac_tumbler_t *mix = chip->mixer_data;
	if (! mix)
		return;

	if (mix->headphone_irq >= 0)
		free_irq(mix->headphone_irq, chip);
	tumbler_gpio_free(&mix->audio_reset);
	tumbler_gpio_free(&mix->amp_mute);
	tumbler_gpio_free(&mix->hp_mute);
	tumbler_gpio_free(&mix->hp_detect);
	snd_pmac_keywest_cleanup(&mix->i2c);
	kfree(mix);
	chip->mixer_data = NULL;
}

/* exported */
int __init snd_pmac_tumbler_init(pmac_t *chip)
{
	int i, err;
	pmac_tumbler_t *mix;
	u32 *paddr;
	struct device_node *tas_node;
	char *chipname;

#ifdef CONFIG_KMOD
	if (current->fs->root)
		request_module("i2c-keywest");
#endif /* CONFIG_KMOD */	

	mix = kmalloc(sizeof(*mix), GFP_KERNEL);
	if (! mix)
		return -ENOMEM;
	memset(mix, 0, sizeof(*mix));
	mix->headphone_irq = -1;

	chip->mixer_data = mix;
	chip->mixer_free = tumbler_cleanup;

	if ((err = tumbler_init(chip)) < 0)
		return err;

	/* set up TAS */
	tas_node = find_devices("deq");
	if (tas_node == NULL)
		return -ENODEV;

	paddr = (u32 *)get_property(tas_node, "i2c-address", NULL);
	if (paddr)
		mix->i2c.addr = (*paddr) >> 1;
	else
		mix->i2c.addr = TAS_I2C_ADDR;

	if (chip->model == PMAC_TUMBLER) {
		mix->i2c.init_client = tumbler_init_client;
		mix->i2c.name = "TAS3001c";
		chipname = "Tumbler";
	} else {
		mix->i2c.init_client = snapper_init_client;
		mix->i2c.name = "TAS3004";
		chipname = "Snapper";
	}

	if ((err = snd_pmac_keywest_init(&mix->i2c)) < 0)
		return err;

	/*
	 * build mixers
	 */
	sprintf(chip->card->mixername, "PowerMac %s", chipname);

	if (chip->model == PMAC_TUMBLER) {
		for (i = 0; i < ARRAY_SIZE(tumbler_mixers); i++) {
			if ((err = snd_ctl_add(chip->card, snd_ctl_new1(&tumbler_mixers[i], chip))) < 0)
				return err;
		}
	} else {
		for (i = 0; i < ARRAY_SIZE(snapper_mixers); i++) {
			if ((err = snd_ctl_add(chip->card, snd_ctl_new1(&snapper_mixers[i], chip))) < 0)
				return err;
		}
	}
	chip->master_sw_ctl = snd_ctl_new1(&tumbler_hp_sw, chip);
	if ((err = snd_ctl_add(chip->card, chip->master_sw_ctl)) < 0)
		return err;
	chip->speaker_sw_ctl = snd_ctl_new1(&tumbler_speaker_sw, chip);
	if ((err = snd_ctl_add(chip->card, chip->speaker_sw_ctl)) < 0)
		return err;
	chip->drc_sw_ctl = snd_ctl_new1(&tumbler_drc_sw, chip);
	if ((err = snd_ctl_add(chip->card, chip->drc_sw_ctl)) < 0)
		return err;


#ifdef CONFIG_PMAC_PBOOK
	chip->resume = tumbler_resume;
#endif

	INIT_WORK(&device_change, device_change_handler, (void *)chip);

#ifdef PMAC_SUPPORT_AUTOMUTE
	if (mix->headphone_irq >=0 && (err = snd_pmac_add_automute(chip)) < 0)
		return err;
	chip->detect_headphone = tumbler_detect_headphone;
	chip->update_automute = tumbler_update_automute;
	tumbler_update_automute(chip, 0); /* update the status only */
#endif

	return 0;
}
